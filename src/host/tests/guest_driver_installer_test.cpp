// The avgpu install job reports its end as a setup_done event, and the
// agent can send that before the reply to run_script that names the job
// has reached the manager: a script that fails at once does exactly
// that. GuestDriverInstaller keeps what arrives before the id and
// replays it; this pins that, and the abandon path for an agent that
// goes away mid-job. No agent: the events are handed in directly.

#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/guest_driver_installer.hpp"

#include <QCoreApplication>
#include <QStringList>
#include <cstdio>

using lsw::host::link::AgentRpcClient;
using lsw::host::manager::GuestDriverInstaller;
using Event = GuestDriverInstaller::Event;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

struct Seen {
    QStringList progress;
    int succeeded = 0;
    bool restart = false;
    QStringList failed;
};

void watch(GuestDriverInstaller& installer, Seen& seen) {
    QObject::connect(&installer, &GuestDriverInstaller::progress,
                     [&seen](const QString& m) { seen.progress << m; });
    QObject::connect(&installer, &GuestDriverInstaller::succeeded, [&seen](bool restart) {
        ++seen.succeeded;
        seen.restart = restart;
    });
    QObject::connect(&installer, &GuestDriverInstaller::failed,
                     [&seen](const QString& why) { seen.failed << why; });
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};
    AgentRpcClient rpc;  // no target: nothing is sent anywhere

    {
        const QString plain = GuestDriverInstaller::script();
        check(plain.contains(QStringLiteral("if not defined SOURCE set SOURCE=C:\\av\\pristine\r\n")) &&
                  plain.contains(QStringLiteral("robocopy %SOURCE% C:\\av\\resign ")),
              "with no staging, the package setup kept is the one installed");
        const QString staged =
            GuestDriverInstaller::script(QStringLiteral("set SOURCE=C:\\av\\incoming\r\n"));
        const qsizetype install = staged.indexOf(QStringLiteral("-Stage install"));
        const qsizetype failed = staged.indexOf(
            QStringLiteral("if not \"%INSTALLED%\"==\"0\" if not \"%INSTALLED%\"==\"3010\" exit /b"));
        const qsizetype kept = staged.indexOf(
            QStringLiteral("robocopy %SOURCE%\\package C:\\av\\pristine\\package /MIR"));
        check(install >= 0 && failed > install && kept > failed,
              "a staged package becomes the pristine one only once it is installed");
    }

    {
        GuestDriverInstaller installer{rpc};
        Seen seen;
        watch(installer, seen);
        (void)installer.start();
        check(installer.running(), "running from the moment it is started");
        // The job's events, and another job's, all before its id.
        installer.on_event({QStringLiteral("other"), false, false, 0, QStringLiteral("not ours")});
        installer.on_event({QStringLiteral("j1"), false, false, 0, QStringLiteral("Signing")});
        installer.on_event({QStringLiteral("j1"), true, false, 3010, QString{}});
        check(seen.succeeded == 0 && seen.failed.isEmpty(), "nothing is decided before the id");
        installer.on_job_id(QStringLiteral("j1"));
        check(seen.progress.contains(QStringLiteral("Signing")),
              "progress that beat the id is shown");
        check(!seen.progress.contains(QStringLiteral("not ours")), "another job's is not");
        check(seen.succeeded == 1 && seen.restart, "an end that beat the id is not missed");
        check(!installer.running(), "and the job is over");
        // The failed reply to run_script (there is no agent) comes back
        // later and must not undo that.
        for (int i = 0; i < 50 && seen.failed.isEmpty(); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        check(seen.failed.isEmpty(), "a late reply after the end changes nothing");
    }

    {
        GuestDriverInstaller installer{rpc};
        Seen seen;
        watch(installer, seen);
        (void)installer.start();
        installer.on_job_id(QStringLiteral("j2"));
        installer.on_event({QStringLiteral("j2"), false, false, 0, QStringLiteral("Installing")});
        check(seen.progress.contains(QStringLiteral("Installing")),
              "events after the id are acted on at once");
        installer.abandon(QStringLiteral("agent gone"));
        check(seen.failed == QStringList{QStringLiteral("agent gone")} && !installer.running(),
              "an agent that goes away ends the job as a failure");
        installer.on_event({QStringLiteral("j2"), true, true, 0, QString{}});
        check(seen.succeeded == 0, "an end after it was abandoned is ignored");
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
