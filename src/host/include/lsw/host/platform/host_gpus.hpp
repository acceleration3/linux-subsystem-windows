#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {
struct HostGpu {
    QString name;
    QString uuid;
    quint32 vendor_id = 0;
    quint32 device_id = 0;
    quint64 vram_bytes = 0;
    QString driver;
    QString driver_info;
    QString pci_address;
    bool cpu = false;
    bool integrated = false;
};

class HostGpus {
public:
    HostGpus() = delete;

    [[nodiscard]] static QVector<HostGpu> enumerate();

    [[nodiscard]] static std::optional<HostGpu> resolve(const QVector<HostGpu>& gpus,
                                                        const QString& setting);

    [[nodiscard]] static QString adapter_name(const HostGpu& gpu);
};

}
