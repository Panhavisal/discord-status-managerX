#include "process_monitor.h"

#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::unordered_set<std::string> ProcessMonitor::GetRunningProcessNames() const {
    std::unordered_set<std::string> names;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return names;
    }

    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe)) {
        do {
            names.insert(ToLower(pe.szExeFile));
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return names;
}

std::string ProcessMonitor::FindMatchingProcess(const std::unordered_set<std::string>& targets) const {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return "";
    }

    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);

    std::string result;
    if (Process32First(snap, &pe)) {
        do {
            std::string name = ToLower(pe.szExeFile);
            if (targets.count(name)) {
                result = name;
                break;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return result;
}