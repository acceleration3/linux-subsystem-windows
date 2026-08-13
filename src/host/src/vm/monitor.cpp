#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/monitor.hpp"

#include "lsw/base/log.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent>
#include <libvirt/libvirt.h>

#include <algorithm>
#include <memory>

namespace lsw::host::vm {

namespace {

[[nodiscard]] QByteArray uuid_bytes(const QUuid& u) {
    return u.toRfc4122();
}

}  // namespace

Monitor::Monitor(LibvirtConnection& conn, QObject* parent) : QObject(parent), conn_(conn) {
    timer_.setInterval(2000);
    connect(&timer_, &QTimer::timeout, this, &Monitor::on_tick);
}

void Monitor::set_guest_probe(IGuestStatsProbe* probe) {
    probe_ = probe;
}

void Monitor::set_interval_ms(int ms) {
    timer_.setInterval(ms);
}

void Monitor::watch(const QUuid& uuid) {
    if (!stats_.contains(uuid)) {
        stats_.insert(uuid, VmStats{});
    }
    if (!agent_poll_in_flight_.contains(uuid)) {
        agent_poll_in_flight_.insert(uuid, std::make_shared<std::atomic<bool>>(false));
    }
    if (!timer_.isActive()) {
        timer_.start();
    }
}

void Monitor::unwatch(const QUuid& uuid) {
    stats_.remove(uuid);
    last_cpu_ns_.remove(uuid);
    last_sample_ms_.remove(uuid);
    agent_poll_in_flight_.remove(uuid);
    last_agent_ms_.remove(uuid);
    if (stats_.isEmpty()) {
        timer_.stop();
    }
}

VmStats Monitor::latest(const QUuid& uuid) const {
    return stats_.value(uuid, VmStats{});
}

void Monitor::update_vram(const QUuid& uuid, quint64 bytes) {
    auto& s = stats_[uuid];
    s.vram_used_bytes = bytes;
    Q_EMIT stats_updated(uuid, s);
}

void Monitor::on_tick() {
    // Copy the key list before iterating — poll_one may emit signals
    // that reach back into the monitor (unlikely, but safer).
    const auto keys = stats_.keys();
    for (const auto& k : keys) {
        poll_one(k);
        kick_agent_poll(k);
    }
}

void Monitor::poll_one(const QUuid& uuid) {
    if (!conn_.is_open()) {
        return;
    }
    const auto bytes = uuid_bytes(uuid);
    auto* d = ::virDomainLookupByUUID(conn_.raw(),
                                      reinterpret_cast<const unsigned char*>(bytes.constData()));
    if (d == nullptr) {
        return;
    }
    DomainHandle dom{d};

    VmStats s = stats_.value(uuid);

    virDomainInfo info{};
    if (::virDomainGetInfo(dom.raw(), &info) == 0) {
        s.running = info.state == VIR_DOMAIN_RUNNING;
        // Fallback CPU% from libvirt cpu_time delta — overwritten by
        // the agent poll when it lands, but useful before the agent
        // is reachable and as a sanity check.
        const quint64 now_ns = info.cpuTime;
        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        const auto last_ns = last_cpu_ns_.value(uuid, 0);
        const auto last_ms = last_sample_ms_.value(uuid, now_ms);
        if (last_ns != 0 && now_ms > last_ms) {
            const quint64 elapsed_ns = (now_ms - last_ms) * 1'000'000ULL;
            const quint64 delta = now_ns > last_ns ? now_ns - last_ns : 0;
            const int vcpus = info.nrVirtCpu > 0 ? info.nrVirtCpu : 1;
            const auto fallback_pct =
                static_cast<int>(std::min<quint64>(100, (delta * 100ULL) / (elapsed_ns * vcpus)));
            // Only adopt the libvirt number if we don't have a fresh
            // agent value yet (the agent path is preferred — it
            // reflects what's actually busy inside Windows rather
            // than the qemu process's host-side CPU footprint).
            if (s.cpu_percent == 0) {
                s.cpu_percent = fallback_pct;
            }
        }
        last_cpu_ns_[uuid] = now_ns;
        last_sample_ms_[uuid] = now_ms;
        // libvirt's ``info.memory`` is "memory available to the
        // domain" — without virtio-balloon that equals maxMem, so
        // it's useless as a "used" gauge. Keep maxMem as the total
        // and leave the agent poll to fill in real usage.
        if (s.memory_total_bytes == 0) {
            s.memory_total_bytes = info.maxMem * 1024ULL;
        }
    }

    // Agent reachability — derived from the recency of the last
    // successful system_info reply. The actual poll is kicked off in
    // ``kick_agent_poll`` (async); here we just decide whether the
    // stored timestamp is fresh enough to call the agent "alive".
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    const auto last_agent = last_agent_ms_.value(uuid, 0);
    s.agent_reachable = (last_agent != 0) && (now_ms - last_agent <= kAgentStaleMs);

    stats_[uuid] = s;
    Q_EMIT stats_updated(uuid, s);
}

void Monitor::kick_agent_poll(const QUuid& uuid) {
    if (probe_ == nullptr || !probe_->available()) {
        return;
    }
    const auto it = agent_poll_in_flight_.constFind(uuid);
    if (it == agent_poll_in_flight_.constEnd()) {
        return;
    }
    auto in_flight = it.value();
    bool expected = false;
    if (!in_flight->compare_exchange_strong(expected, true)) {
        return;  // previous poll still pending — skip this tick
    }

    auto* probe = probe_;
    const QPointer<Monitor> self{this};
    (void)QtConcurrent::run([self, probe, uuid, in_flight]() {
        // ``sample`` blocks on the worker; the GUI thread keeps serving
        // paints and clicks.
        const GuestStatsSample g = probe->sample();
        in_flight->store(false);
        if (!g.valid || !self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self, uuid, g]() {
            if (!self) return;
            if (!self->stats_.contains(uuid)) return;  // unwatched while in flight
            VmStats s = self->stats_.value(uuid);
            if (g.cpu_percent >= 0) {
                s.cpu_percent = std::clamp(g.cpu_percent, 0, 100);
            }
            if (g.mem_total > 0) {
                s.memory_total_bytes = g.mem_total;
                s.memory_used_bytes = g.mem_avail <= g.mem_total ? g.mem_total - g.mem_avail : 0;
            }
            if (g.disk_total > 0) {
                s.disk_total_bytes = g.disk_total;
                s.disk_used_bytes = g.disk_avail <= g.disk_total ? g.disk_total - g.disk_avail : 0;
            }
            // A successful merge is the heartbeat — record it so poll_one
            // can flip ``agent_reachable`` back off if we go silent for
            // ``kAgentStaleMs``.
            self->last_agent_ms_[uuid] = QDateTime::currentMSecsSinceEpoch();
            s.agent_reachable = true;
            self->stats_[uuid] = s;
            Q_EMIT self->stats_updated(uuid, s);
        }, Qt::QueuedConnection);
    });
}

}  // namespace lsw::host::vm
