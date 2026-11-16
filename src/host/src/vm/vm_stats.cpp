#include "lsw/host/vm/vm_stats.hpp"

namespace lsw::host::vm {

void VmStats::set_guest_memory(quint64 total, quint64 available, quint64 ballooned) noexcept {
    memory_total_bytes = total;
    const quint64 taken = available <= total ? total - available : 0;
    memory_used_bytes = taken > ballooned ? taken - ballooned : 0;
}

}
