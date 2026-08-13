// lsw-guest-agent entry point.
//
// Autostarted from the ``lsw-agent`` scheduled task that LswPromote.ps1
// registers at SetupComplete (LogonTrigger on the wizard user's SID,
// LogonType InteractiveToken, RunLevel Highest). The wizard account is a
// local admin and this EXE is manifested requireAdministrator, so
// children inherit a full token for pnputil / sc / driver work.
//
// The task fires the moment LSA mints the session token, which is long
// before the user can see a desktop — so we hold the listener closed
// until the desktop is genuinely usable. That gate matters beyond
// cosmetics: the manager's PostInstallWatcher treats a reachable
// ``:5102`` as "install finished", so binding early makes the wizard
// declare success over the top of "Preparing Windows".

#include "lsw/agent/core/agent_service.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/win/desktop_probe.hpp"
#include "lsw/agent/win/desktop_session.hpp"
#include "lsw/agent/win/token_privileges.hpp"

#include <chrono>

int wmain(int /*argc*/, wchar_t** /*argv*/) {
    LSW_AGENT_INFO("agent: process start");

    // Bounded: a machine whose shell never arrives (replaced shell, or
    // the agent run by hand outside an interactive session) still gets a
    // reachable agent, just a late one. That is worth more than a
    // guaranteed-correct gate, because an unreachable agent cannot be
    // diagnosed remotely at all.
    // Sample every readiness signal for the whole boot, independently of
    // the gate below. The first-logon "Preparing Windows" phase happens
    // once per install and cannot be reproduced by rebooting, so there
    // is no second chance to observe it — the trace has to be captured
    // on the run that matters. Keeps going after the gate opens, because
    // the diagnostic question is "what was still false when we let go".
    lsw::agent::win::DesktopProbe probe;
    probe.start(std::chrono::minutes(20), std::chrono::milliseconds(500));

    // The desktop must stay uncovered for a couple of seconds, not just
    // be uncovered at one instant: OOBE hands off between several
    // full-screen hosts and there are gaps in between that would
    // otherwise read as ready.
    if (!lsw::agent::win::DesktopSession::wait_for_desktop(
            std::chrono::minutes(30), std::chrono::milliseconds(500),
            std::chrono::seconds(3), std::chrono::seconds(90))) {
        LSW_AGENT_WARN("agent: desktop never became usable after 30 min — "
                       "starting anyway");
    }

    // Stamp the exact state at the instant the gate opened, so it can be
    // lined up against the probe trace either side of it.
    lsw::agent::win::DesktopProbe::write(
        lsw::agent::win::DesktopProbe::snapshot("gate-released"));

    (void)lsw::agent::win::TokenPrivileges::enable_install_privileges();
    return lsw::agent::core::AgentService::instance().run();
}
