#include "core/result_log.hpp"

#include "core/json_report.hpp"

#include <windows.h>

namespace lsw::suites {

ResultLog::~ResultLog() {
    if (handle_ != nullptr) {
        ::CloseHandle(handle_);
    }
}

bool ResultLog::open(const std::filesystem::path& path) {
    std::error_code ignored;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ignored);
    }
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    handle_ = file;
    return true;
}

void ResultLog::append(const CheckResult& result) {
    if (handle_ == nullptr) {
        return;
    }
    const std::string line = JsonReport::check_json(result).dump(0) + '\n';
    const std::lock_guard<std::mutex> lock(mutex_);
    std::size_t done = 0;
    while (done < line.size()) {
        DWORD written = 0;
        if (::WriteFile(handle_, line.data() + done, static_cast<DWORD>(line.size() - done), &written, nullptr) == 0 ||
            written == 0) {
            return;
        }
        done += written;
    }
    ::FlushFileBuffers(handle_);
}

}  // namespace lsw::suites
