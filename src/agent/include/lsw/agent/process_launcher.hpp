#pragma once

#include <string>

namespace lsw::agent {

// Starts an installed Windows app on behalf of the manager. Because
// the agent itself runs in the user's interactive session (launched
// by the per-user Scheduled Task ``lsw-agent`` at logon, with
// ``/RL HIGHEST``), a plain ``CreateProcessW`` is enough — the spawned
// process inherits our session, our environment, and our elevated
// token. Earlier iterations had to do the WTSQueryUserToken /
// CreateProcessAsUserW dance because the agent was a session-0
// service; that's gone.
class ProcessLauncher {
public:
    // ``app_id`` may be a .exe or a .lnk path. .lnk targets are
    // resolved via IShellLink and ``%ENVVAR%`` references are expanded
    // before launching. The spawned PID is returned via ``pid_out``
    // so the host's window-watcher can scope itself to a single
    // launch instance. On failure, ``error_out`` (if non-null) gets
    // a short diagnostic identifying which step failed; the same
    // line is also appended to ``C:\ProgramData\LSW\agent.log``.
    [[nodiscard]] static bool launch(const std::string& app_id,
                                       unsigned long* pid_out = nullptr,
                                       std::string* error_out = nullptr);
};

}  // namespace lsw::agent
