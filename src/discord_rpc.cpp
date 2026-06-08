#include "discord_rpc.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

DiscordRpcClient::DiscordRpcClient(const std::string& application_id)
    : application_id_(application_id)
    , pid_(GetCurrentProcessId()) {}

DiscordRpcClient::~DiscordRpcClient() {
    Stop();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool DiscordRpcClient::Start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_.load()) return connected_.load();

    if (application_id_.empty()) return false;

    if (!ConnectToPipe()) return false;
    if (!Handshake()) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }

    running_ = true;
    connected_ = true;
    reader_thread_ = std::thread(&DiscordRpcClient::ReaderThread, this);
    return true;
}

void DiscordRpcClient::Stop() {
    // Set running to false first so the reader thread exits
    running_ = false;

    if (reader_thread_.joinable()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            // Cancel pending I/O so ReadFile in reader thread unblocks
            CancelIo(pipe_);
            // Send close frame (best effort, ignore failure)
            SendFrame(OPCODE_CLOSE, "{}");
            // Close the pipe — reader thread will detect this
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
    }

    // Join outside the mutex to avoid deadlock (reader thread also acquires mutex_)
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    connected_ = false;
}

bool DiscordRpcClient::IsConnected() const {
    return connected_.load();
}

void DiscordRpcClient::SetActivity(const RpcActivity& activity) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_.load() || pipe_ == INVALID_HANDLE_VALUE) return;

    json act = json::object();
    if (!activity.details.empty()) act["details"] = activity.details;
    if (!activity.state.empty())   act["state"] = activity.state;

    if (activity.start_timestamp > 0 || activity.end_timestamp > 0) {
        json ts = json::object();
        if (activity.start_timestamp > 0) ts["start"] = activity.start_timestamp / 1000;
        if (activity.end_timestamp > 0)   ts["end"] = activity.end_timestamp / 1000;
        act["timestamps"] = ts;
    }

    if (!activity.large_image_key.empty() || !activity.small_image_key.empty()) {
        json assets = json::object();
        if (!activity.large_image_key.empty())  assets["large_image"] = activity.large_image_key;
        if (!activity.large_image_text.empty())  assets["large_text"] = activity.large_image_text;
        if (!activity.small_image_key.empty())   assets["small_image"] = activity.small_image_key;
        if (!activity.small_image_text.empty())   assets["small_text"] = activity.small_image_text;
        act["assets"] = assets;
    }

    json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"args", {
            {"pid", static_cast<int>(pid_)},
            {"activity", act}
        }},
        {"nonce", std::to_string(GetTickCount64())}
    };

    SendFrame(OPCODE_FRAME, payload.dump());
}

void DiscordRpcClient::ClearActivity() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_.load() || pipe_ == INVALID_HANDLE_VALUE) return;

    json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"args", {
            {"pid", static_cast<int>(pid_)},
            {"activity", nullptr}
        }},
        {"nonce", std::to_string(GetTickCount64())}
    };

    SendFrame(OPCODE_FRAME, payload.dump());
}

// ---------------------------------------------------------------------------
// Pipe Connection
// ---------------------------------------------------------------------------

