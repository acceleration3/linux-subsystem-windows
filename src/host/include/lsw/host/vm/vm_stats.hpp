#pragma once

#include <QtGlobal>

namespace lsw::host::vm {
struct VmStats {
    bool running = false;
    bool agent_reachable = false;
    int cpu_percent = 0;
    quint64 memory_used_bytes = 0;
    quint64 memory_total_bytes = 0;
    quint64 disk_used_bytes = 0;
    quint64 disk_total_bytes = 0;
    quint64 net_rx_bytes = 0;
    quint64 net_tx_bytes = 0;
};

}
