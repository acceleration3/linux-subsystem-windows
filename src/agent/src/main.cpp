// lsw-guest-agent entry point (deployed as C:\ProgramData\LSW\lsw-agent.exe).
//
// One executable, three ways to run it (core::CommandLine):
//
//   lsw-agent.exe --service
//       The ``lsw-agent`` Windows service: LocalSystem, session 0,
//       automatic start, restarted by the SCM if it fails. Registered by
//       the autounattend's LswPromote.ps1 at SetupComplete. Owns the
//       manager link (TCP 5102 and the shared-memory ring), installs,
//       consoles and process launching — into any session, as that
//       session's user (win::RunAs).
//
//   lsw-agent.exe --session --pipe NAME
//       A session helper, started by the service in each user session as
//       that user. Serves what only works from inside a session (windows,
//       UI Automation, HKCU) and runs the desktop gate that keeps the
//       service's listener closed until the desktop is genuinely usable —
//       which matters beyond cosmetics: the manager's PostInstallWatcher
//       treats a reachable :5102 as "install finished".
//
//   lsw-agent.exe
//       The service's work in the foreground, as whoever starts it, for
//       debugging. It can act for its own session only (borrowing another
//       session's token is LocalSystem's privilege).
//
// No console, ever: the executable is a GUI-subsystem program, so neither
// the service nor a helper nor a debugging run opens a console window,
// and nothing it starts for its own purposes gets one either
// (CREATE_NO_WINDOW). Everything it has to say goes to its log file.
//
// The manifest asks for asInvoker: the service is LocalSystem already,
// and a helper has to start without a UAC prompt — the service hands it
// the user's full token itself when it should be elevated.

#include "lsw/agent/core/agent_service.hpp"
#include "lsw/agent/core/command_line.hpp"
#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/core/service_host.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/session/session_host.hpp"
#include "lsw/agent/win/wts_sessions.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdlib>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    using lsw::agent::core::CommandLine;

    // A GUI-subsystem program reports a CRT abort, and Windows a crash or
    // a missing disk, with a message box. In session 0 nobody can click
    // it, and the process would hang on it instead of ending — which the
    // SCM's recovery could act on. So: no dialogs, ever.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    const CommandLine cl = CommandLine::parse(args);

    // A helper writes a log of its own: two processes appending to one
    // file would interleave, and its records reach agent.log anyway
    // through the service once it is connected.
    if (cl.mode == CommandLine::Mode::SessionHelper) {
        lsw::agent::log::set_file(
            lsw::agent::core::Paths::session_helper_log(lsw::agent::win::WtsSessions::current()));
    }
    if (!cl.error.empty()) {
        LSW_AGENT_ERROR("agent: {}", cl.error);
        lsw::agent::log::flush(1000);
        return 2;
    }

    switch (cl.mode) {
    case CommandLine::Mode::Service:
        LSW_AGENT_INFO("agent: process start (service)");
        return lsw::agent::core::ServiceHost::run();
    case CommandLine::Mode::SessionHelper: {
        const int code = lsw::agent::session::SessionHost::run(cl.pipe);
        LSW_AGENT_INFO("helper: exiting (code {})", code);
        lsw::agent::log::flush(1000);
        // Ended outright: the helper's threads are not joined (a UI
        // Automation call into a hung application cannot be interrupted),
        // and nothing must run destructors underneath them.
        ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(code));
        return code;
    }
    case CommandLine::Mode::Foreground:
        break;
    }
    LSW_AGENT_INFO("agent: process start (foreground)");
    const int code = lsw::agent::core::AgentService::instance().run(
        lsw::agent::core::AgentService::Mode::Foreground);
    lsw::agent::log::flush(1000);
    return code;
}
