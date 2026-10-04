#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::vm {

// One host folder the guest can see, as it reaches the domain XML.
// Deliberately not the same type as the settings' HostShare: this
// layer takes a tag and a path and knows nothing about drive letters,
// which are a guest-side idea.
struct DomainShare {
    QString tag;  // what the guest mounts by
    QString host_path;
    bool read_only = false;
};

// Per-spec extras the manager needs to thread into the XML on top of
// the persisted ``VmRecord``: paths to OVMF firmware, shared folders,
// the GPU device. Defaults match a stock Arch / Fedora / Debian setup.
struct DomainXmlInputs {
    QString ovmf_code = "/usr/share/edk2/x64/OVMF_CODE.4m.fd";
    QString ovmf_vars_template = "/usr/share/edk2/x64/OVMF_VARS.4m.fd";
    // Folders on this machine the guest can see, as virtiofs devices.
    // They need virtiofsd on the host and the guest's RAM shared, and
    // the RAM is shared regardless now (the GPU needs it too), so a
    // share costs only its own device.
    QVector<DomainShare> shares;

    // ---- The GPU: accel-virt's virtio-gpu-av-pci ----------------------

    // The first definition, made while Windows Setup runs, also gets a
    // plain VGA adapter as the primary display. Until avgpu.sys is
    // installed nothing gives the GPU device a firmware framebuffer, so
    // Setup and the install console would be black. Every later
    // definition leaves it out: a second display adapter takes the
    // desktop from the real one.
    bool install_display = false;
    // The display adapter's host GPU: a Vulkan device UUID, or empty for
    // the device's own "auto".
    QString host_gpu_uuid;
    // One render-only adapter (display=off) per UUID, for the host's
    // other GPUs. Frames drawn on one are shown through the display
    // adapter.
    QStringList render_only_gpu_uuids;
    // The device's host window, ``hostmem=``, in MiB.
    int host_window_mib = 8192;
    // The resolutions the display offers, the device's ``modes``: WxH
    // entries between commas, the first preferred. Empty is the host
    // monitors' own modes.
    QString display_modes;
    // The file the driver's log (I/O port 0x402) is written to. Empty
    // leaves the debug console out.
    QString avgpu_log;

    // accel-virt's host half. Empty is "look it up": Lifecycle fills
    // each from platform::AccelVirtPaths before it defines the domain,
    // so only a caller that wants a specific build sets them.
    QString qemu_binary;      // <emulator>
    QString qemu_module_dir;  // QEMU_MODULE_DIR
    QString qemu_data_dir;    // -L, for an installed QEMU; empty leaves it out
    QString executor;         // executor=
    QString d3d_lib_dir;      // AV_D3D_PATH
};

}  // namespace lsw::host::vm
