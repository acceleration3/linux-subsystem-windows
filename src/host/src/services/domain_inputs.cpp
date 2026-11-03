#include "lsw/host/services/domain_inputs.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/display_refresh.hpp"
#include "lsw/host/config/log_verbosity.hpp"
#include "lsw/host/platform/host_gpus.hpp"
#include "lsw/host/services/host_share_layout.hpp"

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

}

QString display_modes_from_settings(const config::ManagerSettings& manager_settings,
                                    const std::optional<config::DisplayMode>& output) {
    const bool match = manager_settings.bool_value(K::kMatchHostOutput);
    return config::DisplayModes::property(config::DisplayModes::effective(
        manager_settings.string_list_value(K::kDisplayModes),
        match ? output : std::nullopt));
}

config::VmSizing vm_sizing_from_settings(const config::ManagerSettings& manager_settings) {
    config::VmSizing sizing;
    sizing.cpu_cores = manager_settings.int_value(K::kCpuCores);
    sizing.memory_mib = manager_settings.int_value(K::kMemoryMib);
    sizing.disk_gib = manager_settings.int_value(K::kDiskGib);
    return sizing;
}

std::optional<quint32> display_refresh_from_settings(const config::ManagerSettings& manager_settings,
                                                     std::optional<quint32> monitor) {
    return config::DisplayRefresh::resolve(manager_settings.string_value(K::kDisplayRefresh), monitor);
}

vm::DomainXmlInputs domain_inputs_from_settings(const config::Settings& settings,
                                                const config::ManagerSettings& manager_settings,
                                                std::optional<quint32> monitor_refresh_millihz) {
    vm::DomainXmlInputs inputs;
    inputs.shares = HostShareLayout::assign(manager_settings.host_shares());

    inputs.host_window_mib = manager_settings.int_value(K::kHostWindowMib);
    inputs.display_modes = display_modes_from_settings(manager_settings);
    inputs.display_refresh_millihz =
        display_refresh_from_settings(manager_settings, monitor_refresh_millihz).value_or(0);
    inputs.avgpu_log = settings.vm_avgpu_log_path();
    const QString gpu_level =
        config::LogVerbosity::effective(manager_settings, config::LogVerbosity::Part::Gpu);
    inputs.dxvk_log_level = config::LogVerbosity::dxvk_level(gpu_level);
    inputs.vkd3d_debug = config::LogVerbosity::vkd3d_level(gpu_level);

    const QVector<platform::HostGpu> gpus = platform::HostGpus::enumerate();
    const QString setting = manager_settings.string_value(K::kHostGpu).trimmed();
    const bool is_auto = setting.isEmpty() || setting == QLatin1StringView("auto");
    std::optional<platform::HostGpu> chosen = platform::HostGpus::resolve(gpus, setting);
    if (!is_auto) {
        if (chosen.has_value() || gpus.isEmpty()) {
            inputs.host_gpu_uuid = setting.toLower();
        } else {
            LSW_WARN("host GPU {} is not on this host any more; using auto", setting.toStdString());
            chosen = platform::HostGpus::resolve(gpus, QStringLiteral("auto"));
        }
    }

    const QString display_uuid = chosen.has_value() ? chosen->uuid : inputs.host_gpu_uuid;
    for (const QString& entry : manager_settings.string_list_value(K::kRenderGpus)) {
        const QString uuid = entry.trimmed().toLower();
        if (uuid.isEmpty() || inputs.render_only_gpu_uuids.contains(uuid)
            || uuid.compare(display_uuid, Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (!gpus.isEmpty() && !platform::HostGpus::resolve(gpus, uuid).has_value()) {
            LSW_WARN("render GPU {} is not on this host any more; leaving it out",
                     uuid.toStdString());
            continue;
        }
        inputs.render_only_gpu_uuids << uuid;
    }
    return inputs;
}

}
