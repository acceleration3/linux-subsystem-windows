#pragma once

#include <windows.h>

#include <string>

namespace lsw::suites {
class SystemTool {
public:
    [[nodiscard]] static std::wstring directory();

    [[nodiscard]] static std::wstring command(const std::wstring& line);

    [[nodiscard]] static bool run(const std::wstring& line, DWORD& exit_code, std::string& error, int timeout_ms = 60000);
};
}  // namespace lsw::suites
