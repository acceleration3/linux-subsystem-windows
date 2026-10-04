#include "lsw/agent/win/process.hpp"

namespace lsw::agent::win {

std::optional<SpawnedProcess> Process::spawn(const SpawnOptions& options, DWORD* error_out) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (options.std_input != nullptr || options.std_output != nullptr ||
        options.std_error != nullptr) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = options.std_input;
        startup.hStdOutput = options.std_output;
        startup.hStdError = options.std_error;
    }
    if (options.hide_window) {
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }

    // CreateProcessW writes through the command-line buffer, so it must
    // be a mutable copy.
    std::vector<wchar_t> command(options.command_line.begin(), options.command_line.end());
    command.push_back(L'\0');

    PROCESS_INFORMATION spawned{};
    const BOOL ok = ::CreateProcessW(
        options.application.empty() ? nullptr : options.application.c_str(), command.data(),
        nullptr, nullptr, options.inherit_handles ? TRUE : FALSE, options.creation_flags,
        // Only read, whatever the parameter's constness says.
        options.environment.empty() ? nullptr : const_cast<wchar_t*>(options.environment.data()),
        options.working_dir.empty() ? nullptr : options.working_dir.c_str(), &startup, &spawned);
    if (ok == 0) {
        if (error_out != nullptr) {
            *error_out = ::GetLastError();
        }
        return std::nullopt;
    }
    if (error_out != nullptr) {
        *error_out = ERROR_SUCCESS;
    }
    SpawnedProcess out;
    out.process.reset(spawned.hProcess);
    out.thread.reset(spawned.hThread);
    out.pid = spawned.dwProcessId;
    return out;
}

}  // namespace lsw::agent::win
