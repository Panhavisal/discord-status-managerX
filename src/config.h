#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <cstdint>

struct ActivityConfig {
    std::string text;
    std::string emoji_name;
};

struct AppConfig {
    uint32_t    poll_interval_ms = 5000;
    uint32_t    update_interval_s = 30;
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