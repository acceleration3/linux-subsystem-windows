#pragma once

#include "lsw/host/vm/guest_stats_probe.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/vm_stats.hpp"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QUuid>
#include <atomic>

namespace lsw::host::vm {
class Monitor : public QObject {
    Q_OBJECT
public:
    explicit Monitor(LibvirtConnection& conn, QObject* parent = nullptr);

    void set_guest_probe(IGuestStatsProbe* probe);

    void watch(const QUuid& uuid);
    void unwatch(const QUuid& uuid);
    void poll_now(const QUuid& uuid);

    [[nodiscard]] VmStats latest(const QUuid& uuid) const;

Q_SIGNALS:
    void stats_updated(const QUuid& uuid, const VmStats& stats);

private Q_SLOTS:
    void on_tick();

private:
    void poll_one(const QUuid& uuid);
    void report_stopped(const QUuid& uuid);
    void kick_agent_poll(const QUuid& uuid);

    LibvirtConnection& conn_;
    IGuestStatsProbe* probe_ = nullptr;
    QTimer timer_;
    QHash<QUuid, VmStats> stats_;
    QHash<QUuid, unsigned int> domain_ids_;
    QHash<QUuid, quint64> last_cpu_ns_;
    QHash<QUuid, qint64> last_sample_ms_;
    QHash<QUuid, std::shared_ptr<std::atomic<bool>>> agent_poll_in_flight_;
    QHash<QUuid, qint64> last_agent_ms_;
    static constexpr qint64 kAgentStaleMs = 6000;
};

}
