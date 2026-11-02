#pragma once

#include <algorithm>
#include <thread>

namespace lsw::host::config {
struct VmSizing {
    [[nodiscard]] static int host_cores() {
        return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    }

    int cpu_cores = host_cores();
    int memory_mib = 8192;
    int disk_gib = 60;
};

}
