#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::apps {

// Starts an installed Windows app on behalf of the manager. Because
// the agent itself runs in the user's interactive session (launched
// by the per-user Scheduled Task ``lsw-agent`` at logon, with
// ``/RL HIGHEST``), a plain ``CreateProcessW`` is enough — the spawned
// process inherits our session, our environment, and our elevated
// token. Earlier iterations had to do the WTSQueryUserToken /
// CreateProcessAsUserW dance because the agent was a session-0
// service; that's gone.
class Launcher {
public:
    // One ``NAME=value`` override for the child's environment.
    using EnvVar = std::pair<std::string, std::string>;

    // ``app_id`` may be a .exe or a .lnk path. .lnk targets are
    // resolved via IShellLink and ``%ENVVAR%`` references are expanded
    // before launching. The spawned PID is returned via ``pid_out``
    // so the host's window-watcher can scope itself to a single
    // launch instance. On failure, ``error_out`` (if non-null) gets
    // a short diagnostic identifying which step failed; the same
    // line is also appended to ``C:\ProgramData\LSW\agent.log``.
    //
    // ``args`` is appended to the command line, after any a .lnk
    // carries of its own.
    //
    // ``env`` overrides variables for this child only. The child
    // otherwise inherits the agent's environment, so an override is
    // exactly that — a name replaced, not a fresh environment. This
    // exists because some applications are configured only through
    // their environment (Mesa's ``GALLIUM_DRIVER`` picks the OpenGL
    // backend, for instance) and the alternatives are all worse: a
    // machine-wide variable reaches every process and needs a logon to
    // take effect, and a batch wrapper hides the real executable from
    // the launch tracking.
    [[nodiscard]] static bool launch(const std::string& app_id,
                                       const std::string& args = {},
                                       const std::vector<EnvVar>& env = {},
                                       unsigned long* pid_out = nullptr,
                                       std::string* error_out = nullptr);
};

}  // namespace lsw::agent::apps
