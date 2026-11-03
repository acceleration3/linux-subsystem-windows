#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace lsw::agent::win {
class ProcessInfo {
public:
    ProcessInfo() = delete;

    struct Entry {
        DWORD pid = 0;
        DWORD parent = 0;
        std::wstring exe_name;
    };

    [[nodiscard]] static std::wstring image_path(DWORD pid);

    [[nodiscard]] static std::wstring image_path(HANDLE process);

    [[nodiscard]] static std::optional<std::uint64_t> creation_time(HANDLE process);
    [[nodiscard]] static std::optional<DWORD> parent_pid(HANDLE process);

    [[nodiscard]] static std::uint64_t creation_time_of(DWORD pid);

    [[nodiscard]] static const std::wstring& self_path();

    [[nodiscard]] static std::vector<Entry> snapshot();
};

}
