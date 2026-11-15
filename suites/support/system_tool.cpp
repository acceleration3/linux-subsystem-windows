#include "support/system_tool.hpp"

namespace lsw::suites {

std::wstring SystemTool::directory() {
    BOOL wow64 = FALSE;
    ::IsWow64Process(::GetCurrentProcess(), &wow64);
    wchar_t path[MAX_PATH] = {};
    if (wow64 != FALSE) {
        const UINT length = ::GetSystemWindowsDirectoryW(path, MAX_PATH);
        return std::wstring(path, length) + L"\\Sysnative";
    }
    const UINT length = ::GetSystemDirectoryW(path, MAX_PATH);
    return std::wstring(path, length);
}

std::wstring SystemTool::command(const std::wstring& line) {
    const std::size_t end = line.find(L' ');
    const std::wstring tool = line.substr(0, end);
    if (tool.find_first_of(L"\\/:\"") != std::wstring::npos) {
        return line;
    }
    return L"\"" + directory() + L"\\" + tool + L"\"" + (end == std::wstring::npos ? std::wstring() : line.substr(end));
}

bool SystemTool::run(const std::wstring& line, DWORD& exit_code, std::string& error, int timeout_ms) {
    std::wstring full = command(line);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    exit_code = ~0u;
    if (!::CreateProcessW(nullptr, full.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
        error = "CreateProcess failed with error " + std::to_string(::GetLastError());
        return false;
    }
    const DWORD waited = ::WaitForSingleObject(info.hProcess, static_cast<DWORD>(timeout_ms));
    if (waited == WAIT_OBJECT_0) {
        ::GetExitCodeProcess(info.hProcess, &exit_code);
    } else {
        error = "the process did not finish within " + std::to_string(timeout_ms) + " ms";
    }
    ::CloseHandle(info.hProcess);
    ::CloseHandle(info.hThread);
    return waited == WAIT_OBJECT_0;
}

}  // namespace lsw::suites
