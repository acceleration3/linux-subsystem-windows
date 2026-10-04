#pragma once

#include <string>

namespace lsw::agent::session {

// The session helper: ``lsw-agent.exe --session --pipe NAME``, started by
// the service in a user's session as that user.
//
// It does the work that has to happen inside the session — enumerating
// and messaging windows, UI Automation and its event subscriptions,
// HKCU — for the commands whose handlers say so
// (RpcHandler::Placement::UserSession), and it runs the desktop gate
// (DesktopSession::wait_for_desktop, with the DesktopProbe trace beside
// it) exactly as the agent did when it was itself started at logon,
// reporting the outcome to the service.
//
// It dials the service's pipe, refuses a server that is not in session 0,
// and exits when the pipe breaks: a helper never outlives its service.
class SessionHost {
public:
    SessionHost() = delete;

    // Runs until the service goes away. Returns the process exit code.
    [[nodiscard]] static int run(const std::wstring& pipe_name);
};

}  // namespace lsw::agent::session
