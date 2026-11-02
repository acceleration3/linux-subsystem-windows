#include "lsw/agent/system/log_clearer.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"

#include <windows.h>

#include <cwctype>

namespace lsw::agent::system {

namespace fs = std::filesystem;

namespace {

std::wstring lower(std::wstring text) {
    for (wchar_t& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

bool is_rolled(const fs::path& file) {
    const std::wstring stem = file.stem().wstring();
    const std::size_t dot = stem.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= stem.size()) {
        return false;
    }
    for (std::size_t i = dot + 1; i < stem.size(); ++i) {
        if (std::iswdigit(stem[i]) == 0) {
            return false;
        }
    }
    return true;
}

bool is_helper_log(const fs::path& file) {
    return lower(file.filename().wstring()).rfind(L"agent-session-", 0) == 0;
}

}

nlohmann::json LogClearer::run() {
    sweep(core::Paths::logs_dir(), true, false);
    sweep(core::Paths::driver_logs_dir(), false, true);
    return {{"deleted", deleted_}, {"emptied", emptied_}, {"skipped", skipped_}};
}

void LogClearer::sweep(const fs::path& dir, bool recursive, bool only_logs) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (recursive) {
        for (fs::recursive_directory_iterator it{dir, ec}, end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                files.push_back(it->path());
            }
        }
    } else {
        for (fs::directory_iterator it{dir, ec}, end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && (!only_logs || lower(it->path().extension().wstring()) == L".log")) {
                files.push_back(it->path());
            }
        }
    }
    for (const fs::path& file : files) {
        clear(file);
    }
}

void LogClearer::clear(const fs::path& file) {
    if (lower(file.wstring()) == lower(core::Paths::agent_log())) {
        log::clear_file();
        ++emptied_;
        return;
    }
    unsigned long error = 0;
    if (is_helper_log(file) && !is_rolled(file)) {
        if (empty(file, error)) {
            ++emptied_;
        } else {
            skip(file, error);
        }
        return;
    }
    if (::DeleteFileW(file.c_str()) != FALSE) {
        ++deleted_;
        return;
    }
    error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) {
        return;
    }
    if (empty(file, error)) {
        ++emptied_;
    } else {
        skip(file, error);
    }
}

bool LogClearer::empty(const fs::path& file, unsigned long& error) {
    const HANDLE handle = ::CreateFileW(file.c_str(), GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = ::GetLastError();
        return false;
    }
    const bool done = ::SetEndOfFile(handle) != FALSE;
    if (!done) {
        error = ::GetLastError();
    }
    ::CloseHandle(handle);
    return done;
}

void LogClearer::skip(const fs::path& file, unsigned long error) {
    skipped_.push_back(util::TextEncoding::to_utf8(file.wstring()) + ": " +
                       win::ErrorText::win32(error));
}

}
