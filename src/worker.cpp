#include "worker.h"

#include <algorithm>

Worker::Worker(const AppConfig& config)
    : config_(config), api_("") {} // token set later via SetToken

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

// ---------------------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------------------

void Worker::RunLoop() {
    UpdateStatus("Starting...");

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
}

// ---------------------------------------------------------------------------
// Token Management
// ---------------------------------------------------------------------------

bool Worker::EnsureTokenValid() {
    if (has_valid_token_) {
        return true;
    }
    UpdateStatus("No valid token");
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

    std::string match = monitor_.FindMatchingProcess(targets);
    if (!match.empty()) {
        return {config_.activities[match], match};
    }
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
    } else if (result == ApiResult::Unauthorized) {
        has_valid_token_ = false;
        UpdateStatus("Token expired");
    } else if (result == ApiResult::RateLimited) {
        UpdateStatus("Rate limited");
    } else {
        UpdateStatus("API error");
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