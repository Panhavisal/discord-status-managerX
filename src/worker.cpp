#include "worker.h"

#include <algorithm>

Worker::Worker(const AppConfig& config, WorkerMode mode)
    : config_(config), mode_(mode), api_("") {} // token set later via SetToken

Worker::~Worker() {
    Stop();
}

void Worker::Start() {
    if (worker_thread_.joinable()) return;
    stop_requested_ = false;
    worker_thread_ = std::thread(&Worker::RunLoop, this);
}

void Worker::Stop() {
    stop_requested_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

std::string Worker::GetStatusText() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_text_;
}

void Worker::SetToken(const std::string& token) {
    api_.SetToken(token);
    last_validate_attempt_time_ = GetTickCount();
    has_valid_token_ = api_.ValidateToken();
    if (has_valid_token_) {
        last_sent_process_.clear(); // force re-send
    }
}

void Worker::ClearToken() {
    api_.SetToken("");
    has_valid_token_ = false;
    last_sent_process_.clear();
    UpdateStatus("Logged out");
}

void Worker::SetLogCallback(LogCallback cb) {
    log_callback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------------------

void Worker::RunLoop() {
    UpdateStatus("Starting...");
    if (log_callback_) log_callback_("Worker started");

    while (!stop_requested_.load()) {
        // 1. Ensure we have a valid token
        if (!EnsureTokenValid()) {
            InterruptibleSleep(5000);
            continue;
        }

        // 2. Poll running processes
        auto [activity, process_name] = DetermineActivity();

        // 3. Update presence if changed and enough time has passed
        DWORD now = GetTickCount();
        DWORD min_interval_ms = config_.update_interval_s * 1000;
        bool time_ok = (now - last_update_time_) >= min_interval_ms;

        if (process_name != last_sent_process_ || time_ok) {
            UpdatePresence(activity, process_name);
        }

        // 4. Sleep for poll interval (interruptible)
        InterruptibleSleep(config_.poll_interval_ms);
    }

    // Graceful cleanup: clear custom status on exit
    if (has_valid_token_) {
        api_.ClearCustomStatus();
    }
    UpdateStatus("Stopped");
    if (log_callback_) log_callback_("Worker stopped");
}

// ---------------------------------------------------------------------------
// Token Management
// ---------------------------------------------------------------------------

bool Worker::EnsureTokenValid() {
    if (has_valid_token_) {
        return true;
    }

    // If a token string is set but not yet validated, retry validation periodically.
    // This handles the boot-time case where the network isn't ready yet.
    if (api_.HasToken()) {
        DWORD now = GetTickCount();
        // Retry every 60 seconds (avoid hammering Discord API)
        if (now - last_validate_attempt_time_ >= 60000) {
            last_validate_attempt_time_ = now;
            if (log_callback_) log_callback_("Retrying token validation...");
            has_valid_token_ = api_.ValidateToken();
            if (has_valid_token_) {
                last_sent_process_.clear(); // force re-send
                UpdateStatus("Connected");
                if (log_callback_) log_callback_("Token validated successfully on retry");
                return true;
            }
            if (log_callback_) log_callback_("Token validation retry failed (network may not be ready)");
        }
    }

    UpdateStatus("No valid token");
    if (log_callback_) log_callback_("No valid token - waiting for re-authentication");
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
        // Service mode: scan all running processes (GetForegroundWindow
        // returns NULL in Session 0, so foreground detection won't work).
        // Reports the first matching process as active.
        std::string match = monitor_.FindMatchingProcess(targets);
        if (!match.empty()) {
            return {config_.activities[match], match};
        }
        return {config_.default_activity, "idle"};
    }

    // GUI mode: check the foreground (focused) window's process
    std::string fg = monitor_.GetForegroundProcessName();
    if (!fg.empty() && targets.count(fg)) {
        return {config_.activities[fg], fg};
    }

    // No matching foreground app — show idle/default
    return {config_.default_activity, "idle"};
}

// ---------------------------------------------------------------------------
// Presence Update
// ---------------------------------------------------------------------------

void Worker::UpdatePresence(const ActivityConfig& cfg, const std::string& process_name) {
    CustomStatus status;
    status.text       = cfg.text;
    status.emoji_name = cfg.emoji_name;

    ApiResult result = api_.SetCustomStatus(status);

    if (result == ApiResult::Success) {
        last_sent_process_ = process_name;
        last_update_time_ = GetTickCount();
        if (process_name == "idle") {
            UpdateStatus("Connected - Idle");
        } else {
            UpdateStatus("Connected - " + cfg.text);
        }
        if (log_callback_) log_callback_("Status updated: " + (process_name == "idle" ? std::string("Idle") : cfg.text));
    } else if (result == ApiResult::Unauthorized) {
        has_valid_token_ = false;
        UpdateStatus("Token expired");
        if (log_callback_) log_callback_("Token expired or unauthorized");
    } else if (result == ApiResult::RateLimited) {
        UpdateStatus("Rate limited");
        if (log_callback_) log_callback_("Rate limited by Discord API");
    } else {
        UpdateStatus("API error");
        if (log_callback_) log_callback_("Discord API error");
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
    DWORD elapsed = 0;
    while (elapsed < ms && !stop_requested_.load()) {
        Sleep(100);
        elapsed += 100;
    }
}