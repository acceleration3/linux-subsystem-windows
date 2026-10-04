#include "lsw/agent/win/process_info.hpp"

#include "lsw/agent/win/file_time.hpp"
#include "lsw/agent/win/unique_handle.hpp"

// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <algorithm>

namespace lsw::agent::win {

namespace {

// The longest path Win32 can express (UNICODE_STRING_MAX_CHARS).
constexpr std::size_t kMaxPathChars = 32767;

// Call ``query(buffer, &length)`` — ``length`` in: capacity, out: chars
// written — with a buffer that grows until the answer fits or the path
// limit is reached. ``query`` returns false on failure, and
// ``insufficient()`` says whether that failure meant "buffer too small".
template<class Query, class Insufficient>
[[nodiscard]] std::wstring grow_until_fits(Query query, Insufficient insufficient) {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD length = static_cast<DWORD>(buf.size());
        if (query(buf.data(), &length)) {
            buf.resize(length);
            return buf;
        }
        if (!insufficient() || buf.size() >= kMaxPathChars) {
            return {};
        }
        buf.resize(std::min(buf.size() * 4, kMaxPathChars));
    }
}

}  // namespace

std::wstring ProcessInfo::image_path(DWORD pid) {
    const UniqueHandle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process) {
        return {};
    }
    return image_path(process.get());
}

std::wstring ProcessInfo::image_path(HANDLE process) {
    return grow_until_fits(
        [process](wchar_t* buf, DWORD* length) {
            return ::QueryFullProcessImageNameW(process, 0, buf, length) != 0;
        },
        [] { return ::GetLastError() == ERROR_INSUFFICIENT_BUFFER; });
}

std::optional<std::uint64_t> ProcessInfo::creation_time(HANDLE process) {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (::GetProcessTimes(process, &created, &exited, &kernel, &user) == 0) {
        return std::nullopt;
    }
    return FileTime::to_u64(created);
}

const std::wstring& ProcessInfo::self_path() {
    static const std::wstring self = grow_until_fits(
        [](wchar_t* buf, DWORD* length) {
            // Returns the length copied; a result equal to the buffer
            // size means it was truncated.
            const DWORD n = ::GetModuleFileNameW(nullptr, buf, *length);
            if (n == 0 || n >= *length) {
                return false;
            }
            *length = n;
            return true;
        },
        [] { return ::GetLastError() == ERROR_INSUFFICIENT_BUFFER; });
    return self;
}

std::vector<ProcessInfo::Entry> ProcessInfo::snapshot() {
    std::vector<Entry> entries;
    const UniqueHandle snap{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (!snap) {
        return entries;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snap.get(), &entry) != FALSE) {
        do {
            entries.push_back({entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile});
        } while (::Process32NextW(snap.get(), &entry) != FALSE);
    }
    return entries;
}

}  // namespace lsw::agent::win
