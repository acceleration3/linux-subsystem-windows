#pragma once

#include "lsw/host/platform/host_gpus.hpp"

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace lsw::host::provision::install {

// The C:\av\adapters.txt the payload carries: what each of this host's
// GPUs is called, which accel-virt's provision.ps1 names the INF model
// it writes for that GPU with — and so what Device Manager calls the
// adapter. One "<vendor>\t<device>\t<name>\n" line per GPU, the IDs in
// lowercase hex, CPU devices left out: the format of accel-virt's
// tools/host/list_gpus.py --adapter-names.
//
// Written when the payload is built, on the machine the guest will run
// on. accel-virt's own install writes one too, but it describes the
// machine that built the driver package, which for a packaged release
// is nobody's.
//
// The rules are the device's (virtio-gpu-av-host.c), so the INF model
// and the name the device reports agree:
//
//   * The IDs are those of the GPU's PCI function, read from sysfs —
//     the ones the device presents to the guest. A GPU without one
//     keeps virtio-gpu's own, 1af4:1050.
//   * The name is AMD's product table (libdrm's amdgpu.ids) by device
//     and revision first. Then, for an integrated GPU, which RADV names
//     after its processor: an AMD one is "AMD Radeon(TM) Graphics", as
//     AMD's Windows driver calls those its table leaves out; another
//     vendor's is the PCI ID database's product name after the vendor's,
//     not repeating it. Else the Vulkan deviceName without the driver's
//     parenthesised suffix (HostGpus::adapter_name). Cut to the 47 bytes
//     the device's capset carries, never inside a UTF-8 character.
class AdapterNames {
public:
    // Where the lookups read from. The defaults are the system's; a
    // test points them at files of its own.
    struct Sources {
        QString sysfs_pci = QStringLiteral("/sys/bus/pci/devices");
        QString amdgpu_ids = QStringLiteral("/usr/share/libdrm/amdgpu.ids");
        QStringList pci_ids = {QStringLiteral("/usr/share/hwdata/pci.ids"),
                               QStringLiteral("/usr/share/misc/pci.ids")};
    };

    AdapterNames();
    explicit AdapterNames(Sources sources);

    // The file's contents for ``gpus``; empty when none is a GPU, which
    // leaves provision.ps1 naming each model as the INF's static one.
    [[nodiscard]] QString render(const QVector<platform::HostGpu>& gpus) const;

    // The name one GPU's adapter is given.
    [[nodiscard]] QString name(const platform::HostGpu& gpu) const;

private:
    struct PciIdentity {
        quint32 vendor = 0x1af4;
        quint32 device = 0x1050;
        int revision = -1;  // unknown
    };

    [[nodiscard]] PciIdentity identity(const platform::HostGpu& gpu) const;
    // ``attribute`` of ``address`` in sysfs, a "0x..." hex number no
    // larger than ``limit``; nullopt when unreadable or out of range.
    [[nodiscard]] std::optional<quint32> sysfs_number(const QString& address,
                                                      const char* attribute, quint32 limit) const;
    [[nodiscard]] std::optional<QString> amdgpu_name(const PciIdentity& id) const;
    [[nodiscard]] std::optional<QString> pci_ids_name(const PciIdentity& id) const;
    // ``name`` cut to the capset's field at a character boundary.
    [[nodiscard]] static QString fitted(const QString& name);

    Sources sources_;
};

}  // namespace lsw::host::provision::install
