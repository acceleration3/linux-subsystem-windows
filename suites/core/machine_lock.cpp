#include "core/machine_lock.hpp"

#include <windows.h>

namespace lsw::suites {

MachineLock::MachineLock() {
    handle_ = ::CreateMutexW(nullptr, FALSE, L"Global\\lsw-suites-exclusive-check");
    if (handle_ == nullptr) {
        handle_ = ::CreateMutexW(nullptr, FALSE, L"lsw-suites-exclusive-check");
    }
}

MachineLock::~MachineLock() {
    if (handle_ != nullptr) {
        ::CloseHandle(handle_);
    }
}

void MachineLock::lock() {
    if (handle_ != nullptr) {
        ::WaitForSingleObject(handle_, INFINITE);
    }
}

void MachineLock::unlock() {
    if (handle_ != nullptr) {
        ::ReleaseMutex(handle_);
    }
}

}  // namespace lsw::suites
