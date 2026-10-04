#include "lsw/host/services/domain_inputs.hpp"

#include <QtGlobal>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

constexpr int kMinVideoVramKib = 16384;

}  // namespace

int video_vram_kib_for(int width, int height) {
    const int needed_kib = ((width * height * 4 * 2) / 1024) + 1024;
    return qMax(kMinVideoVramKib, needed_kib);
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
    inputs.video_vram_kib = video_vram_kib_for(manager_settings.int_value(K::kGuestWidth),
                                               manager_settings.int_value(K::kGuestHeight));
    for (const auto& share : manager_settings.host_shares()) {
        vm::DomainShare mapped;
        // The tag is what the guest mounts by, and a drive letter is a
        // guest-side idea, so the letter is the tag: "Z:" becomes "z".
        mapped.tag = share.drive.left(1).toLower();
        mapped.host_path = share.host_path;
        mapped.read_only = share.read_only;
        inputs.shares.push_back(mapped);
    }
    return inputs;
}

}  // namespace lsw::host::services
