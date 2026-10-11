#include "lsw/host/services/guest_upkeep.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/agent_redeployer.hpp"
#include "lsw/host/services/guest_swap_script.hpp"
#include "lsw/host/services/status_hub.hpp"

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
        if (!result.attempted.isEmpty()) {
            attempted_.insert(result.attempted);
        }
        error_ = result.error;
        set_state(result.ok ? State::Current : State::Failed);
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
        return;
    }
    if (!status_.vm().running()) {
        passed_ = false;
        set_state(State::Idle);
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
    set_state(State::Checking);
    error_.clear();
    watcher_.setFuture(
        QtConcurrent::run([this, attempted = attempted_] { return pass(attempted); }));
}

void GuestUpkeep::set_state(State state) {
    if (state == state_) {
        return;
    }
    state_ = state;
    Q_EMIT changed();
}

GuestUpkeep::Result GuestUpkeep::pass(QSet<QString> attempted) {
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

    // 2. The time zone and the clock, as the host's.
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
