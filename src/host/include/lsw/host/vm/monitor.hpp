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

// Live stats for one running VM. CPU%, memory used/total and disk
// used/total come from the in-guest agent (``system_info`` RPC);
// libvirt is the fallback when the agent isn't reachable yet.
// Polls libvirt (and, when wired, the guest agent) for per-VM stats
// and emits ``stats_updated`` on a fixed cadence (default 2 s).
// Stops automatically when no VMs are watched.
class Monitor : public QObject {
    Q_OBJECT
public:
    explicit Monitor(LibvirtConnection& conn, QObject* parent = nullptr);

    // Optional: route per-tick ``system_info`` RPCs through the
    // shared agent client so we get in-guest CPU / RAM / disk
    // numbers. Set once at App construction; we don't expect this to
    // change at runtime.
    // Install the source of in-guest numbers. Non-owning, may be
    // null (libvirt-only stats). Set once at composition time.
    void set_guest_probe(IGuestStatsProbe* probe);

    void watch(const QUuid& uuid);
    void unwatch(const QUuid& uuid);
    // Read ``uuid`` now rather than at the next tick: a newly watched VM
    // is then shown as it is from the start, not as "off" for a tick.
    void poll_now(const QUuid& uuid);

    [[nodiscard]] VmStats latest(const QUuid& uuid) const;

Q_SIGNALS:
    void stats_updated(const QUuid& uuid, const VmStats& stats);

private Q_SLOTS:
    void on_tick();

private:
    void poll_one(const QUuid& uuid);
    // Kick off an async ``system_info`` call on the rpc, then merge
    // the reply into ``stats_[uuid]`` and emit. Runs on a QtConcurrent
    // worker so a slow guest never blocks the GUI thread.
    void kick_agent_poll(const QUuid& uuid);

    LibvirtConnection& conn_;
    IGuestStatsProbe* probe_ = nullptr;  // non-owning; may be null
    QTimer timer_;
    QHash<QUuid, VmStats> stats_;
    // For libvirt-side fallback CPU% we need the delta between samples.
    QHash<QUuid, quint64> last_cpu_ns_;
    QHash<QUuid, qint64> last_sample_ms_;
    // Throttle: at most one in-flight agent poll per VM at a time —
    // an unresponsive agent shouldn't queue an unbounded backlog.
    QHash<QUuid, std::shared_ptr<std::atomic<bool>>> agent_poll_in_flight_;
    // Wall-clock of the last successful agent system_info reply per
    // VM. ``agent_reachable`` flips to false once this is older than
    // ``kAgentStaleMs``.
    QHash<QUuid, qint64> last_agent_ms_;
    static constexpr qint64 kAgentStaleMs = 6000;
};

}  // namespace lsw::host::vm
