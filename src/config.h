#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <cstdint>

struct ActivityConfig {
    // Custom Status fields (existing)
    std::string text;
    std::string emoji_name;

    // Rich Presence fields (new)
    std::string rp_details;       // First line of Rich Presence (falls back to text)
    std::string rp_state;         // Second line of Rich Presence
    std::string rp_large_image;   // Large image key or URL
    std::string rp_large_text;    // Hover text for large image
    std::string rp_small_image;   // Small image key or URL
    std::string rp_small_text;    // Hover text for small image
};

struct AppConfig {
    uint32_t    poll_interval_ms = 5000;
    uint32_t    update_interval_s = 30;
    std::string application_id;          // Discord Application ID for Rich Presence
    bool        enable_custom_status = true;   // Update Discord custom status via HTTP
    bool        enable_rich_presence = true;   // Update Rich Presence via IPC
    std::map<std::string, ActivityConfig> activities; // key = lowercase process name
    ActivityConfig default_activity;
};

class Config {
public:
    static AppConfig Load(const std::string& config_path);
    static std::string GetDefaultConfigPath();

private:
    static ActivityConfig ParseActivity(const nlohmann::json& obj);
    static void WriteDefaultConfig(const std::string& path);
    static std::string ToLower(std::string s);
};