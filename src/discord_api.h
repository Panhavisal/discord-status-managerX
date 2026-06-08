#pragma once

#include <string>
#include <cstdint>
#include <mutex>

struct CustomStatus {
    std::string text;
    std::string emoji_name;  // Unicode emoji character
};

enum class ApiResult {
    Success,
    Unauthorized,  // 401 — token expired/invalid
    RateLimited,    // 429
    Error,          // network error, other HTTP error
};

class DiscordApi {
public:
    explicit DiscordApi(const std::string& token);
    ~DiscordApi();

    DiscordApi(const DiscordApi&) = delete;
    DiscordApi& operator=(const DiscordApi&) = delete;

    // Set custom status on the user's account.
    ApiResult SetCustomStatus(const CustomStatus& status);

    // Clear custom status.
    ApiResult ClearCustomStatus();

    // Validate current token (calls GET /users/@me).
    // Returns true if token is valid.
    bool ValidateToken();

    // Get the username associated with the token (filled after ValidateToken).
    std::string GetUsername() const;

    // Update the token (e.g., after re-extraction).
    void SetToken(const std::string& token);

    // Check if a token string has been set (regardless of validity).
    bool HasToken() const;

private:
    // Internal HTTP PATCH helper
    ApiResult PatchSettings(const std::string& json_body);

    mutable std::mutex mutex_;
    std::string token_;
    std::string username_;
};