#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

// Rich Presence activity data sent to Discord via IPC.
struct RpcActivity {
    std::string details;          // First line of presence (max 128 chars)
    std::string state;            // Second line of presence (max 128 chars)
    int64_t start_timestamp = 0;  // Unix epoch ms (0 = not set)
    int64_t end_timestamp = 0;    // Unix epoch ms (0 = not set)
    std::string large_image_key;  // Key of uploaded asset or URL
    std::string large_image_text; // Hover text for large image
    std::string small_image_key;  // Key of uploaded small asset or URL
    std::string small_image_text; // Hover text for small image
};

// Discord Rich Presence client using the local IPC protocol.
// Connects to Discord's named pipe, performs handshake, and sends
// SET_ACTIVITY commands to update the user's Rich Presence.
//
// Thread-safe: public methods are guarded by a mutex.
// Runs a background reader thread for incoming responses and PING keepalive.
// Automatically reconnects if the pipe is closed (e.g., Discord restarts).
class DiscordRpcClient {
public:
    explicit DiscordRpcClient(const std::string& application_id);
    ~DiscordRpcClient();

    DiscordRpcClient(const DiscordRpcClient&) = delete;
    DiscordRpcClient& operator=(const DiscordRpcClient&) = delete;

    // Connect to Discord IPC pipe, perform handshake, start reader thread.
    // Returns true if connection and handshake succeeded.
    bool Start();

    // Disconnect and clean up. Sends CLEAR_ACTIVITY if connected.
    void Stop();

    // Whether the client is connected and handshaked.
    bool IsConnected() const;

    // Update Rich Presence with the given activity.
    void SetActivity(const RpcActivity& activity);

    // Clear Rich Presence (shows no activity).
    void ClearActivity();

    // Attempt to reconnect if disconnected (throttled).
    void TryReconnect();

private:
    // Try connecting to \\.\pipe\discord-ipc-0 through -9.
    bool ConnectToPipe();

    // Send handshake frame and wait for READY response.
    bool Handshake();

    // Background thread: reads frames, handles PING/PONG and errors.
    void ReaderThread();

    // Send a frame (opcode + JSON payload) over the pipe.
    bool SendFrame(uint32_t opcode, const std::string& payload);

    // Read a frame from the pipe. Returns false on error/disconnect.
    bool ReadFrame(uint32_t& opcode, std::string& payload, DWORD timeout_ms = 5000);

    std::string application_id_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::thread reader_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    mutable std::mutex mutex_;
    DWORD pid_;  // Process ID for SET_ACTIVITY

    // Time of last reconnect attempt (for throttling)
    std::atomic<DWORD> last_reconnect_attempt_{0};

    // Opcode constants for the Discord IPC protocol
    static constexpr uint32_t OPCODE_HANDSHAKE = 0;
    static constexpr uint32_t OPCODE_FRAME      = 1;
    static constexpr uint32_t OPCODE_CLOSE      = 2;
    static constexpr uint32_t OPCODE_PING       = 3;
    static constexpr uint32_t OPCODE_PONG       = 4;

    // Reconnect interval in milliseconds
    static constexpr DWORD RECONNECT_INTERVAL_MS = 15000;
};