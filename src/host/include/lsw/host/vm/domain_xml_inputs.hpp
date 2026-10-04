#pragma once

#include "lsw/linkproto/region_layout.hpp"

#include <QString>
#include <QVector>

namespace lsw::host::vm {

// Per-spec extras the manager needs to thread into the XML on top of
// the persisted ``VmRecord`` — paths to OVMF firmware, the virtio-win
// driver ISO, etc. Defaults match a stock Arch / Fedora / Debian setup;
// the wizard exposes them when auto-detection fails.
// One host folder the guest can see, as it reaches the domain XML.
// Deliberately not the same type as the settings' HostShare: this
// layer takes a tag and a path and knows nothing about drive letters,
// which are a guest-side idea.
struct DomainShare {
    QString tag;  // what the guest mounts by
    QString host_path;
    bool read_only = false;
};

struct DomainXmlInputs {
    QString ovmf_code = "/usr/share/edk2/x64/OVMF_CODE.4m.fd";
    QString ovmf_vars_template = "/usr/share/edk2/x64/OVMF_VARS.4m.fd";
    // Path to virtio-win.iso. Always mounted as a CD in domain.xml;
    // the autounattend's PnP customizations inject viostor/vioscsi/
    // NetKVM from it so Windows installs straight onto virtio-blk.
    QString virtio_iso;
    // IVSHMEM-doorbell chardev path the manager's ``IvshmemServer``
    // listens on. Must exist before ``virDomainCreate`` or libvirt
    // fails the start. Filled by ``Lifecycle::define_and_start``.
    QString ivshmem_socket;
    // ShmemRegion size, in MiB. The default is the shared one, so a VM's
    // region and a local loopback's region are laid out identically;
    // caller can override per-VM.
    //
    // 256 fits ~14 simultaneous 1080p mirrors (double-buffered) before
    // the slot allocator starts refusing — it was ~15 before the
    // host-memory arena took its share; see
    // ``kDefaultHostMemChannelBytes`` for that tradeoff.
    //
    // Must stay a power of two: the region backs the ivshmem PCI BAR.
    int ivshmem_size_mib = linkproto::kDefaultRegionMib;

    // Folders on this machine the guest can see, as virtiofs devices.
    //
    // Empty by default and that matters: virtiofs needs the guest's RAM
    // shared, which changes how QEMU backs it, and it needs virtiofsd
    // on the host. A domain with no shares emits neither the shared
    // backing nor the devices, so the ordinary case is byte for byte
    // what it was.
    QVector<DomainShare> shares;

    // How much video memory the emulated adapter has, in KiB. Bounds
    // the modes the guest can set, so it follows the resolution the
    // user asked for rather than capping it at a constant.
    int video_vram_kib = 16384;
};

}  // namespace lsw::host::vm
