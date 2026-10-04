#pragma once

#include <algorithm>
#include <thread>

namespace lsw::host::config {

// Sizing knobs the wizard captures and the domain XML consumes. Defaults
// are sensible for a Win11 install on a modern desktop.
struct VmSizing {
    // Every thread the host has, by default: the guest is where the work
    // happens, and a vCPU that is idle costs the host next to nothing.
    [[nodiscard]] static int host_cores() {
        return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    }

    int cpu_cores = host_cores();  // 1 .. host_threads
    int memory_mib = 8192;  // 2048 .. 90% of host RAM
    int disk_gib = 60;      // 30 .. host free space - margin
};

}  // namespace lsw::host::config
