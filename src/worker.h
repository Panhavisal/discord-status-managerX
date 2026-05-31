#pragma once

#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include "config.h"
#include "discord_api.h"
#include "process_monitor.h"
#include "token_extractor.h"

class Worker {
public:
    explicit Worker(const AppConfig& config);
    ~Worker();

    void Start();
    void Stop();

    // Thread-safe: returns current status for tray tooltip
    std::string GetStatusText() const;

    // Set/renew the Discord token (e.g., after re-extraction)
    void SetToken(const std::string& token);

    // Clear the token and mark as logged out
    void ClearToken();

private:
    void RunLoop();
    bool EnsureTokenValid();
    std::pair<ActivityConfig, std::string> DetermineActivity();
    void UpdatePresence(const ActivityConfig& cfg, const std::string& process_name);
    void UpdateStatus(const std::string& text);
    void InterruptibleSleep(DWORD ms);

    AppConfig       config_;
    ProcessMonitor  monitor_;
    DiscordApi      api_;

    std::thread     worker_thread_;
    std::atomic<bool> stop_requested_{false};

    // Current presence state (to avoid redundant updates)
    std::string     last_sent_process_; // empty = nothing sent, "idle" = default sent
    DWORD           last_update_time_ = 0;

    mutable std::mutex status_mutex_;
    std::string     status_text_ = "Starting...";

    // Token state
    bool            has_valid_token_ = false;
};