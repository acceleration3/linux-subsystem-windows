#pragma once

#include "lsw/agent/win/unique_handle.hpp"

#include <optional>
#include <string>
#include <vector>

namespace lsw::agent::win {
struct SpawnOptions {
    std::wstring application;
    std::wstring command_line;
    DWORD creation_flags = 0;
    std::vector<wchar_t> environment;
    std::wstring working_dir;
    bool inherit_handles = false;
    HANDLE std_input = nullptr;
    HANDLE std_output = nullptr;
    HANDLE std_error = nullptr;
    bool hide_window = false;
    HANDLE token = nullptr;
    std::wstring desktop;
};

struct SpawnedProcess {
    UniqueHandle process;
    UniqueHandle thread;
    DWORD pid = 0;
};

class Process {
public:
    Process() = delete;

    [[nodiscard]] static std::optional<SpawnedProcess> spawn(const SpawnOptions& options,
                                                             DWORD* error_out = nullptr);

    [[nodiscard]] static std::wstring quote(const std::wstring& path) {
        return L"\"" + path + L"\"";
    }
};

}
