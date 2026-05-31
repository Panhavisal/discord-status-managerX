#pragma once

#include <string>

// Token persistence: save/load Discord token to a file next to the exe.
// Tokens are encrypted at rest using DPAPI (Windows Data Protection API).
// The file format uses a "DPAPI" magic prefix to distinguish encrypted
// files from legacy plain-text files (backward compatible).
namespace TokenStore {

// Returns the full path to token.dat (next to the executable).
std::string GetFilePath();

// Load the saved token. Handles both encrypted (DPAPI) and legacy
// plain-text formats. Returns empty string if not found or decryption fails.
std::string Load();

// Save a token to token.dat (encrypted with DPAPI, overwrites if exists).
// Use machine-level encryption if 'machine_scope' is true (for service mode).
void Save(const std::string& token, bool machine_scope = false);

// Delete the saved token file.
void Clear();

} // namespace TokenStore