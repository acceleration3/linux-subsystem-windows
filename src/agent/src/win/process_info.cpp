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
constexpr std::size_t kMaxPathChars = 32767;
constexpr ULONG kProcessBasicInformation = 0;

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

}

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

std::optional<DWORD> ProcessInfo::parent_pid(HANDLE process) {
    struct BasicInformation {
        LONG exit_status;
        PVOID peb_base;
        ULONG_PTR affinity_mask;
        LONG base_priority;
        ULONG_PTR unique_process_id;
        ULONG_PTR inherited_from_unique_process_id;
    };
    using QueryInformationProcess = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    const auto query = ntdll == nullptr ? nullptr
                                        : reinterpret_cast<QueryInformationProcess>(
                                              ::GetProcAddress(ntdll, "NtQueryInformationProcess"));
    BasicInformation info{};
    if (query == nullptr ||
        query(process, kProcessBasicInformation, &info, sizeof(info), nullptr) < 0) {
        return std::nullopt;
    }
    return static_cast<DWORD>(info.inherited_from_unique_process_id);
}

std::uint64_t ProcessInfo::creation_time_of(DWORD pid) {
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return 0;
    }
    const auto created = creation_time(process);
    ::CloseHandle(process);
    return created.value_or(0);
}

const std::wstring& ProcessInfo::self_path() {
    static const std::wstring self = grow_until_fits(
        [](wchar_t* buf, DWORD* length) {
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

}
