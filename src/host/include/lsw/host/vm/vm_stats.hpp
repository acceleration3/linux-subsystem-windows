#pragma once

#include <QtGlobal>

namespace lsw::host::vm {

// One sample of a VM's health, as the UI wants to render it. Half comes
// from libvirt (is the domain up, host CPU time, net counters), half
// from inside the guest via an IGuestStatsProbe.
struct VmStats {
    bool running = false;            // libvirt domain state (qemu process up)
    bool agent_reachable = false;    // last in-guest sample answered recently
    int cpu_percent = 0;             // 0..100, from the guest
    quint64 memory_used_bytes = 0;   // total - avail (from the guest)
    quint64 memory_total_bytes = 0;
    quint64 disk_used_bytes = 0;     // total - avail on C:\ (from the guest)
    quint64 disk_total_bytes = 0;
    quint64 vram_used_bytes = 0;     // sum of VkDeviceMemory from render-server
    quint64 net_rx_bytes = 0;
    quint64 net_tx_bytes = 0;
};

}  // namespace lsw::host::vm
