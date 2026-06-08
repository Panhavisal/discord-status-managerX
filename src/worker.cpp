#include "worker.h"

#include <algorithm>
#include <chrono>

// Helper: get current Unix epoch milliseconds
static int64_t NowEpochMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

Worker::Worker(const AppConfig& config, WorkerMode mode)
    : config_(config), mode_(mode), api_(""), rpc_(config.application_id) {}

Worker::~Worker() {
    Stop();
}

void Worker::Start() {
    if (worker_thread_.joinable()) return;
    stop_requested_ = false;

    // Start Rich Presence if enabled and application_id is configured
    if (config_.enable_rich_presence && !config_.application_id.empty()) {
        if (!rpc_.Start()) {
            UpdateStatus("Rich Presence: connection failed");
        }
    }

    worker_thread_ = std::thread(&Worker::RunLoop, this);
}

void Worker::Stop() {
    stop_requested_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    // Stop Rich Presence
    if (config_.enable_rich_presence && !config_.application_id.empty()) {
        rpc_.Stop();
    }
}

std::string Worker::GetStatusText() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_text_;
}

void Worker::SetToken(const std::string& token) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        api_.SetToken(token);
        last_validate_attempt_time_ = GetTickCount();
        has_valid_token_ = api_.ValidateToken();
        if (has_valid_token_) {
            last_sent_process_.clear(); // force re-send
        }
    }
}

void Worker::ClearToken() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        api_.SetToken("");
        has_valid_token_ = false;
        last_sent_process_.clear();
    }
    UpdateStatus("Logged out");
}

void Worker::SetLogCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    log_callback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------------------

void Worker::RunLoop() {
    UpdateStatus("Starting...");
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (log_callback_) log_callback_("Worker started");
    }

    while (!stop_requested_.load()) {
        // 1. Ensure we have a valid token (for Custom Status)
        if (config_.enable_custom_status) {
            if (!EnsureTokenValid()) {
                InterruptibleSleep(5000);
                continue;
            }
        }

        // 2. Try to reconnect Rich Presence if disconnected
        if (config_.enable_rich_presence && !config_.application_id.empty() && !rpc_.IsConnected()) {
            rpc_.TryReconnect();
        }

        // 3. Poll running processes
        auto [activity, process_name] = DetermineActivity();

        // 4. Update presence if changed and enough time has passed
        DWORD now = GetTickCount();
        DWORD min_interval_ms = config_.update_interval_s * 1000;
        bool time_ok;
        bool process_changed;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            time_ok = (now - last_update_time_) >= min_interval_ms;
            process_changed = (process_name != last_sent_process_);
        }

        if (process_changed || time_ok) {
            UpdatePresence(activity, process_name);
        }

        // 5. Sleep for poll interval (interruptible)
        InterruptibleSleep(config_.poll_interval_ms);
    }

    // Graceful cleanup
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (has_valid_token_ && config_.enable_custom_status) {
            api_.ClearCustomStatus();
        }
    }
    if (config_.enable_rich_presence && !config_.application_id.empty()) {
        rpc_.ClearActivity();
    }
    UpdateStatus("Stopped");
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (log_callback_) log_callback_("Worker stopped");
    }
}

// ---------------------------------------------------------------------------
// Token Management
// ---------------------------------------------------------------------------

bool Worker::EnsureTokenValid() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (has_valid_token_) {
        return true;
    }

    // If a token string is set but not yet validated, retry validation periodically.
    if (api_.HasToken()) {
        DWORD now = GetTickCount();
        // Retry every 60 seconds (avoid hammering Discord API)
        if (now - last_validate_attempt_time_ >= 60000) {
            last_validate_attempt_time_ = now;
            {
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                if (log_callback_) log_callback_("Retrying token validation...");
            }
            has_valid_token_ = api_.ValidateToken();
            if (has_valid_token_) {
                last_sent_process_.clear(); // force re-send
                UpdateStatus("Connected");
                {
                    std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                    if (log_callback_) log_callback_("Token validated successfully on retry");
                }
                return true;
            }
            {
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                if (log_callback_) log_callback_("Token validation retry failed (network may not be ready)");
            }
        }
    }

    UpdateStatus("No valid token");
    {
        std::lock_guard<std::mutex> cb_lock(callback_mutex_);
        if (log_callback_) log_callback_("No valid token - waiting for re-authentication");
    }
    return false;
}

