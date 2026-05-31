#pragma once

#include <unordered_set>
#include <string>

class ProcessMonitor {
public:
    // Returns all running process executable names (lowercase).
    std::unordered_set<std::string> GetRunningProcessNames() const;

    // Checks if any process from the provided set is running.
    // Returns the first match (lowercase) or empty string if none.
    std::string FindMatchingProcess(const std::unordered_set<std::string>& targets) const;

    // Returns the lowercase exe name of the process owning the
    // foreground (focused) window, or "" if unavailable.
    std::string GetForegroundProcessName() const;
};