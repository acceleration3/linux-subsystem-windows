#include "lsw/agent/win/rolling_file.hpp"

#include <windows.h>

#include <filesystem>

namespace lsw::agent::win {

RollingFile::RollingFile(std::wstring path, Limits limits)
    : path_(std::move(path)), limits_(limits), handle_(INVALID_HANDLE_VALUE) {
    if (limits_.max_files < 1) {
        limits_.max_files = 1;
    }
}

RollingFile::~RollingFile() {
    close();
}

std::wstring RollingFile::numbered(const std::wstring& path, int index) {
    if (index <= 0) {
        return path;
    }
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::size_t dot = path.find_last_of(L'.');
    const bool has_extension = dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
    if (!has_extension) {
        return path + L"." + std::to_wstring(index);
    }
    return path.substr(0, dot) + L"." + std::to_wstring(index) + path.substr(dot);
}

bool RollingFile::open() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        return true;
    }
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path{path_}.parent_path(), ignored);
    handle_ = ::CreateFileW(path_.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    size_ = ::GetFileSizeEx(handle_, &size) != FALSE ? static_cast<std::uint64_t>(size.QuadPart) : 0;
    return true;
}

void RollingFile::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    size_ = 0;
}

void RollingFile::roll() {
    close();
    if (::GetFileAttributesW(path_.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    ::DeleteFileW(numbered(path_, limits_.max_files - 1).c_str());
    for (int i = limits_.max_files - 2; i >= 0; --i) {
        const std::wstring from = numbered(path_, i);
        if (::GetFileAttributesW(from.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ::MoveFileExW(from.c_str(), numbered(path_, i + 1).c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
}

bool RollingFile::append(std::string_view text) {
    if (!open()) {
        return false;
    }
    if (size_ > 0 && size_ + text.size() > limits_.max_bytes) {
        roll();
        if (!open()) {
            return false;
        }
    }
    const char* data = text.data();
    std::size_t left = text.size();
    while (left > 0) {
        DWORD wrote = 0;
        if (::WriteFile(handle_, data, static_cast<DWORD>(left), &wrote, nullptr) == FALSE) {
            return false;
        }
        data += wrote;
        left -= wrote;
        size_ += wrote;
    }
    return true;
}

void RollingFile::roll_now() {
    close();
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (::GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &info) == FALSE ||
        (info.nFileSizeHigh == 0 && info.nFileSizeLow == 0)) {
        return;
    }
    roll();
}

}
