#include "lsw/host/services/guest_upkeep.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/agent_redeployer.hpp"
#include "lsw/host/services/guest_swap_script.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QTimeZone>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include <utility>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::services {

GuestUpkeep::GuestUpkeep(StatusHub& status, link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), status_(status), rpc_(rpc) {
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area == StatusHub::Area::Vm) {
            on_status();
        }
    });
    connect(&watcher_, &QFutureWatcher<Result>::finished, this, [this] {
        const Result result = watcher_.result();
        if (stopped_during_pass_) {
            // The boot this was a pass for is gone, and so is what it found.
            stopped_during_pass_ = false;
            passed_ = false;
            shares_reached_ = false;
            shares_error_.clear();
            error_.clear();
            set_state(State::Idle);
            Q_EMIT changed();
            on_status();
            return;
        }
        shares_reached_ = result.shares_reached;
        if (!result.attempted.isEmpty()) {
            attempted_.insert(result.attempted);
        }
        error_ = result.error;
        const bool shares_changed = shares_error_ != result.shares_error;
        shares_error_ = result.shares_error;
        if (!set_state(result.ok ? State::Current : State::Failed) && shares_changed) {
            Q_EMIT changed();
        }
        // An agent swapped in reports the driver package anew.
        status_.refresh(StatusHub::Area::Graphics);
    });
}

GuestUpkeep::~GuestUpkeep() { watcher_.waitForFinished(); }

void GuestUpkeep::retry() {
    attempted_.clear();
    if (ready()) {
        start();
    }
}

bool GuestUpkeep::ready() const {
    const auto& vm = status_.vm();
    const vm::DomainDescription* definition = vm.definition();
    return vm.running() && vm.agent_online() && definition != nullptr && !definition->installing;
}

// A machine that stops ends the boot. The swap takes the agent away and
// brings it back while the pass runs, which is the same boot.
void GuestUpkeep::on_status() {
    if (watcher_.isRunning()) {
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
        const bool had_shares_error = !shares_error_.isEmpty();
        shares_error_.clear();
        if (!set_state(State::Idle) && had_shares_error) {
            Q_EMIT changed();
        }
        return;
    }
    if (!passed_ && ready()) {
        start();
    }
}

void GuestUpkeep::start() {
    if (watcher_.isRunning()) {
        return;
    }
    passed_ = true;
    shares_reached_ = false;
    stopped_during_pass_ = false;
    set_state(State::Checking);
    error_.clear();
    // The shares the machine has now: its live definition, which is what
    // gave Windows its virtio-fs devices.
    QVector<GuestShares::Mount> mounts;
    if (const vm::DomainDescription* definition = status_.vm().definition()) {
        mounts = GuestShares::mounts_of(*definition);
    }
    watcher_.setFuture(QtConcurrent::run(
        [this, attempted = attempted_, mounts] { return pass(attempted, mounts); }));
}

bool GuestUpkeep::set_state(State state) {
    if (state == state_) {
        return false;
    }
    state_ = state;
    Q_EMIT changed();
    return true;
}

GuestUpkeep::Result GuestUpkeep::pass(QSet<QString> attempted,
                                      const QVector<GuestShares::Mount>& mounts) {
    Result result;

    // 1. The agent and the mirror's guest half, against the shipped ones.
    const AgentRedeployer::Shipped shipped = AgentRedeployer::shipped();
    if (shipped.agent.isEmpty()) {
        // Nothing to compare with: a build without the guest's binaries.
        LSW_INFO("upkeep: no agent shipped here ({}); the guest's is left as it is",
                 shipped.error);
    } else {
        const std::string agent_hash = AgentRedeployer::sha256(shipped.agent);
        const std::string mirror_hash =
            shipped.mirror.isEmpty() ? std::string{} : AgentRedeployer::sha256(shipped.mirror);
        nlohmann::json paths = nlohmann::json::array({GuestSwapScript::kGuestAgent});
        if (!mirror_hash.empty()) {
            paths.push_back(GuestSwapScript::kGuestMirror);
        }
        const auto reply = rpc_.request(linkproto::cmd::kFileHashes, {{"paths", paths}});
        const auto in_guest = [&reply](const char* path) {
            const auto found = reply.body.value("hashes", nlohmann::json::object());
            return found.contains(path) && found[path].is_string()
                       ? found[path].get<std::string>()
                       : std::string{};
        };
        const bool agent_stale = !reply.ok || in_guest(GuestSwapScript::kGuestAgent) != agent_hash;
        const bool mirror_stale =
            reply.ok && !mirror_hash.empty() && in_guest(GuestSwapScript::kGuestMirror) != mirror_hash;
        const QString pair = QString::fromStdString(agent_hash + mirror_hash);
        if ((agent_stale || mirror_stale) && !attempted.contains(pair)) {
            LSW_INFO("upkeep: the guest's {} differ from the ones shipped; swapping them",
                     !reply.ok      ? "agent (too old for file_hashes) and mirror"
                     : agent_stale  ? "agent and mirror"
                                    : "mirror");
            result.attempted = pair;
            QPointer<GuestUpkeep> self(this);
            QMetaObject::invokeMethod(this, [self] {
                if (self) {
                    self->set_state(State::Updating);
                }
            }, Qt::QueuedConnection);
            const auto outcome = AgentRedeployer{rpc_}.run();
            LSW_INFO("upkeep: {}", outcome.message);
            if (!outcome.ok) {
                result.ok = false;
                result.error = QString::fromStdString(outcome.message);
                return result;
            }
        }
    }

    // 2. The shared folders.
    result.shares_reached = true;
    const GuestShares::Outcome shares = GuestShares{rpc_}.run(mounts);
    if (shares.ok) {
        LSW_INFO("upkeep: shared folders: {}", shares.message.toStdString());
    } else {
        LSW_WARN("upkeep: shared folders: {}", shares.message.toStdString());
        result.shares_error = shares.message;
    }

    // 3. The time zone and the clock, as the host's.
    const QByteArray zone = QTimeZone::ianaIdToWindowsId(QTimeZone::systemTimeZoneId());
    if (zone.isEmpty()) {
        LSW_WARN("upkeep: Windows has no name for the host's time zone {}; its clock is left",
                 QTimeZone::systemTimeZoneId().toStdString());
        return result;
    }
    const auto clock = rpc_.request(
        linkproto::cmd::kSetClock,
        {{"time_zone", zone.toStdString()}, {"utc_ms", QDateTime::currentMSecsSinceEpoch()}});
    if (!clock.ok) {
        result.ok = false;
        result.error = QStringLiteral("Windows' clock was not set: %1")
                           .arg(QString::fromStdString(clock.error));
        LSW_WARN("upkeep: {}", result.error.toStdString());
        return result;
    }
    LSW_INFO("upkeep: Windows' time zone {} (was {}), its clock {} ms off{}",
             clock.body.value("time_zone", std::string{}),
             clock.body.value("time_zone_was", std::string{}),
             clock.body.value("clock_off_ms", 0LL),
             clock.body.value("clock_set", false) ? ", set to the host's" : "");
    return result;
}

}  // namespace lsw::host::services
