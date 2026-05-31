#include "discord_api.h"

#include <windows.h>
#include <wininet.h>
#include <nlohmann/json.hpp>

#pragma comment(lib, "wininet.lib")

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DiscordApi::DiscordApi(const std::string& token)
    : token_(token) {}

DiscordApi::~DiscordApi() = default;

void DiscordApi::SetToken(const std::string& token) {
    token_ = token;
}

// ---------------------------------------------------------------------------
// Validate token
// ---------------------------------------------------------------------------

bool DiscordApi::ValidateToken() {
    HINTERNET hInternet = InternetOpenA("DiscordPresenceUpdater/1.0",
                                          INTERNET_OPEN_TYPE_DIRECT,
                                          nullptr, nullptr, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetConnectA(hInternet, "discord.com",
                                            INTERNET_DEFAULT_HTTPS_PORT,
                                            nullptr, nullptr,
                                            INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return false;
    }

    const char* accept_types[] = {"application/json", nullptr};
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "GET",
                                            "/api/v9/users/@me",
                                            nullptr, nullptr,
                                            accept_types,
                                            INTERNET_FLAG_SECURE |
                                            INTERNET_FLAG_NO_CACHE_WRITE |
                                            INTERNET_FLAG_RELOAD,
                                            0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return false;
    }

    std::string auth_header = "Authorization: " + token_;
    HttpSendRequestA(hRequest, auth_header.c_str(),
                      static_cast<DWORD>(auth_header.length()), nullptr, 0);

    DWORD status_code = 0;
    DWORD status_len = sizeof(status_code);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                    &status_code, &status_len, nullptr);

    bool valid = (status_code == 200);
    if (valid) {
        std::string body;
        char buf[4096];
        DWORD read = 0;
        while (InternetReadFile(hRequest, buf, sizeof(buf), &read) && read > 0) {
            body.append(buf, read);
        }
        auto resp = json::parse(body, nullptr, false);
        if (!resp.is_discarded()) {
            std::string user = resp.value("username", "");
            std::string disc = resp.value("discriminator", "");
            username_ = disc == "0" ? user : user + "#" + disc;
        }
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return valid;
}

// ---------------------------------------------------------------------------
// Internal PATCH helper
// ---------------------------------------------------------------------------

ApiResult DiscordApi::PatchSettings(const std::string& json_body) {
    HINTERNET hInternet = InternetOpenA("DiscordPresenceUpdater/1.0",
                                          INTERNET_OPEN_TYPE_DIRECT,
                                          nullptr, nullptr, 0);
    if (!hInternet) return ApiResult::Error;

    HINTERNET hConnect = InternetConnectA(hInternet, "discord.com",
                                            INTERNET_DEFAULT_HTTPS_PORT,
                                            nullptr, nullptr,
                                            INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return ApiResult::Error;
    }

    const char* accept_types[] = {"application/json", nullptr};
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "PATCH",
                                            "/api/v9/users/@me/settings",
                                            nullptr, nullptr,
                                            accept_types,
                                            INTERNET_FLAG_SECURE |
                                            INTERNET_FLAG_NO_CACHE_WRITE |
                                            INTERNET_FLAG_RELOAD,
                                            0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return ApiResult::Error;
    }

    // Send headers
    std::string headers = "Authorization: " + token_ + "\r\n"
                          "Content-Type: application/json\r\n";
    HttpSendRequestA(hRequest, headers.c_str(),
                      static_cast<DWORD>(headers.length()),
                      const_cast<char*>(json_body.c_str()),
                      static_cast<DWORD>(json_body.length()));

    DWORD status_code = 0;
    DWORD status_len = sizeof(status_code);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                    &status_code, &status_len, nullptr);

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    switch (status_code) {
    case 200:   return ApiResult::Success;
    case 401:   return ApiResult::Unauthorized;
    case 429:   return ApiResult::RateLimited;
    default:    return ApiResult::Error;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ApiResult DiscordApi::SetCustomStatus(const CustomStatus& status) {
    json payload = {
        {"custom_status", {
            {"text", status.text},
            {"emoji_name", status.emoji_name.empty() ? json(nullptr) : status.emoji_name},
            {"emoji_id", nullptr},
            {"expires_at", nullptr}
        }}
    };
    return PatchSettings(payload.dump());
}

ApiResult DiscordApi::ClearCustomStatus() {
    json payload = {{"custom_status", nullptr}};
    return PatchSettings(payload.dump());
}