#pragma once

#include "lsw/manager/vm/libvirt_conn.hpp"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QUuid>

#include <atomic>

namespace lsw::manager::vm {

class AgentRpcClient;

// Live stats for one running VM. CPU%, memory used/total and disk
// used/total come from the in-guest agent (``system_info`` RPC); the
// render-server (when wired up) fills in ``vram``; libvirt is the
// fallback when the agent isn't reachable yet.
struct VmStats {
    bool running = false;            // libvirt domain state (qemu process up)
    bool agent_reachable = false;    // last in-guest system_info answered recently
    int cpu_percent = 0;            // 0..100, from agent (GetSystemTimes)
    quint64 memory_used_bytes = 0;  // total - avail (from agent)
    quint64 memory_total_bytes = 0;
    quint64 disk_used_bytes = 0;    // total - avail on C:\ (from agent)
    quint64 disk_total_bytes = 0;
    quint64 vram_used_bytes = 0;    // sum of VkDeviceMemory from render-server
    quint64 net_rx_bytes = 0;
    quint64 net_tx_bytes = 0;
};

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
    void set_rpc(AgentRpcClient* rpc);

    void watch(const QUuid& uuid);
    void unwatch(const QUuid& uuid);

    [[nodiscard]] VmStats latest(const QUuid& uuid) const;

    // Optional hook from the render-server: per-VM VRAM tally.
    void update_vram(const QUuid& uuid, quint64 bytes);

    void set_interval_ms(int ms);

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
    AgentRpcClient* rpc_ = nullptr;  // non-owning; may be nullptr
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

}  // namespace lsw::manager::vm
