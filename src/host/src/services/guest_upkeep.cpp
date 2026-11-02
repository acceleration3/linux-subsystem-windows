#include "lsw/host/services/guest_upkeep.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/guest_update.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QDateTime>
#include <QTimeZone>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include <utility>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::services {

GuestUpkeep::GuestUpkeep(StatusHub& status, link::AgentRpcClient& rpc, GuestUpdate& update,
                         QObject* parent)
    : QObject(parent), status_(status), rpc_(rpc), update_(update) {
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area == StatusHub::Area::Vm) {
            on_status();
        }
    });
    connect(&probe_watcher_, &QFutureWatcher<Probe>::finished, this, [this] {
        const Probe probe = probe_watcher_.result();
        if (end_if_stopped()) {
            return;
        }
        if (!probe.ok) {
            finish_pass(false, probe.error);
            return;
        }
        // While it runs nothing else here touches the guest: the pass goes
        // on from its end.
        awaiting_update_ = true;
        update_.run_if_stale(probe.release, probe.version);
        if (update_.running()) {
            set_state(State::Updating);
        }
    });
    connect(&update_, &GuestUpdate::succeeded, this, [this] {
        // A retry from the dialog or Settings is not a pass of ours, but one
        // that ended Failed is completed by it.
        if (!awaiting_update_ && state_ != State::Failed) {
            return;
        }
        awaiting_update_ = false;
        if (end_if_stopped()) {
            return;
        }
        // The update mounts the shared folders; a pass that did not get that
        // far has not, and a document in a shared folder is not to be opened
        // as if it had.
        shares_reached_ = true;
        set_state(State::Checking);
        clock_watcher_.setFuture(QtConcurrent::run([this] { return set_clock(); }));
    });
    connect(&update_, &GuestUpdate::failed, this, [this](const QString& message) {
        if (!awaiting_update_) {
            return;
        }
        awaiting_update_ = false;
        if (end_if_stopped()) {
            return;
        }
        finish_pass(false, message);
    });
    connect(&clock_watcher_, &QFutureWatcher<Clock>::finished, this, [this] {
        const Clock clock = clock_watcher_.result();
        if (end_if_stopped()) {
            return;
        }
        finish_pass(clock.ok, clock.error);
    });
}

GuestUpkeep::~GuestUpkeep() {
    probe_watcher_.waitForFinished();
    clock_watcher_.waitForFinished();
}

void GuestUpkeep::retry() {
    if (ready() && !busy()) {
        start();
    }
}

bool GuestUpkeep::ready() const {
    const auto& vm = status_.vm();
    const vm::DomainDescription* definition = vm.definition();
    return vm.running() && vm.agent_online() && definition != nullptr && !definition->installing;
}

bool GuestUpkeep::busy() const {
    return probe_watcher_.isRunning() || clock_watcher_.isRunning() || awaiting_update_;
}

// A machine that stops ends the boot. The update takes the agent away and
// brings it back while the pass runs, which is the same boot.
void GuestUpkeep::on_status() {
    if (busy()) {
        // A stop seen now is the end of the boot being passed, which the
        // finished pass must not be taken for the next one's.
        if (!status_.vm().running()) {
            stopped_during_pass_ = true;
        }
        return;
    }
    if (!status_.vm().running()) {
        passed_ = false;
        shares_reached_ = false;
        set_state(State::Idle);
        return;
    }
    if (!passed_ && ready()) {
        start();
    }
}

bool GuestUpkeep::end_if_stopped() {
    if (!stopped_during_pass_) {
        return false;
    }
    // The boot this was a pass for is gone, and so is what it found.
    stopped_during_pass_ = false;
    passed_ = false;
    shares_reached_ = false;
    error_.clear();
    set_state(State::Idle);
    Q_EMIT changed();
    on_status();
    return true;
}

void GuestUpkeep::start() {
    if (busy()) {
        return;
    }
    passed_ = true;
    shares_reached_ = false;
    stopped_during_pass_ = false;
    set_state(State::Checking);
    error_.clear();
    probe_watcher_.setFuture(QtConcurrent::run([this] { return probe(); }));
}

void GuestUpkeep::finish_pass(bool ok, const QString& error) {
    error_ = error;
    if (!set_state(ok ? State::Current : State::Failed)) {
        Q_EMIT changed();
    }
    // A driver put in place reports anew.
    status_.refresh(StatusHub::Area::Graphics);
}

bool GuestUpkeep::set_state(State state) {
    if (state == state_) {
        return false;
    }
    state_ = state;
    Q_EMIT changed();
    return true;
}

GuestUpkeep::Probe GuestUpkeep::probe() {
    const auto info = rpc_.request(linkproto::cmd::kSystemInfo);
    if (!info.ok) {
        LSW_WARN("upkeep: system_info: {}", info.error);
        return {false, {}, {},
                QStringLiteral("Windows did not say which LSW release it has: %1")
                    .arg(QString::fromStdString(info.error))};
    }
    // An agent from before releases reports none, which is a release that
    // differs from every shipped one.
    const auto text = [&info](const char* key) {
        const auto value = info.body.value(key, nlohmann::json{});
        return value.is_string() ? QString::fromStdString(value.get<std::string>()) : QString{};
    };
    return {true, text("release"), text("release_version"), {}};
}

// The time zone and the clock, as the host's.
GuestUpkeep::Clock GuestUpkeep::set_clock() {
    const QByteArray zone = QTimeZone::ianaIdToWindowsId(QTimeZone::systemTimeZoneId());
    if (zone.isEmpty()) {
        LSW_WARN("upkeep: Windows has no name for the host's time zone {}; its clock is left",
                 QTimeZone::systemTimeZoneId().toStdString());
        return {};
    }
    const auto clock = rpc_.request(
        linkproto::cmd::kSetClock,
        {{"time_zone", zone.toStdString()}, {"utc_ms", QDateTime::currentMSecsSinceEpoch()}});
    if (!clock.ok) {
        const QString error = QStringLiteral("Windows' clock was not set: %1")
                                  .arg(QString::fromStdString(clock.error));
        LSW_WARN("upkeep: {}", error.toStdString());
        return {false, error};
    }
    LSW_INFO("upkeep: Windows' time zone {} (was {}), its clock {} ms off{}",
             clock.body.value("time_zone", std::string{}),
             clock.body.value("time_zone_was", std::string{}),
             clock.body.value("clock_off_ms", 0LL),
             clock.body.value("clock_set", false) ? ", set to the host's" : "");
    return {};
}

}  // namespace lsw::host::services
