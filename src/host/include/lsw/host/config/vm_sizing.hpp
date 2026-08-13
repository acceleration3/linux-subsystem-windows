#pragma once

namespace lsw::host::config {

// Sizing knobs the wizard captures and the domain XML consumes. Defaults
// are sensible for a Win11 install on a modern desktop.
struct VmSizing {
    int cpu_cores = 4;      // 1 .. host_threads
    int memory_mib = 8192;  // 2048 .. 90% of host RAM
    int disk_gib = 60;      // 30 .. host free space - margin
};

}  // namespace lsw::host::config
