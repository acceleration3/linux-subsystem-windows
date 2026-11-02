// GuestUpkeep's pass over a guest's boots: when it asks Windows which
// release it has, what it hands GuestUpdate, and how the pass ends. The
// agent is a fake that answers over a socket; GuestUpdate is a stand-in
// that records what it is asked and is finished by the test; the machine's
// state is set on a StatusHub that has no libvirt behind it.

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/guest_update.hpp"
#include "lsw/host/services/guest_upkeep.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/guest_stats_probe.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"
#include "lsw/linkproto/envelope.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

using lsw::host::services::GuestUpdate;
using lsw::host::services::GuestUpkeep;
using lsw::host::services::StatusHub;
using lsw::host::services::VmStatus;
using State = GuestUpkeep::State;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

bool wait_for(const std::function<bool()>& done, int milliseconds = 3000) {
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    poll.start(5);
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    if (!done()) loop.exec();
    return done();
}

// For the time nothing is expected to happen.
void settle(int milliseconds = 250) { wait_for([] { return false; }, milliseconds); }

class NoProbe : public lsw::host::vm::IGuestStatsProbe {
public:
    bool available() const override { return false; }
    lsw::host::vm::GuestStatsSample sample() override { return {}; }
};

// What GuestUpdate is to GuestUpkeep, without a bundle or a guest.
class StandIn : public GuestUpdate {
public:
    using GuestUpdate::GuestUpdate;
    struct Call {
        QString release;
        QString version;
    };

    bool running() const noexcept override { return running_now; }
    void run_if_stale(const QString& release, const QString& version) override {
        calls.push_back({release, version});
        running_now = accepts;
        if (!accepts) {
            // As GuestUpdate does for a guest that has the release: succeeded, from the event loop.
            QMetaObject::invokeMethod(this, [this] { Q_EMIT succeeded(); }, Qt::QueuedConnection);
        }
    }
    void end_ok() {
        running_now = false;
        Q_EMIT succeeded();
    }
    void end_failed(const QString& why) {
        running_now = false;
        Q_EMIT failed(why);
    }

    std::vector<Call> calls;
    bool running_now = false;
    // False: the stale check found the guest current and ran nothing.
    bool accepts = true;
};

VmStatus machine(bool running, bool online, bool installing) {
    VmStatus status;
    status.stats.running = running;
    status.stats.agent_reachable = online;
    if (running) {
        lsw::host::vm::DomainDescription definition;
        definition.installing = installing;
        status.running_device = definition;
    }
    return status;
}

}  // namespace

