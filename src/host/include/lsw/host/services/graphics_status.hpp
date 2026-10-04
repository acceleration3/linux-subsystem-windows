#pragma once

#include "lsw/host/platform/gpu_memory_usage.hpp"
#include "lsw/host/platform/host_gpus.hpp"
#include "lsw/host/platform/host_graphics_probe.hpp"
#include "lsw/host/services/guest_gpu_status.hpp"
#include "lsw/host/vm/avgpu_log.hpp"

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace lsw::host::services {

// One host GPU passed to the guest, and Windows' side of it.
struct GraphicsAdapter {
    platform::HostGpu gpu;
    bool display = false;
    // The guest adapter that is this GPU, once the agent has said.
    std::optional<GuestGpuStatus::Adapter> guest;
};

// What the settings will change at the next restart, against the
// definition the machine runs (or, while it is off, will boot). Each is
// unset, or false, when the two agree.
struct PendingGraphics {
    std::optional<int> host_window_mib;
    // The display GPU or the render-only set differs.
    bool adapters = false;

    [[nodiscard]] bool any() const noexcept { return host_window_mib.has_value() || adapters; }
};

// Everything about the guest's graphics StatusHub last saw: the host's
// GPUs and what it can serve, the GPUs the machine's definition passes
// and Windows' side of each, the device's live state, and the driver's
// own log.
struct GraphicsStatus {
    // ---- the host ------------------------------------------------
    // False until the first enumeration and API probe have answered.
    bool probed = false;
    QVector<platform::HostGpu> host_gpus;
    QVector<platform::HostApi> apis;
    // The display GPU's VRAM in use, whole device; unset where its
    // driver does not say.
    std::optional<platform::GpuMemoryUsage::Reading> vram;

    // ---- the GPUs passed, display first ---------------------------
    // As the domain definition has them (VmStatus::definition): the
    // running VM's devices, or the ones the next start creates. The
    // settings stand in only when there is no definition to read.
    QVector<GraphicsAdapter> adapters;
    PendingGraphics pending;

    // ---- the device ----------------------------------------------
    // Its ``modes`` as QEMU holds it now; unset while the VM is off or
    // QEMU did not say.
    std::optional<QString> live_modes;

    // ---- the guest -----------------------------------------------
    GuestGpuStatus guest;

    // ---- the driver's log ----------------------------------------
    QString log_path;
    QStringList log_lines;  // the last ones, oldest first
    qint64 log_line_count = 0;
    vm::AvgpuLog::Facts log_facts;
};

}  // namespace lsw::host::services
