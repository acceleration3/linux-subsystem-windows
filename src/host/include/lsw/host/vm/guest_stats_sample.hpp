#pragma once

#include <QtGlobal>

namespace lsw::host::vm {

// What one in-guest sample carries. Raw numbers exactly as the guest
// reports them; Monitor is what turns them into VmStats deltas.
struct GuestStatsSample {
    bool valid = false;
    int cpu_percent = -1;      // <0 means "not reported"
    quint64 mem_total = 0;
    quint64 mem_avail = 0;
    quint64 disk_total = 0;
    quint64 disk_avail = 0;
};

}  // namespace lsw::host::vm
