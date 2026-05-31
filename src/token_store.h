#pragma once

#include <string>

// Token persistence: save/load Discord token to a file next to the exe.
namespace TokenStore {

// Returns the full path to token.dat (next to the executable).
std::string GetFilePath();

// Load the saved token. Returns empty string if not found.
std::string Load();

// Save a token to token.dat (overwrites if exists).
void Save(const std::string& token);

// Delete the saved token file.
void Clear();

} // namespace TokenStore