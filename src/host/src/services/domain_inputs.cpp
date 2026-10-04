#include "lsw/host/services/domain_inputs.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/host_gpus.hpp"

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

}  // namespace

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

vm::DomainXmlInputs domain_inputs_from_settings(const config::Settings& settings,
                                                const config::ManagerSettings& manager_settings) {
    vm::DomainXmlInputs inputs;
    // The template names all three CDs, so the definition has to carry
    // them even for a guest that has already ejected the media: an
    // empty source is a definition libvirt will not take. The Windows
    // and payload paths live on the record; virtio-win is where the
    // pipeline put it.
    inputs.virtio_iso = settings.virtio_iso_path();
    for (const auto& share : manager_settings.host_shares()) {
        vm::DomainShare mapped;
        // The tag is what the guest mounts by, and a drive letter is a
        // guest-side idea, so the letter is the tag: "Z:" becomes "z".
        mapped.tag = share.drive.left(1).toLower();
        mapped.host_path = share.host_path;
        mapped.read_only = share.read_only;
        inputs.shares.push_back(mapped);
    }

    inputs.host_window_mib = manager_settings.int_value(K::kHostWindowMib);
    inputs.isolate_renderer = manager_settings.bool_value(K::kIsolateRenderer);
    inputs.display_modes = display_modes_from_settings(manager_settings);
    inputs.avgpu_log = settings.vm_avgpu_log_path();

    // The host GPU. A UUID is checked against what is plugged in now: a
    // GPU that was removed, or whose driver changed its UUID, would make
    // the device refuse to realize and the VM refuse to start, over a
    // setting the user made long ago. "auto" is a better answer than no
    // VM, and the log says why it was taken.
    //
    // When nothing enumerates here (no Vulkan loader in the manager's
    // environment) the setting is passed through as it is: the device
    // does its own enumeration, and not being able to check is not
    // evidence the GPU is gone.
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

    // The GPUs chosen as render-only adapters, each once. The display
    // GPU is never one of them too: two devices bound to one GPU would be
    // two adapters Windows lists for the same card. A GPU no longer on
    // the host is left out for the same reason a missing display GPU is
    // replaced: a device that cannot realize is a VM that does not start.
    // When nothing enumerates, the list is passed through as it is.
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

}  // namespace lsw::host::services
