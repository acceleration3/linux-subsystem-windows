#include "lsw/host/vm/monitor.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/vm/domain_handle.hpp"

#include <QDateTime>
#include <QtConcurrent>
#include <algorithm>
#include <libvirt/libvirt.h>
#include <memory>

namespace lsw::host::vm {

Monitor::Monitor(LibvirtConnection& conn, QObject* parent) : QObject(parent), conn_(conn) {
    timer_.setInterval(2000);
    connect(&timer_, &QTimer::timeout, this, &Monitor::on_tick);
}

void Monitor::set_guest_probe(IGuestStatsProbe* probe) {
    probe_ = probe;
}

void Monitor::watch(const QUuid& uuid) {
    LSW_DEBUG("monitor: watching {}", uuid.toString(QUuid::WithoutBraces).toStdString());
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
    LSW_DEBUG("monitor: no longer watching {}", uuid.toString(QUuid::WithoutBraces).toStdString());
    stats_.remove(uuid);
    domain_ids_.remove(uuid);
    last_cpu_ns_.remove(uuid);
    last_sample_ms_.remove(uuid);
    agent_poll_in_flight_.remove(uuid);
    last_agent_ms_.remove(uuid);
    if (stats_.isEmpty()) {
        timer_.stop();
    }
}

void Monitor::poll_now(const QUuid& uuid) {
    if (stats_.contains(uuid)) {
        poll_one(uuid);
        kick_agent_poll(uuid);
    }
}

VmStats Monitor::latest(const QUuid& uuid) const {
    return stats_.value(uuid, VmStats{});
}

void Monitor::on_tick() {
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
    const DomainHandle dom = DomainHandle::lookup(conn_.raw(), uuid);
    if (!dom.valid()) {
        LSW_TRACE("monitor: domain {} not found", uuid.toString(QUuid::WithoutBraces).toStdString());
        return;
    }

    VmStats s = stats_.value(uuid);

    virDomainInfo info{};
    if (::virDomainGetInfo(dom.raw(), &info) != 0) {
        LSW_TRACE("monitor: virDomainGetInfo failed");
        return;
    }
    s.running = info.state == VIR_DOMAIN_RUNNING;
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (!s.running) {
        report_stopped(uuid);
        return;
    }

    const unsigned int domain_id = ::virDomainGetID(dom.raw());
    const auto seen_id = domain_ids_.constFind(uuid);
    if (seen_id != domain_ids_.constEnd() && *seen_id != domain_id) {
        LSW_DEBUG("monitor: domain id changed from {} to {}; the VM restarted", *seen_id, domain_id);
        report_stopped(uuid);
        s = VmStats{};
        s.running = true;
    }
    domain_ids_[uuid] = domain_id;

    const auto last_agent = last_agent_ms_.value(uuid, 0);
    s.agent_reachable = (last_agent != 0) && (now_ms - last_agent <= kAgentStaleMs);

    const quint64 now_ns = info.cpuTime;
    const auto last_ns = last_cpu_ns_.value(uuid, 0);
    const auto last_ms = last_sample_ms_.value(uuid, now_ms);
    if (!s.agent_reachable) {
        s.cpu_percent = 0;
        if (last_ns != 0 && now_ms > last_ms) {
            const auto elapsed_ns = static_cast<quint64>(now_ms - last_ms) * 1'000'000ULL;
            const quint64 delta = now_ns > last_ns ? now_ns - last_ns : 0;
            const int vcpus = info.nrVirtCpu > 0 ? info.nrVirtCpu : 1;
            s.cpu_percent = static_cast<int>(std::min<quint64>(
                100, (delta * 100ULL) / (elapsed_ns * static_cast<quint64>(vcpus))));
        }
        s.memory_used_bytes = 0;
        s.memory_total_bytes = info.maxMem * 1024ULL;
        s.disk_used_bytes = 0;
        s.disk_total_bytes = 0;
    }
    last_cpu_ns_[uuid] = now_ns;
    last_sample_ms_[uuid] = now_ms;

    stats_[uuid] = s;
    Q_EMIT stats_updated(uuid, s);
}

void Monitor::report_stopped(const QUuid& uuid) {
    const VmStats s{};
    domain_ids_.remove(uuid);
    last_cpu_ns_.remove(uuid);
    last_sample_ms_.remove(uuid);
    last_agent_ms_.remove(uuid);
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
    const auto& in_flight = it.value();
    bool expected = false;
    if (!in_flight->compare_exchange_strong(expected, true)) {
        return;
    }

    (void)QtConcurrent::run([probe = probe_, in_flight] {
        const GuestStatsSample g = probe->sample();
        in_flight->store(false);
        return g;
    }).then(this, [this, uuid](const GuestStatsSample& g) {
        if (!g.valid || !stats_.contains(uuid)) {
            return;
        }
        VmStats s = stats_.value(uuid);
        if (!s.running) {
            return;
        }
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
        last_agent_ms_[uuid] = QDateTime::currentMSecsSinceEpoch();
        s.agent_reachable = true;
        stats_[uuid] = s;
        Q_EMIT stats_updated(uuid, s);
    });
}

}
