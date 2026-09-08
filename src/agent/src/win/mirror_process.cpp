#include "lsw/agent/win/mirror_process.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <vector>

namespace lsw::agent::win {

namespace {

// QEMU's user networking puts the host at the gateway address, with no
// forwarding rule needed for a connection the guest opens itself.
constexpr const char* kHostAddress = "10.0.2.2";

void set_error(std::string* into, std::string text) {
    if (into != nullptr) {
        *into = std::move(text);
    }
}

}  // namespace

unsigned long MirrorProcess::start(const std::string& exe, std::uint16_t port,
                                   const std::string& log_path, std::string* error_out) {
    if (exe.empty() || port == 0) {
        set_error(error_out, "start_window_mirror: an executable and a port are required");
        return 0;
    }

    const std::wstring path = util::TextEncoding::to_wide(exe);
    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        set_error(error_out, "start_window_mirror: no such file: " + exe);
        return 0;
    }

    std::wstring command = L"\"" + path + L"\" --host "
                           + util::TextEncoding::to_wide(kHostAddress) + L" --port "
                           + std::to_wstring(port);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    // Where the mirror says what happened. Its output would otherwise go
    // to a console it was given none of.
    std::wstring environment;
    if (!log_path.empty()) {
        environment = L"RWIN_LOG_FILE=" + util::TextEncoding::to_wide(log_path);
    }
    std::vector<wchar_t> block;
    if (!environment.empty()) {
        for (const wchar_t* copy = ::GetEnvironmentStringsW(); copy != nullptr;) {
            const wchar_t* cursor = copy;
            while (*cursor != L'\0') {
                const std::size_t length = ::wcslen(cursor);
                block.insert(block.end(), cursor, cursor + length + 1);
                cursor += length + 1;
            }
            ::FreeEnvironmentStringsW(const_cast<wchar_t*>(copy));
            break;
        }
        block.insert(block.end(), environment.begin(), environment.end());
        block.push_back(L'\0');
        block.push_back(L'\0');
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION spawned{};
    // CREATE_NO_WINDOW: a console program with nobody to read it.
    const BOOL ok = ::CreateProcessW(
        path.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        block.empty() ? nullptr : block.data(), nullptr, &startup, &spawned);
    if (ok == 0) {
        set_error(error_out, "start_window_mirror: CreateProcessW failed (err="
                                 + std::to_string(::GetLastError()) + ")");
        return 0;
    }
    ::CloseHandle(spawned.hProcess);
    ::CloseHandle(spawned.hThread);
    LSW_AGENT_INFO("start_window_mirror: pid=" + std::to_string(spawned.dwProcessId)
                   + " port=" + std::to_string(port));
    return spawned.dwProcessId;
}

}  // namespace lsw::agent::win
