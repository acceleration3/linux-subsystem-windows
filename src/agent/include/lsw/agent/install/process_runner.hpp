#pragma once

#include "lsw/agent/install/process_outcome.hpp"

#include <string>

namespace lsw::agent::install {

// Spawns a child process and waits for it.
//
// The agent runs as the user, in-session, elevated-admin (HKLM\Run
// autostart), so a plain CreateProcessW suffices — the child inherits
// the elevated token and the interactive desktop. No session bridging.
//
// A Job Object wraps the child purely so a timeout can kill the WHOLE
// tree: a wedged installer is often a grandchild of the cmd.exe we
// spawned. Notably NOT kill-on-close — the agent-redeploy script kills
// the agent mid-run on purpose and then relaunches it, and kill-on-close
// would take the still-running swap script down with it.
class ProcessRunner {
public:
    ProcessRunner() = delete;

    // 0xFFFFFFFF == Win32 INFINITE, spelled literally so this header
    // need not pull in <windows.h>.
    static constexpr unsigned long kNoTimeout = 0xFFFFFFFFUL;

    // Blocking. On timeout the process tree is terminated and the
    // outcome reports ``completed == false`` with a "timed out" message.
    [[nodiscard]] static ProcessOutcome run(const std::wstring& command_line,
                                            unsigned long timeout_ms = kNoTimeout);
};

}  // namespace lsw::agent::install
