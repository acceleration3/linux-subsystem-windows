#pragma once

#include <QString>
#include <QVector>

namespace lsw::host::platform {

// One USB device on this machine, as sysfs describes it.
struct HostUsbDevice {
    int vendor = 0;
    int product = 0;
    QString serial;
    // The product string; else the manufacturer's; else "-".
    QString name;
    int bus = 0;
    int device = 0;
    // Mb/s as the kernel states it ("480", "5000"); empty when unknown.
    QString speed;
    // A keyboard, a mouse or another human interface device: handing it
    // away takes the machine's own input with it.
    bool is_hid = false;
    // An interface of class 08: a disk, whose raw device node is what
    // opening the device gives access to.
    bool is_mass_storage = false;
    // A disk of this device (or something built on one: LUKS, LVM, a btrfs
    // that has it among its members) is mounted or swapped on here.
    bool mounted = false;
    // /dev/bus/usb/BBB/DDD can be opened for reading and writing, which is
    // what QEMU, running as the user, needs to claim it.
    bool accessible = false;

    // "vvvv:pppp", or "vvvv:pppp:serial" for a device that has one: what
    // the settings remember a device by, since its bus and device number
    // change at every replug.
    [[nodiscard]] QString identity() const;
    // The type alone, "vvvv:pppp".
    [[nodiscard]] QString type_id() const;
    [[nodiscard]] static QString identity_of(int vendor, int product, const QString& serial);
};

// Where the USB devices are read from. The defaults are the real system; a
// test points them at a tree it built.
struct UsbRoots {
    QString sysfs_devices = QStringLiteral("/sys/bus/usb/devices");
    QString sysfs_class_block = QStringLiteral("/sys/class/block");
    QString mountinfo = QStringLiteral("/proc/self/mountinfo");
    QString swaps = QStringLiteral("/proc/swaps");
    // Where btrfs lists the members of each filesystem.
    QString sysfs_btrfs = QStringLiteral("/sys/fs/btrfs");
    QString dev_bus_usb = QStringLiteral("/dev/bus/usb");
};

// The USB devices attached to this machine, read from sysfs, with no
// libusb: the same few files lsusb reads.
class HostUsbDevices {
public:
    HostUsbDevices() = delete;

    // Every device that is not a root hub or a hub, in bus and device
    // order.
    [[nodiscard]] static QVector<HostUsbDevice> enumerate();
    [[nodiscard]] static QVector<HostUsbDevice> enumerate(const UsbRoots& roots);
};

}  // namespace lsw::host::platform
