#pragma once

#include "lsw/host/platform/gpu_memory_usage.hpp"
#include "lsw/host/platform/host_gpus.hpp"
#include "lsw/host/platform/host_graphics_probe.hpp"
#include "lsw/host/platform/host_monitor_rates.hpp"
#include "lsw/host/services/guest_gpu_status.hpp"
#include "lsw/host/vm/avgpu_log.hpp"

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace lsw::host::services {
struct GraphicsAdapter {
    platform::HostGpu gpu;
    bool display = false;
    std::optional<GuestGpuStatus::Adapter> guest;
};

struct PendingGraphics {
    std::optional<int> host_window_mib;
    bool adapters = false;

    [[nodiscard]] bool any() const noexcept { return host_window_mib.has_value() || adapters; }
};

struct GraphicsStatus {
    bool probed = false;
    QVector<platform::HostGpu> host_gpus;
    QVector<platform::HostApi> apis;
    std::optional<platform::GpuMemoryUsage::Reading> vram;
    QVector<platform::RateChoice> monitor_rates;
    std::optional<quint32> monitor_refresh_millihz;

    QVector<GraphicsAdapter> adapters;
    PendingGraphics pending;

    std::optional<QString> live_modes;
    std::optional<quint32> live_refresh;

    GuestGpuStatus guest;

    QString log_path;
    QStringList log_lines;
    qint64 log_line_count = 0;
    vm::AvgpuLog::Facts log_facts;
};

}