bool DiscordRpcClient::ConnectToPipe() {
    // Try pipes \\.\pipe\discord-ipc-0 through \\.\pipe\discord-ipc-9
    for (int i = 0; i < 10; ++i) {
        std::string pipe_name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        std::wstring wide_name(pipe_name.begin(), pipe_name.end());

        HANDLE h = CreateFileW(
            wide_name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (h != INVALID_HANDLE_VALUE) {
            pipe_ = h;
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

bool DiscordRpcClient::Handshake() {
    json handshake = {
        {"v", 1},
        {"client_id", application_id_}
    };

    if (!SendFrame(OPCODE_HANDSHAKE, handshake.dump())) {
        return false;
    }

    // Read the READY response (with a timeout)
    uint32_t opcode = 0;
    std::string payload;
    if (!ReadFrame(opcode, payload, 10000)) {
        return false;
    }

    if (opcode == OPCODE_FRAME) {
        // Parse and check for error
        auto resp = json::parse(payload, nullptr, false);
        if (!resp.is_discarded() && resp.contains("cmd") && resp["cmd"] == "DISPATCH" &&
            resp.contains("evt") && resp["evt"] == "READY") {
            return true;
        }
        // Some Discord versions respond with opcode FRAME + cmd=DISPATCH + evt=READY
        // Others might respond differently — accept any FRAME response as success
        // since errors come as opcode FRAME with an "code" field
        if (!resp.is_discarded() && resp.contains("code")) {
            // Error response
            OutputDebugStringA("DiscordRpc: Handshake error response\n");
            return false;
        }
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Reader Thread
// ---------------------------------------------------------------------------

void DiscordRpcClient::ReaderThread() {
    while (running_.load()) {
        uint32_t opcode = 0;
        std::string payload;

        if (!ReadFrame(opcode, payload, 5000)) {
            // Pipe disconnected or error
            if (running_.load()) {
                connected_ = false;
                // Close the broken pipe
                std::lock_guard<std::mutex> lock(mutex_);
                if (pipe_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(pipe_);
                    pipe_ = INVALID_HANDLE_VALUE;
                }
            }
            break;
        }

        if (opcode == OPCODE_PING) {
            // Respond with PONG (same payload)
            std::lock_guard<std::mutex> lock(mutex_);
            SendFrame(OPCODE_FRAME, payload);
        } else if (opcode == OPCODE_CLOSE) {
            // Server is closing the connection
            connected_ = false;
            std::lock_guard<std::mutex> lock(mutex_);
            if (pipe_ != INVALID_HANDLE_VALUE) {
                CloseHandle(pipe_);
                pipe_ = INVALID_HANDLE_VALUE;
            }
            break;
        }
        // OPCODE_FRAME responses are received here — we don't need to
        // process them since SET_ACTIVITY is fire-and-forget.
    }
}

// ---------------------------------------------------------------------------
// Frame I/O
// ---------------------------------------------------------------------------

bool DiscordRpcClient::SendFrame(uint32_t opcode, const std::string& payload) {
    if (pipe_ == INVALID_HANDLE_VALUE) return false;

    // Guard against integer truncation on 64-bit platforms
    if (payload.size() > UINT32_MAX) return false;
    uint32_t len = static_cast<uint32_t>(payload.size());

    // Write opcode (4 bytes, little-endian)
    DWORD written = 0;
    if (!WriteFile(pipe_, &opcode, 4, &written, nullptr) || written != 4) {
        return false;
    }

    // Write length (4 bytes, little-endian)
    if (!WriteFile(pipe_, &len, 4, &written, nullptr) || written != 4) {
        return false;
    }

    // Write payload
    if (len > 0) {
        if (!WriteFile(pipe_, payload.data(), len, &written, nullptr) || written != len) {
            return false;
        }
    }

    FlushFileBuffers(pipe_);
    return true;
}

bool DiscordRpcClient::ReadFrame(uint32_t& opcode, std::string& payload, DWORD timeout_ms) {
    if (pipe_ == INVALID_HANDLE_VALUE) return false;

    // Use overlapped I/O for timeout support
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) return false;

    auto cleanup = [&overlapped]() { CloseHandle(overlapped.hEvent); };

    // Read opcode (4 bytes)
    DWORD bytes_read = 0;
    uint8_t header[8] = {};

    // Read the 8-byte header (opcode + length) with timeout
    BOOL ok = ReadFile(pipe_, header, 8, &bytes_read, &overlapped);
    if (ok) {
        // Synchronous completion — validate bytes read
        if (bytes_read != 8) {
            cleanup();
            return false;
        }
    } else if (GetLastError() != ERROR_IO_PENDING) {
        cleanup();
        return false;
    }

    if (!ok) {
        // Wait for the read to complete with timeout
        DWORD wait = WaitForSingleObject(overlapped.hEvent, timeout_ms);
        if (wait != WAIT_OBJECT_0) {
            CancelIo(pipe_);
            cleanup();
            return false;
        }
        if (!GetOverlappedResult(pipe_, &overlapped, &bytes_read, FALSE) || bytes_read != 8) {
            cleanup();
            return false;
        }
    }

    // Parse header
    memcpy(&opcode, header, 4);
    uint32_t length = 0;
    memcpy(&length, header + 4, 4);

    if (length > 1024 * 1024) {
        // Sanity check: reject messages > 1MB
        cleanup();
        return false;
    }

    // Read payload
    payload.resize(length);
    if (length > 0) {
        ResetEvent(overlapped.hEvent);
        bytes_read = 0;
        ok = ReadFile(pipe_, payload.data(), length, &bytes_read, &overlapped);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            cleanup();
            return false;
        }

        if (!ok) {
            DWORD wait = WaitForSingleObject(overlapped.hEvent, timeout_ms);
            if (wait != WAIT_OBJECT_0) {
                CancelIo(pipe_);
                cleanup();
                return false;
            }
            if (!GetOverlappedResult(pipe_, &overlapped, &bytes_read, FALSE) ||
                bytes_read != length) {
                cleanup();
                return false;
            }
        }
    }

    cleanup();
    return true;
}

// ---------------------------------------------------------------------------
// Reconnection
// ---------------------------------------------------------------------------

void DiscordRpcClient::TryReconnect() {
    DWORD now = GetTickCount();
    DWORD last = last_reconnect_attempt_.load();

    // Throttle reconnection attempts
    if (now - last < RECONNECT_INTERVAL_MS) return;
    last_reconnect_attempt_ = now;

    std::lock_guard<std::mutex> lock(mutex_);

    // Clean up old pipe
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }

    if (!ConnectToPipe()) return;
    if (!Handshake()) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return;
    }

    connected_ = true;
}