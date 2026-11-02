#pragma once

#include "lsw/host/platform/host_gpus.hpp"

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace lsw::host::services {
class AdapterNames {
public:
    struct Sources {
        QString sysfs_pci = QStringLiteral("/sys/bus/pci/devices");
        QString amdgpu_ids = QStringLiteral("/usr/share/libdrm/amdgpu.ids");
        QStringList pci_ids = {QStringLiteral("/usr/share/hwdata/pci.ids"),
                               QStringLiteral("/usr/share/misc/pci.ids")};
    };

    AdapterNames();
    explicit AdapterNames(Sources sources);

    [[nodiscard]] QString render(const QVector<platform::HostGpu>& gpus) const;

    [[nodiscard]] QString name(const platform::HostGpu& gpu) const;

private:
    struct PciIdentity {
        quint32 vendor = 0x1af4;
        quint32 device = 0x1050;
        int revision = -1;
    };

    [[nodiscard]] PciIdentity identity(const platform::HostGpu& gpu) const;
    [[nodiscard]] std::optional<quint32> sysfs_number(const QString& address,
                                                      const char* attribute, quint32 limit) const;
    [[nodiscard]] std::optional<QString> amdgpu_name(const PciIdentity& id) const;
    [[nodiscard]] std::optional<QString> pci_ids_name(const PciIdentity& id) const;
    [[nodiscard]] static QString fitted(const QString& name);

    Sources sources_;
};

}
