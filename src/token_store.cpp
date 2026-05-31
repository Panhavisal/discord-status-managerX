#include "token_store.h"

#include <windows.h>
#include <dpapi.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <cstdint>

namespace TokenStore {

// Magic prefix written at the start of encrypted token files.
// Legacy (plain-text) files do NOT have this prefix, allowing
// backward-compatible migration on first load.
static const char kEncryptedPrefix[] = "DPAPI";
static const size_t kEncryptedPrefixLen = 5;

// ---------------------------------------------------------------------------
// DPAPI helpers
// ---------------------------------------------------------------------------

// Encrypt a plaintext string using DPAPI.
// Always uses CRYPTPROTECT_LOCAL_MACHINE so the encrypted token can be
// read by both the GUI (user session) and the service (LocalSystem account).
// This means any local process can decrypt the token, but it cannot be
// decrypted on a different machine — still much better than plain text.
static std::vector<uint8_t> EncryptData(const std::string& plaintext) {
    DATA_BLOB data_in = {};
    data_in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    data_in.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB data_out = {};
    DWORD flags = CRYPTPROTECT_UI_FORBIDDEN | CRYPTPROTECT_LOCAL_MACHINE;

    if (!CryptProtectData(&data_in, nullptr, nullptr, nullptr, nullptr, flags, &data_out)) {
        return {};
    }

    std::vector<uint8_t> result(data_out.pbData, data_out.pbData + data_out.cbData);
    LocalFree(data_out.pbData);
    return result;
}

// Decrypt a DPAPI-encrypted blob back to plaintext.
// Returns empty string on failure.
static std::string DecryptData(const std::vector<uint8_t>& encrypted) {
    DATA_BLOB data_in = {};
    data_in.pbData = const_cast<BYTE*>(encrypted.data());
    data_in.cbData = static_cast<DWORD>(encrypted.size());

    DATA_BLOB data_out = {};
    DWORD flags = CRYPTPROTECT_UI_FORBIDDEN;

    if (!CryptUnprotectData(&data_in, nullptr, nullptr, nullptr, nullptr, flags, &data_out)) {
        return "";
    }

    std::string result(reinterpret_cast<char*>(data_out.pbData), data_out.cbData);
    LocalFree(data_out.pbData);
    return result;
}

// ---------------------------------------------------------------------------
// File path
// ---------------------------------------------------------------------------

std::string GetFilePath() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string spath(path);
    auto pos = spath.find_last_of("\\/");
    if (pos != std::string::npos) {
        spath = spath.substr(0, pos + 1);
    }
    spath += "token.dat";
    return spath;
}

// ---------------------------------------------------------------------------
// Load — handles both encrypted and legacy plain-text formats
// ---------------------------------------------------------------------------

std::string Load() {
    std::string path = GetFilePath();

    // Open as binary to detect the encrypted prefix correctly
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";

    // Read the entire file
    std::string contents((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    file.close();

    if (contents.empty()) return "";

    // Check for encrypted format: starts with "DPAPI" magic prefix
    if (contents.size() >= kEncryptedPrefixLen &&
        memcmp(contents.data(), kEncryptedPrefix, kEncryptedPrefixLen) == 0) {

        // Encrypted format: DPAPI<4-byte-len><encrypted-blob>
        if (contents.size() < kEncryptedPrefixLen + 4) return "";

        // Read the 4-byte little-endian length
        uint32_t blob_len = 0;
        memcpy(&blob_len, contents.data() + kEncryptedPrefixLen, 4);

        size_t expected_size = kEncryptedPrefixLen + 4 + blob_len;
        if (contents.size() < expected_size) return "";

        // Extract the encrypted blob
        std::vector<uint8_t> encrypted_blob(
            reinterpret_cast<const uint8_t*>(contents.data() + kEncryptedPrefixLen + 4),
            reinterpret_cast<const uint8_t*>(contents.data() + kEncryptedPrefixLen + 4) + blob_len
        );

        // Decrypt
        std::string token = DecryptData(encrypted_blob);

        // Trim whitespace (DPAPI preserves exact bytes, but just in case)
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t' ||
                                   token.front() == '\n' || token.front() == '\r'))
            token.erase(token.begin());
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t' ||
                                   token.back() == '\n' || token.back() == '\r'))
            token.pop_back();

        return token;
    }

    // Legacy plain-text format — read as text and trim
    std::string token = contents;

    // Trim whitespace
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t' ||
                               token.front() == '\n' || token.front() == '\r'))
        token.erase(token.begin());
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t' ||
                               token.back() == '\n' || token.back() == '\r'))
        token.pop_back();

    return token;
}

// ---------------------------------------------------------------------------
// Save — encrypts with DPAPI before writing
// ---------------------------------------------------------------------------

void Save(const std::string& token, bool /*machine_scope*/) {
    // Encrypt the token (always machine-scope for cross-account access)
    std::vector<uint8_t> encrypted = EncryptData(token);
    if (encrypted.empty()) {
        // Encryption failed — fall back to plain text (shouldn't happen normally)
        std::string path = GetFilePath();
        std::ofstream file(path, std::ios::trunc);
        if (file.is_open()) {
            file << token;
        }
        return;
    }

    // Write: "DPAPI" + 4-byte LE length + encrypted blob
    std::string path = GetFilePath();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;

    // Magic prefix
    file.write(kEncryptedPrefix, kEncryptedPrefixLen);

    // 4-byte little-endian length
    uint32_t blob_len = static_cast<uint32_t>(encrypted.size());
    file.write(reinterpret_cast<const char*>(&blob_len), 4);

    // Encrypted blob
    file.write(reinterpret_cast<const char*>(encrypted.data()),
               static_cast<std::streamsize>(encrypted.size()));
}

// ---------------------------------------------------------------------------
// Clear — delete the token file
// ---------------------------------------------------------------------------

void Clear() {
    std::string path = GetFilePath();
    std::filesystem::remove(path);
}

} // namespace TokenStore