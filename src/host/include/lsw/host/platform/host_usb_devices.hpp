#pragma once

#include <QString>
#include <QVector>

namespace lsw::host::platform {
struct HostUsbDevice {
    int vendor = 0;
    int product = 0;
    QString serial;
    QString name;
    int bus = 0;
    int device = 0;
    QString speed;
    bool is_hid = false;
    bool is_mass_storage = false;
    bool mounted = false;
    bool accessible = false;

    [[nodiscard]] QString identity() const;
    [[nodiscard]] QString type_id() const;
    [[nodiscard]] static QString identity_of(int vendor, int product, const QString& serial);
};

struct UsbRoots {
    QString sysfs_devices = QStringLiteral("/sys/bus/usb/devices");
    QString sysfs_class_block = QStringLiteral("/sys/class/block");
    QString mountinfo = QStringLiteral("/proc/self/mountinfo");
    QString swaps = QStringLiteral("/proc/swaps");
    QString sysfs_btrfs = QStringLiteral("/sys/fs/btrfs");
    QString dev_bus_usb = QStringLiteral("/dev/bus/usb");
};

class HostUsbDevices {
public:
    HostUsbDevices() = delete;

    [[nodiscard]] static QVector<HostUsbDevice> enumerate();
    [[nodiscard]] static QVector<HostUsbDevice> enumerate(const UsbRoots& roots);
};

}
