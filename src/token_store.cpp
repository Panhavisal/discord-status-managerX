#include "token_store.h"

#include <windows.h>
#include <fstream>
#include <filesystem>

namespace TokenStore {

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

std::string Load() {
    std::string path = GetFilePath();
    std::ifstream file(path);
    if (!file.is_open()) return "";

    std::string token;
    std::getline(file, token);

    // Trim whitespace
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t' ||
                               token.front() == '\n' || token.front() == '\r'))
        token.erase(token.begin());
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t' ||
                               token.back() == '\n' || token.back() == '\r'))
        token.pop_back();

    return token;
}

void Save(const std::string& token) {
    std::string path = GetFilePath();
    std::ofstream file(path, std::ios::trunc);
    if (file.is_open()) {
        file << token;
    }
}

void Clear() {
    std::string path = GetFilePath();
    std::filesystem::remove(path);
}

} // namespace TokenStore