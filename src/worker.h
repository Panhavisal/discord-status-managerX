#pragma once

#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <functional>
#include "config.h"
#include "discord_api.h"
#include "discord_rpc.h"
#include "process_monitor.h"
#include "token_extractor.h"

// WorkerMode controls how process detection works:
// - GUI:     Uses GetForegroundProcessName (focused window only)
// - Service: Uses FindMatchingProcess (any running process, for Session 0)
enum class WorkerMode {
    GUI,
    Service
};

// Callback for logging status messages (used by service mode)
using LogCallback = std::function<void(const std::string&)>;

class Worker {
public:
    explicit Worker(const AppConfig& config, WorkerMode mode = WorkerMode::GUI);
    ~Worker();

    void Start();
    void Stop();

    // Thread-safe: returns current status for tray tooltip
    std::string GetStatusText() const;

    // Set/renew the Discord token (e.g., after re-extraction)
    void SetToken(const std::string& token);

    // Clear the token and mark as logged out
    void ClearToken();

    // Set a callback for logging status messages (used by service mode)
    void SetLogCallback(LogCallback cb);

private:
    void RunLoop();
    bool EnsureTokenValid();
    std::pair<ActivityConfig, std::string> DetermineActivity();
    void UpdatePresence(const ActivityConfig& cfg, const std::string& process_name);
    void UpdateStatus(const std::string& text);
    void InterruptibleSleep(DWORD ms);

    AppConfig       config_;
    WorkerMode      mode_ = WorkerMode::GUI;
    ProcessMonitor  monitor_;
    DiscordApi      api_;
    DiscordRpcClient rpc_;

    std::mutex      callback_mutex_;
    LogCallback     log_callback_;

    std::thread     worker_thread_;
    std::atomic<bool> stop_requested_{false};

    // Token/presence state — protected by state_mutex_
    mutable std::mutex state_mutex_;
    bool            has_valid_token_ = false;
    std::string     last_sent_process_; // empty = nothing sent, "idle" = default sent
    DWORD           last_update_time_ = 0;
    DWORD           last_validate_attempt_time_ = 0;
    int64_t         activity_start_time_ = 0;  // Unix epoch ms for Rich Presence timestamps

    mutable std::mutex status_mutex_;
    std::string     status_text_ = "Starting...";
};