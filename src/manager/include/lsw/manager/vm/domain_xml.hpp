#pragma once

#include "lsw/manager/config/vm_state.hpp"

#include <QString>

namespace lsw::manager::vm {

// Per-spec extras the manager needs to thread into the XML on top of
// the persisted ``VmRecord`` — paths to OVMF firmware, the virtio-win
// driver ISO, etc. Defaults match a stock Arch / Fedora / Debian setup;
// the wizard exposes them when auto-detection fails.
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
    // ShmemRegion size, in MiB. Default 256 fits ~15 simultaneous
    // 1080p mirrors (double-buffered) before the slot allocator
    // starts refusing. Caller can override per-VM.
    int ivshmem_size_mib = 256;
    // Retained for ABI/source stability only. There is one domain
    // template now (domain.xml, virtio-from-install); this no longer
    // selects anything.
    bool use_virtio = false;
};

// Builder: VmRecord + DomainXmlInputs -> libvirt domain XML string.
// Centralized here so the wizard, the lifecycle layer, and tests all
// agree on the schema.
class DomainXmlBuilder {
public:
    [[nodiscard]] static QString build(const config::VmRecord& vm, const DomainXmlInputs& inputs);

    // Test hook — exposed so unit tests can pin the exact output.
    [[nodiscard]] static QString render_template(const QString& tmpl,
                                                 const QHash<QString, QString>& vars);
};

}  // namespace lsw::manager::vm