// ---------------------------------------------------------------------------
// Activity Detection
// ---------------------------------------------------------------------------

std::pair<ActivityConfig, std::string> Worker::DetermineActivity() {
    std::unordered_set<std::string> targets;
    for (const auto& [name, _] : config_.activities) {
        targets.insert(name);
    }

    if (mode_ == WorkerMode::Service) {
        std::string match = monitor_.FindMatchingProcess(targets);
        if (!match.empty()) {
            return {config_.activities.at(match), match};
        }
        return {config_.default_activity, "idle"};
    }

    // GUI mode: check the foreground (focused) window's process
    std::string fg = monitor_.GetForegroundProcessName();
    if (!fg.empty() && targets.count(fg)) {
        return {config_.activities.at(fg), fg};
    }

    return {config_.default_activity, "idle"};
}

// ---------------------------------------------------------------------------
// Presence Update
// ---------------------------------------------------------------------------

void Worker::UpdatePresence(const ActivityConfig& cfg, const std::string& process_name) {
    bool process_changed;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        process_changed = (process_name != last_sent_process_);
    }

    // Update Custom Status via HTTP
    if (config_.enable_custom_status) {
        CustomStatus status;
        status.text       = cfg.text;
        status.emoji_name = cfg.emoji_name;

        ApiResult result = api_.SetCustomStatus(status);

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (result == ApiResult::Success) {
                last_sent_process_ = process_name;
                last_update_time_ = GetTickCount();
                if (process_changed) {
                    activity_start_time_ = NowEpochMs();
                }
            } else if (result == ApiResult::Unauthorized) {
                has_valid_token_ = false;
            }
        }

        if (result == ApiResult::Success) {
            if (process_name == "idle") {
                UpdateStatus("Connected - Idle");
            } else {
                UpdateStatus("Connected - " + cfg.text);
            }
            {
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                if (log_callback_) log_callback_("Status updated: " + (process_name == "idle" ? std::string("Idle") : cfg.text));
            }
        } else if (result == ApiResult::Unauthorized) {
            UpdateStatus("Token expired");
            {
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                if (log_callback_) log_callback_("Token expired or unauthorized");
            }
        } else if (result == ApiResult::RateLimited) {
            UpdateStatus("Rate limited");
            {
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                if (log_callback_) log_callback_("Rate limited by Discord API");
            }
        } else {
            UpdateStatus("API error");
            {
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                if (log_callback_) log_callback_("Discord API error");
            }
        }
    } else {
        // Not using Custom Status, just track the activity change
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_sent_process_ = process_name;
        last_update_time_ = GetTickCount();
        if (process_changed) {
            activity_start_time_ = NowEpochMs();
        }
        if (process_name == "idle") {
            UpdateStatus("Idle");
        } else {
            UpdateStatus(cfg.text);
        }
    }

    // Update Rich Presence via IPC
    if (config_.enable_rich_presence && !config_.application_id.empty() && rpc_.IsConnected()) {
        if (process_name == "idle") {
            // Clear Rich Presence when idle
            rpc_.ClearActivity();
        } else {
            RpcActivity rpc;
            // Fallback: use custom status text if rp_details not set
            rpc.details        = cfg.rp_details.empty() ? cfg.text : cfg.rp_details;
            rpc.state          = cfg.rp_state;
            rpc.large_image_key = cfg.rp_large_image;
            rpc.large_image_text = cfg.rp_large_text;
            rpc.small_image_key = cfg.rp_small_image;
            rpc.small_image_text = cfg.rp_small_text;

            // Set start timestamp for elapsed time display
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (activity_start_time_ > 0) {
                    rpc.start_timestamp = activity_start_time_;
                }
            }

            rpc_.SetActivity(rpc);
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void Worker::UpdateStatus(const std::string& text) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_text_ = text;
}

void Worker::InterruptibleSleep(DWORD ms) {
    DWORD start = GetTickCount();
    while (!stop_requested_.load()) {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= ms) break;
        Sleep(100);
    }
}