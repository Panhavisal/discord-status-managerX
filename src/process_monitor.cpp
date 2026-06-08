#include "process_monitor.h"

#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Convert wide string (from PROCESSENTRY32W) to UTF-8 lowercase
static std::string WideToUtf8Lower(const wchar_t* wide) {
    if (!wide || !wide[0]) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');  // len includes null terminator
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], len, nullptr, nullptr);
    return ToLower(result);
}

std::unordered_set<std::string> ProcessMonitor::GetRunningProcessNames() const {
    std::unordered_set<std::string> names;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return names;
    }

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            names.insert(WideToUtf8Lower(pe.szExeFile));
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return names;
}

std::string ProcessMonitor::FindMatchingProcess(const std::unordered_set<std::string>& targets) const {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return "";
    }

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    std::string result;
    if (Process32FirstW(snap, &pe)) {
        do {
            std::string name = WideToUtf8Lower(pe.szExeFile);
            if (targets.count(name)) {
                result = name;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return result;
}

std::string ProcessMonitor::GetForegroundProcessName() const {
    // Get the window the user is currently focused on
    HWND fg = GetForegroundWindow();
    if (!fg) return "";

    // Get the process ID that owns the foreground window
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == 0) return "";

    // Look up the exe name for that PID
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return "";
    }

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    std::string result;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                result = WideToUtf8Lower(pe.szExeFile);
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return result;
}