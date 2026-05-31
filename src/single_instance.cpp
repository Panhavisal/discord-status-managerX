#include "single_instance.h"

SingleInstanceGuard::SingleInstanceGuard(const std::wstring& mutex_name) {
    h_mutex_ = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (h_mutex_) {
        is_first_ = (GetLastError() != ERROR_ALREADY_EXISTS);
    }
}

SingleInstanceGuard::~SingleInstanceGuard() {
    if (h_mutex_) {
        ReleaseMutex(h_mutex_);
        CloseHandle(h_mutex_);
    }
}