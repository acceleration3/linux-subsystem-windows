#pragma once

#include "lsw/host/vm/domain_xml_inputs.hpp"

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace lsw::host::vm {
struct DomainDescription {
    struct GpuDevice {
        QString id;
        QString host_gpu;
        bool display = true;
        int hostmem_mib = 0;
        int vram_reserve_mib = 0;
        std::optional<QString> modes;
        std::optional<quint32> refresh_millihz;
    };

    using Share = DomainShare;

    struct UsbDevice {
        int vendor = 0;
        int product = 0;
        int bus = 0;
        int device = 0;

        [[nodiscard]] bool operator==(const UsbDevice&) const = default;
    };

    int vcpus = 0;
    qint64 memory_mib = 0;
    QString disk_path;
    QString disk_bus;
    QVector<GpuDevice> gpus;
    bool installing = false;
    QVector<Share> shares;
    QVector<UsbDevice> usb_devices;

    [[nodiscard]] const GpuDevice* display() const noexcept;

    [[nodiscard]] static std::optional<DomainDescription> parse(const QString& xml);

    [[nodiscard]] static QStringList split_options(const QString& line);

    [[nodiscard]] static int size_mib(const QString& value);
};

class DiskImage {
public:
    DiskImage() = delete;

    [[nodiscard]] static std::optional<quint64> virtual_size(const QString& path);

    [[nodiscard]] static std::optional<quint64> qcow2_virtual_size(const QByteArray& header);
};

}
