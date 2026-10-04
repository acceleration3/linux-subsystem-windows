#pragma once

#include "lsw/agent/install/process_outcome.hpp"

#include <string>

namespace lsw::agent::win {
class RunAs;
}

namespace lsw::agent::install {

// Spawns a child process and waits for it.
//
// As SYSTEM in session 0 by default — the agent is a LocalSystem
// service, so a plain CreateProcessW is that — or as a session's user
// when the job was asked to run there (``win::RunAs``). Either way the
// child gets no window: CREATE_NO_WINDOW and SW_HIDE.
//
// A Job Object wraps the child purely so a timeout can kill the WHOLE
// tree: a wedged installer is often a grandchild of the cmd.exe we
// spawned. Notably NOT kill-on-close: the agent-redeploy script stops
// the agent's service mid-run on purpose and then starts it again, and
// kill-on-close would take the still-running swap script down with the
// service process.
class ProcessRunner {
public:
    ProcessRunner() = delete;

    // 0xFFFFFFFF == Win32 INFINITE, spelled literally so this header
    // need not pull in <windows.h>.
    static constexpr unsigned long kNoTimeout = 0xFFFFFFFFUL;

    // Blocking. On timeout the process tree is terminated and the
    // outcome reports ``completed == false`` with a "timed out" message.
    [[nodiscard]] static ProcessOutcome run(const std::wstring& command_line,
                                            const win::RunAs& as,
                                            unsigned long timeout_ms = kNoTimeout);

    // As this process (SYSTEM, for the service).
    [[nodiscard]] static ProcessOutcome run(const std::wstring& command_line,
                                            unsigned long timeout_ms = kNoTimeout);
};

}  // namespace lsw::agent::install
