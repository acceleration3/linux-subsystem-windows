#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::win {
class RunAs;
}

namespace lsw::agent::apps {

// Starts an installed Windows app on behalf of the manager.
//
// The agent is a LocalSystem service in session 0, so an application is
// started as somebody else: ``as`` is normally the user of the session
// the request named (by default the console's), with their full token —
// what applications got when the agent itself ran as that user,
// elevated — their environment and their interactive desktop. SYSTEM in
// session 0 is possible too, for a request that asks for session 0.
class Launcher {
public:
    // One ``NAME=value`` override for the child's environment.
    using EnvVar = std::pair<std::string, std::string>;
    enum class Mode { NewInstance, Open };

    // ``app_id`` may be a .exe or a .lnk path. .lnk targets are
    // resolved via IShellLink and ``%ENVVAR%`` references are expanded
    // against ``as``'s environment before launching. The spawned PID is
    // returned via ``pid_out`` so the host's window-watcher can scope
    // itself to a single launch instance. On failure, ``error_out`` (if
    // non-null) gets a short diagnostic identifying which step failed;
    // the same line is also appended to ``C:\ProgramData\LSW\agent.log``.
    //
    // ``args`` is appended to the command line, after any a .lnk
    // carries of its own.
    //
    // ``env`` overrides variables for this child only, on top of the
    // environment ``as`` gets — an override is exactly that, a name
    // replaced, not a fresh environment. This exists because some
    // applications are configured only through their environment
    // (Mesa's ``GALLIUM_DRIVER`` picks the OpenGL backend, for instance)
    // and the alternatives are all worse: a machine-wide variable
    // reaches every process and needs a logon to take effect, and a
    // batch wrapper hides the real executable from the launch tracking.
    // ``visible_pids`` is the target session's titled-window owners. In Open
    // mode a process without a visible window needs its normal activation command.
    [[nodiscard]] static bool launch(const std::string& app_id, const std::string& args,
                                     const std::vector<EnvVar>& env, const win::RunAs& as,
                                     unsigned long* pid_out = nullptr,
                                     std::string* error_out = nullptr,
                                      Mode mode = Mode::NewInstance,
                                      const std::vector<unsigned long>& visible_pids = {});
};

}  // namespace lsw::agent::apps