int main(int argc, char** argv) {
    // The manager's settings are a file under the user's configuration.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("XDG_CACHE_HOME", home.path().toUtf8());
    QApplication app(argc, argv);

    // The fake agent.
    bool answer_system_info = true;
    std::string guest_release = "guest-release";
    int clock_requests = 0;
    int info_requests = 0;
    QTcpServer agent;
    check(agent.listen(QHostAddress::LocalHost, 0), "fake agent listening");
    QObject::connect(&agent, &QTcpServer::newConnection, &app, [&] {
        auto* socket = agent.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::readyRead, &app, [&, socket, buffer] {
            *buffer += socket->readAll();
            while (buffer->size() >= 5) {
                const auto size = lsw::linkproto::read_payload_len(
                    reinterpret_cast<const std::uint8_t*>(buffer->constData()));
                if (buffer->size() < 5 + size) break;
                const auto request = nlohmann::json::parse(buffer->mid(5, size).toStdString());
                buffer->remove(0, 5 + size);
                nlohmann::json body = {{"id", request["id"]}, {"error", "unknown command"}};
                if (request["cmd"] == "system_info") {
                    ++info_requests;
                    if (answer_system_info) {
                        body = {{"id", request["id"]},
                                {"result", {{"release", guest_release}, {"release_version", "0.1.0"}}}};
                    } else {
                        body = {{"id", request["id"]}, {"error", "the agent is busy"}};
                    }
                } else if (request["cmd"] == "set_clock") {
                    ++clock_requests;
                    body = {{"id", request["id"]}, {"result", {{"time_zone", "UTC"}, {"clock_set", true}}}};
                }
                const auto reply = body.dump();
                std::uint8_t header[5];
                lsw::linkproto::write_header(header, lsw::linkproto::FrameType::Control,
                                             static_cast<std::uint32_t>(reply.size()));
                socket->write(reinterpret_cast<const char*>(header), 5);
                socket->write(reply.data(), static_cast<qint64>(reply.size()));
            }
        });
    });
    lsw::host::link::AgentRpcClient rpc;
    rpc.set_target(1, agent.serverPort());

    lsw::host::config::Settings settings;
    lsw::host::config::ManagerSettings manager_settings;
    lsw::host::vm::LibvirtConnection libvirt;
    lsw::host::vm::Lifecycle lifecycle{libvirt};
    NoProbe probe;
    StatusHub hub{libvirt, lifecycle, rpc, probe, settings, manager_settings};
    StandIn update{settings, hub, rpc};
    GuestUpkeep upkeep{hub, rpc, update};

    check(upkeep.state() == State::Idle && !upkeep.shares_ready(), "before any machine it is idle and the shares are not ready");

    // Setup has not finished: no check.
    hub.set_vm(machine(true, true, /*installing=*/true));
    settle();
    check(update.calls.empty() && info_requests == 0 && upkeep.state() == State::Idle,
          "while the definition is installing it asks Windows nothing and runs no update");
    hub.set_vm(machine(true, false, false));
    settle();
    check(update.calls.empty() && info_requests == 0, "with no answering agent it asks nothing");

    // The boot's pass.
    hub.set_vm(machine(true, true, false));
    check(wait_for([&] { return update.calls.size() == 1; }), "an answering agent of a set-up guest gets the update check");
    check(update.calls[0].release == QStringLiteral("guest-release") && update.calls[0].version == QStringLiteral("0.1.0"),
          "run_if_stale is called with the release and version the guest reports");
    check(upkeep.state() == State::Updating, "while the update runs the state is Updating");
    check(!upkeep.shares_ready(), "and the shared folders are not ready");
    hub.set_vm(machine(true, true, false));
    hub.set_vm(machine(true, true, false));
    settle();
    check(update.calls.size() == 1 && info_requests == 1, "a status change during the pass starts no second check");
    update.end_ok();
    check(wait_for([&] { return upkeep.state() == State::Current; }), "the update's success goes on to the clock and ends Current");
    check(upkeep.shares_ready() && clock_requests == 1, "the shares are ready and the clock was set once");
    hub.set_vm(machine(true, true, false));
    settle();
    check(update.calls.size() == 1 && clock_requests == 1 && upkeep.state() == State::Current,
          "the check runs once per boot: a later status change of the same boot runs no more");

    // A run asked for elsewhere (Settings) while Current is not a pass.
    update.end_ok();
    settle();
    check(clock_requests == 1 && upkeep.state() == State::Current, "a success of a run that was not this pass's is ignored");
    update.end_failed(QStringLiteral("elsewhere"));
    settle();
    check(upkeep.state() == State::Current, "and so is a failure of one");

    // The machine stops: the next boot has its own pass.
    hub.set_vm(machine(false, false, false));
    check(wait_for([&] { return upkeep.state() == State::Idle; }) && !upkeep.shares_ready(),
          "when the machine stops the state is Idle and the shares are not ready");
    hub.set_vm(machine(true, true, false));
    check(wait_for([&] { return update.calls.size() == 2; }), "the next boot gets its own check");
    update.end_ok();
    check(wait_for([&] { return upkeep.state() == State::Current; }) && clock_requests == 2, "and its own clock");

    // The update fails, then a retry from the dialog succeeds.
    hub.set_vm(machine(false, false, false));
    settle();
    hub.set_vm(machine(true, true, false));
    check(wait_for([&] { return update.calls.size() == 3; }), "a third boot is checked");
    update.end_failed(QStringLiteral("could not copy"));
    check(wait_for([&] { return upkeep.state() == State::Failed; }) && upkeep.error() == QStringLiteral("could not copy"),
          "a failed update ends the pass Failed with its message");
    check(!upkeep.shares_ready(), "and the shares are not ready");
    const int clocks_before = clock_requests;
    update.end_ok();
    check(wait_for([&] { return upkeep.state() == State::Current; }), "a GuestUpdate success while Failed completes the pass");
    check(upkeep.shares_ready() && clock_requests == clocks_before + 1 && upkeep.error().isEmpty(),
          "the shares are ready, the clock stepped and the error is gone");
    check(update.calls.size() == 3, "completing the pass runs no new check");

    // The probe fails.
    hub.set_vm(machine(false, false, false));
    settle();
    answer_system_info = false;
    hub.set_vm(machine(true, true, false));
    check(wait_for([&] { return upkeep.state() == State::Failed; }) && update.calls.size() == 3
              && upkeep.error().contains(QStringLiteral("did not say which LSW release")),
          "an agent that will not say its release fails the pass with no update run");
    answer_system_info = true;
    upkeep.retry();
    check(wait_for([&] { return update.calls.size() == 4; }), "retry asks again, and the update follows");
    update.end_ok();
    check(wait_for([&] { return upkeep.state() == State::Current; }), "and the pass ends Current");

    // The guest is current: GuestUpdate runs nothing.
    hub.set_vm(machine(false, false, false));
    settle();
    update.accepts = false;
    hub.set_vm(machine(true, true, false));
    check(wait_for([&] { return update.calls.size() == 5; }), "a boot is checked again");
    check(wait_for([&] { return upkeep.state() == State::Current; }) && upkeep.shares_ready(),
          "a check that finds the guest current runs no update and ends Current");

    // A machine that stops during a pass drops it.
    update.accepts = true;
    update.calls.clear();
    hub.set_vm(machine(false, false, false));
    settle();
    hub.set_vm(machine(true, true, false));
    check(wait_for([&] { return update.calls.size() == 1 && upkeep.state() == State::Updating; }), "a new boot's pass is running");
    hub.set_vm(machine(false, false, false));
    update.end_ok();
    check(wait_for([&] { return upkeep.state() == State::Idle; }) && !upkeep.shares_ready(),
          "an update that ends after the machine stopped is about a boot that is over: Idle, no shares");

    QThreadPool::globalInstance()->waitForDone();
    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
