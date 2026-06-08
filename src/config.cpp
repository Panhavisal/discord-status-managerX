#include "config.h"

#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <fstream>

using json = nlohmann::json;

std::string Config::ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Config::GetDefaultConfigPath() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string spath(path);
    auto pos = spath.find_last_of("\\/");
    if (pos != std::string::npos) {
        spath = spath.substr(0, pos + 1);
    }
    spath += "config.json";
    return spath;
}

ActivityConfig Config::ParseActivity(const json& obj) {
    ActivityConfig cfg;
    cfg.text            = obj.value("text", "");
    cfg.emoji_name      = obj.value("emoji_name", "");
    cfg.rp_details      = obj.value("rp_details", "");
    cfg.rp_state        = obj.value("rp_state", "");
    cfg.rp_large_image  = obj.value("rp_large_image", "");
    cfg.rp_large_text   = obj.value("rp_large_text", "");
    cfg.rp_small_image  = obj.value("rp_small_image", "");
    cfg.rp_small_text   = obj.value("rp_small_text", "");
    return cfg;
}

void Config::WriteDefaultConfig(const std::string& path) {
    json default_cfg = {
        {"application_id", ""},
        {"enable_custom_status", true},
        {"enable_rich_presence", true},
        {"poll_interval_ms", 5000},
        {"update_interval_s", 30},
        {"activities", {
            {"code.exe",                {{"text", "Coding in VS Code"},           {"emoji_name", ""}}},
            {"cursor.exe",             {{"text", "Coding in Cursor"},            {"emoji_name", ""}}},
            {"windsurf.exe",           {{"text", "Coding in Windsurf"},          {"emoji_name", ""}}},
            {"devenv.exe",             {{"text", "Coding in Visual Studio"},     {"emoji_name", ""}}},
            {"idea64.exe",             {{"text", "Coding in IntelliJ IDEA"},     {"emoji_name", ""}}},
            {"webstorm64.exe",         {{"text", "Coding in WebStorm"},          {"emoji_name", ""}}},
            {"pycharm64.exe",          {{"text", "Coding in PyCharm"},           {"emoji_name", ""}}},
            {"rider64.exe",            {{"text", "Coding in Rider"},             {"emoji_name", ""}}},
            {"clion64.exe",            {{"text", "Coding in CLion"},             {"emoji_name", ""}}},
            {"goland64.exe",           {{"text", "Coding in GoLand"},            {"emoji_name", ""}}},
            {"sublime_text.exe",       {{"text", "Coding in Sublime Text"},      {"emoji_name", ""}}},
            {"notepad++.exe",          {{"text", "Editing in Notepad++"},        {"emoji_name", ""}}},
            {"nvim.exe",               {{"text", "Coding in Neovim"},            {"emoji_name", ""}}},
            {"zed.exe",                {{"text", "Coding in Zed"},               {"emoji_name", ""}}},
            {"chrome.exe",             {{"text", "Browsing the web"},            {"emoji_name", ""}}},
            {"msedge.exe",             {{"text", "Browsing the web"},            {"emoji_name", ""}}},
            {"firefox.exe",            {{"text", "Browsing the web"},            {"emoji_name", ""}}},
            {"brave.exe",              {{"text", "Browsing the web"},            {"emoji_name", ""}}},
            {"opera.exe",              {{"text", "Browsing the web"},            {"emoji_name", ""}}},
            {"arc.exe",                {{"text", "Browsing in Arc"},             {"emoji_name", ""}}},
            {"spotify.exe",            {{"text", "Listening to music"},          {"emoji_name", ""}}},
            {"discord.exe",            {{"text", "On Discord"},                  {"emoji_name", ""}}},
            {"slack.exe",              {{"text", "On Slack"},                    {"emoji_name", ""}}},
            {"teams.exe",              {{"text", "In a meeting"},               {"emoji_name", ""}}},
            {"zoom.exe",               {{"text", "In a meeting"},               {"emoji_name", ""}}},
            {"steam.exe",              {{"text", "Gaming on Steam"},            {"emoji_name", ""}}},
            {"epicgameslauncher.exe",  {{"text", "Gaming on Epic"},             {"emoji_name", ""}}},
            {"battlenet.exe",          {{"text", "Gaming on Battle.net"},        {"emoji_name", ""}}},
            {"leagueclient.exe",       {{"text", "Playing League of Legends"},   {"emoji_name", ""}}},
            {"valorant.exe",           {{"text", "Playing Valorant"},            {"emoji_name", ""}}},
            {"fortnite.exe",          {{"text", "Playing Fortnite"},            {"emoji_name", ""}}},
            {"minecraft.exe",          {{"text", "Playing Minecraft"},          {"emoji_name", ""}}},
            {"javaw.exe",              {{"text", "Playing Minecraft"},          {"emoji_name", ""}}},
            {"obs64.exe",              {{"text", "Streaming on OBS"},            {"emoji_name", ""}}},
            {"figma.exe",              {{"text", "Designing in Figma"},          {"emoji_name", ""}}},
            {"photoshop.exe",          {{"text", "Editing in Photoshop"},        {"emoji_name", ""}}},
            {"blender.exe",            {{"text", "3D modeling in Blender"},      {"emoji_name", ""}}},
            {"unity.exe",              {{"text", "Developing in Unity"},         {"emoji_name", ""}}},
            {"unrealeditor.exe",       {{"text", "Developing in Unreal"},        {"emoji_name", ""}}},
            {"windowsterminal.exe",    {{"text", "Working in terminal"},         {"emoji_name", ""}}},
            {"pwsh.exe",               {{"text", "Working in PowerShell"},       {"emoji_name", ""}}},
            {"docker desktop.exe",     {{"text", "Working with containers"},      {"emoji_name", ""}}},
            {"postman.exe",            {{"text", "Testing APIs"},                {"emoji_name", ""}}},
            {"fork.exe",               {{"text", "Using Git"},                   {"emoji_name", ""}}}
        }},
        {"default_activity", {
            {"text", "Staying low..."},
            {"emoji_name", "\xf0\x9f\xab\xa5"}  // 🫥 (UTF-8 encoded)
        }}
    };

    std::ofstream file(path);
    if (file.is_open()) {
        file << default_cfg.dump(2);
        file.close();
        if (!file.good()) {
            OutputDebugStringA("Config: Failed to write default config file\n");
        }
    }
}

