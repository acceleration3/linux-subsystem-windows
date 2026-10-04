#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {

// How much of a host GPU's VRAM is in use, as its kernel driver counts
// it: everything on that GPU, the host desktop included. What the guest
// allocates lives there, so this is the overview's VRAM gauge.
//
// Read from sysfs where the driver publishes it: amdgpu's
// mem_info_vram_used and mem_info_vram_total under the card's device
// directory. Other drivers publish nothing there (NVIDIA would need
// NVML), and the overview hides the row.
class GpuMemoryUsage {
public:
    struct Reading {
        quint64 used_bytes = 0;
        quint64 total_bytes = 0;
    };

    GpuMemoryUsage() = delete;

    // The reading for the GPU at ``pci_address`` ("0000:03:00.0", as
    // HostGpu::pci_address has it), or nullopt when the address is empty,
    // no DRM card is that device, or its driver publishes no counters.
    [[nodiscard]] static std::optional<Reading> read(const QString& pci_address);
};

}  // namespace lsw::host::platform
