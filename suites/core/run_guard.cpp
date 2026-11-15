#include "core/run_guard.hpp"

#include <windows.h>

namespace lsw::suites {

RunGuard::~RunGuard() {
    if (handle_ != nullptr) {
        ::CloseHandle(handle_);
    }
}

std::wstring RunGuard::marker_path() {
    wchar_t buffer[MAX_PATH];
    DWORD length = ::GetEnvironmentVariableW(L"ProgramData", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        length = ::GetTempPathW(MAX_PATH, buffer);
        if (length == 0 || length >= MAX_PATH) {
            return L"lsw-suites.lock";
        }
        return std::wstring(buffer, length) + L"lsw-suites.lock";
    }
    return std::wstring(buffer, length) + L"\\lsw-suites.lock";
}

bool RunGuard::acquire(bool exclusive, std::string& why) {
    const HANDLE file =
        ::CreateFileW(marker_path().c_str(), GENERIC_READ, exclusive ? 0 : FILE_SHARE_READ, nullptr,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
            why = exclusive ? "another lsw-suites run is active; a serial suite needs the machine to itself"
                            : "a serial lsw-suites run holds the machine";
        } else {
            why = "cannot open the run marker (error " + std::to_string(error) + ")";
        }
        return false;
    }
    handle_ = file;
    return true;
}

}  // namespace lsw::suites
