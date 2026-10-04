#pragma once

#include "lsw/agent/win/unique_handle.hpp"

#include <optional>
#include <string>
#include <vector>

namespace lsw::agent::win {

// Everything one CreateProcessW call is asked for, one field per argument
// or flag, so each caller states exactly what it passes and nothing is
// implied: a launch depends on CREATE_SUSPENDED, the console on inherited
// pipe handles, the installer on a hidden window, and those differences
// are the whole reason the four call sites were never merged by hand.
struct SpawnOptions {
    // lpApplicationName. Empty passes nullptr, and the first token of
    // ``command_line`` is resolved through the search path instead.
    std::wstring application;
    std::wstring command_line;
    DWORD creation_flags = 0;
    // A block from Environment::block_with. Empty passes nullptr: the
    // child inherits ours.
    std::vector<wchar_t> environment;
    // Empty passes nullptr: the child inherits ours.
    std::wstring working_dir;
    bool inherit_handles = false;
    // Any of the three set means STARTF_USESTDHANDLES with all three.
    HANDLE std_input = nullptr;
    HANDLE std_output = nullptr;
    HANDLE std_error = nullptr;
    // STARTF_USESHOWWINDOW with SW_HIDE.
    bool hide_window = false;
    // A primary token to start the process as (CreateProcessAsUserW);
    // null starts it as this process (CreateProcessW). Borrowed, not
    // owned. Set by RunAs::apply, which also supplies the user's
    // environment block — a token without one would hand the child the
    // service's environment.
    HANDLE token = nullptr;
    // lpDesktop, e.g. ``winsta0\default``. Empty passes nullptr: the
    // child gets this process's window station and desktop.
    std::wstring desktop;
};

// A process CreateProcessW started, with both of its handles owned.
struct SpawnedProcess {
    UniqueHandle process;
    UniqueHandle thread;
    DWORD pid = 0;
};

// Starts processes.
//
// Inheritance is explicit: with ``inherit_handles`` set, only the three
// standard handles are inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST),
// never whatever else happens to be inheritable in the agent at that
// moment — another console's pipe end leaking into this child would keep
// that console from ever seeing EOF.
//
// A non-empty environment block is always passed as UTF-16
// (CREATE_UNICODE_ENVIRONMENT is added), since that is what every block
// the agent builds is.
class Process {
public:
    Process() = delete;

    // Nullopt on failure, with the Win32 error in ``error_out`` — read
    // there rather than from GetLastError, which nothing between the
    // failure and the caller is obliged to leave alone.
    [[nodiscard]] static std::optional<SpawnedProcess> spawn(const SpawnOptions& options,
                                                             DWORD* error_out = nullptr);

    // ``path`` in double quotes, so a space in it does not split it into
    // two arguments. Nothing is escaped: a Windows path cannot contain a
    // quote, and this is for paths only.
    [[nodiscard]] static std::wstring quote(const std::wstring& path) {
        return L"\"" + path + L"\"";
    }
};

}  // namespace lsw::agent::win
