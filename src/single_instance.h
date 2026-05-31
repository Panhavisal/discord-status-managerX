#pragma once

#include <windows.h>
#include <string>

class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(const std::wstring& mutex_name);
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    bool IsFirstInstance() const { return is_first_; }

private:
    HANDLE h_mutex_ = nullptr;
    bool   is_first_ = false;
};