AppConfig Config::Load(const std::string& config_path) {
    AppConfig cfg;

    std::ifstream file(config_path);
    if (!file.is_open()) {
        WriteDefaultConfig(config_path);
        cfg.default_activity = {"Staying low...", "\xf0\x9f\xab\xa5"};
        return cfg;
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& e) {
        OutputDebugStringA("Config: Failed to parse config file: ");
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
        cfg.default_activity = {"Staying low...", "\xf0\x9f\xab\xa5"};
        return cfg;
    }

    cfg.poll_interval_ms = root.value("poll_interval_ms", 5000u);
    cfg.update_interval_s = root.value("update_interval_s", 30u);
    cfg.application_id = root.value("application_id", "");
    cfg.enable_custom_status = root.value("enable_custom_status", true);
    cfg.enable_rich_presence = root.value("enable_rich_presence", true);

    // Enforce minimum and maximum values to prevent tight loops, API spam, or overflow
    if (cfg.poll_interval_ms < 1000) {
        cfg.poll_interval_ms = 1000;
    }
    if (cfg.poll_interval_ms > 60000) {
        cfg.poll_interval_ms = 60000;
    }
    if (cfg.update_interval_s < 5) {
        cfg.update_interval_s = 5;
    }
    if (cfg.update_interval_s > 3600) {
        cfg.update_interval_s = 3600;
    }

    if (root.contains("activities") && root["activities"].is_object()) {
        for (auto& [key, val] : root["activities"].items()) {
            std::string proc = ToLower(key);
            cfg.activities[proc] = ParseActivity(val);
        }
    }

    if (root.contains("default_activity") && root["default_activity"].is_object()) {
        cfg.default_activity = ParseActivity(root["default_activity"]);
    } else {
        cfg.default_activity = {"Staying low...", "\xf0\x9f\xab\xa5"};
    }

    return cfg;
}