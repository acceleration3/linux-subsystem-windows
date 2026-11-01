// The USB devices the manager offers to hand to the guest are read from
// sysfs, so this builds a small sysfs of its own and reads it: which
// directories are devices at all, how a device is named and identified,
// which are input devices, and which are in use because something on
// them (or on a LUKS or LVM layer above them) is mounted or swapped on.
// The real /sys is not touched.

#include "lsw/host/platform/host_usb_devices.hpp"
#include "lsw/host/services/usb_status.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

using lsw::host::platform::HostUsbDevice;
using lsw::host::platform::HostUsbDevices;
using lsw::host::platform::UsbRoots;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void write(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text.toUtf8());
    }
}

// A device directory with the attributes sysfs gives it.
void device(const QString& root, const QString& name, const QString& vendor, const QString& product,
            int bus, int number, const QString& klass = QStringLiteral("00")) {
    const QString dir = root + QLatin1Char('/') + name;
    write(dir + QStringLiteral("/idVendor"), vendor + QLatin1Char('\n'));
    write(dir + QStringLiteral("/idProduct"), product + QLatin1Char('\n'));
    write(dir + QStringLiteral("/busnum"), QString::number(bus) + QLatin1Char('\n'));
    write(dir + QStringLiteral("/devnum"), QString::number(number) + QLatin1Char('\n'));
    write(dir + QStringLiteral("/bDeviceClass"), klass + QLatin1Char('\n'));
    write(dir + QStringLiteral("/speed"), QStringLiteral("480\n"));
}

const HostUsbDevice* find(const QVector<HostUsbDevice>& all, const QString& identity) {
    for (const auto& d : all) {
        if (d.identity() == identity) {
            return &d;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};
    QTemporaryDir tree;
    const QString root = tree.path();
    const QString devices = root + QStringLiteral("/devices");

    device(devices, QStringLiteral("usb1"), QStringLiteral("1d6b"), QStringLiteral("0002"), 1, 1,
           QStringLiteral("09"));
    // A tablet with a serial number and a HID interface.
    device(devices, QStringLiteral("1-2"), QStringLiteral("056a"), QStringLiteral("0374"), 1, 5);
    write(devices + QStringLiteral("/1-2/product"), QStringLiteral("Wacom Intuos S\n"));
    write(devices + QStringLiteral("/1-2/serial"), QStringLiteral("ABC123\n"));
    write(devices + QStringLiteral("/1-2/1-2:1.0/bInterfaceClass"), QStringLiteral("03\n"));
    // A hub.
    device(devices, QStringLiteral("1-3"), QStringLiteral("2109"), QStringLiteral("0817"), 1, 6,
           QStringLiteral("09"));
    // A stick whose partition sits under a LUKS mapping that is mounted.
    device(devices, QStringLiteral("1-4"), QStringLiteral("0781"), QStringLiteral("5581"), 1, 7);
    write(devices + QStringLiteral("/1-4/product"), QStringLiteral("Ultra\n"));
    write(devices + QStringLiteral("/1-4/1-4:1.0/bInterfaceClass"), QStringLiteral("08\n"));
    QDir().mkpath(devices + QStringLiteral("/1-4/1-4:1.0/host0/target0:0:0/0:0:0:0/block/sda/sda1"));
    // Another stick with a disk nobody has mounted.
    device(devices, QStringLiteral("1-5"), QStringLiteral("0951"), QStringLiteral("1666"), 1, 8);
    QDir().mkpath(devices + QStringLiteral("/1-5/1-5:1.0/host1/target1:0:0/1:0:0:0/block/sdb"));
    // Only a manufacturer string, and nothing at all.
    device(devices, QStringLiteral("2-1"), QStringLiteral("046d"), QStringLiteral("c52b"), 2, 3);
    write(devices + QStringLiteral("/2-1/manufacturer"), QStringLiteral("Logitech\n"));
    device(devices, QStringLiteral("2-2"), QStringLiteral("dead"), QStringLiteral("beef"), 2, 4);
    // Not a device: an interface, and a directory with no ids.
    QDir().mkpath(devices + QStringLiteral("/1-2:1.0"));
    QDir().mkpath(devices + QStringLiteral("/stray"));
    // One reached through a symlink, as /sys/bus/usb/devices has them.
    device(root + QStringLiteral("/real"), QStringLiteral("3-1"), QStringLiteral("0bda"), QStringLiteral("8153"), 3, 2);
    QFile::link(root + QStringLiteral("/real/3-1"), devices + QStringLiteral("/3-1"));

    // A stick formatted btrfs: mounted with an anonymous 0:N, so only the
    // source says which disk. And a second one that is a member of a
    // multi-device btrfs whose other member is mounted.
    device(devices, QStringLiteral("2-3"), QStringLiteral("abcd"), QStringLiteral("0001"), 2, 5);
    QDir().mkpath(devices + QStringLiteral("/2-3/2-3:1.0/host2/target2:0:0/2:0:0:0/block/sde/sde1"));
    device(devices, QStringLiteral("2-4"), QStringLiteral("abcd"), QStringLiteral("0002"), 2, 6);
    QDir().mkpath(devices + QStringLiteral("/2-4/2-4:1.0/host3/target3:0:0/3:0:0:0/block/sdf/sdf1"));
    device(devices, QStringLiteral("2-5"), QStringLiteral("abcd"), QStringLiteral("0003"), 2, 7);
    QDir().mkpath(devices + QStringLiteral("/2-5/2-5:1.0/host4/target4:0:0/4:0:0:0/block/sdg/sdg1"));

    const QString blocks = root + QStringLiteral("/class_block");
    write(blocks + QStringLiteral("/sde1/dev"), QStringLiteral("8:65\n"));
    write(blocks + QStringLiteral("/sdf1/dev"), QStringLiteral("8:81\n"));
    write(blocks + QStringLiteral("/sdg1/dev"), QStringLiteral("8:97\n"));
    QDir().mkpath(root + QStringLiteral("/btrfs/1111-2222/devices/sdf1"));
    QDir().mkpath(root + QStringLiteral("/btrfs/1111-2222/devices/sde1"));
    write(blocks + QStringLiteral("/sda/dev"), QStringLiteral("8:0\n"));
    write(blocks + QStringLiteral("/sda1/dev"), QStringLiteral("8:1\n"));
    write(blocks + QStringLiteral("/sdb/dev"), QStringLiteral("8:16\n"));
    write(blocks + QStringLiteral("/dm-0/dev"), QStringLiteral("254:0\n"));
    QDir().mkpath(blocks + QStringLiteral("/dm-0/slaves/sda1"));
    write(root + QStringLiteral("/mountinfo"),
          QStringLiteral("22 1 259:2 / / rw - ext4 /dev/nvme0n1p2 rw\n"
                         "36 22 254:0 / /run/media/u/stick rw - ext4 /dev/mapper/luks-x rw\n"
                         "41 22 0:45 / /run/media/u/btrfs rw,relatime shared:9 - btrfs /dev/sde1 rw,space_cache\n"));

    UsbRoots roots;
    roots.sysfs_devices = devices;
    roots.sysfs_class_block = blocks;
    roots.mountinfo = root + QStringLiteral("/mountinfo");
    roots.swaps = root + QStringLiteral("/swaps");
    roots.dev_bus_usb = root + QStringLiteral("/dev");
    roots.sysfs_btrfs = root + QStringLiteral("/btrfs");
    const auto all = HostUsbDevices::enumerate(roots);

    check(find(all, QStringLiteral("1d6b:0002")) == nullptr, "a root hub is not offered");
    check(find(all, QStringLiteral("2109:0817")) == nullptr, "a class 09 hub is not offered");
    check(all.size() == 9, "nine devices: the tablet, two sticks, two others, the linked one and three btrfs ones");

    const auto* tablet = find(all, QStringLiteral("056a:0374:ABC123"));
    check(tablet != nullptr, "a device with a serial is identified by vendor, product and serial");
    if (tablet != nullptr) {
        check(tablet->name == QStringLiteral("Wacom Intuos S"), "named by its product string");
        check(tablet->is_hid, "an interface of class 03 makes it a HID device");
        check(!tablet->mounted, "a tablet has nothing mounted");
        check(tablet->bus == 1 && tablet->device == 5 && tablet->speed == QStringLiteral("480"),
              "bus, device and speed");
        check(tablet->type_id() == QStringLiteral("056a:0374"), "the type is vendor and product");
        check(!tablet->accessible, "no device node, so not accessible");
    }
    const auto* stick = find(all, QStringLiteral("0781:5581"));
    check(stick != nullptr && stick->mounted && !stick->is_hid,
          "a partition under a mounted LUKS mapping is mounted");
    const auto* idle = find(all, QStringLiteral("0951:1666"));
    check(idle != nullptr && !idle->mounted, "a disk nothing is mounted on is not mounted");
    const auto* maker = find(all, QStringLiteral("046d:c52b"));
    check(maker != nullptr && maker->name == QStringLiteral("Logitech"),
          "without a product string, the manufacturer's");
    const auto* bare = find(all, QStringLiteral("dead:beef"));
    check(bare != nullptr && bare->name == QStringLiteral("-"), "with neither, a dash");
    const auto* btrfs = find(all, QStringLiteral("abcd:0001"));
    check(btrfs != nullptr && btrfs->mounted, "a btrfs mount (anonymous 0:N) is found by its source");
    const auto* member = find(all, QStringLiteral("abcd:0002"));
    check(member != nullptr && member->mounted, "a member of a mounted multi-device btrfs is in use");
    const auto* alone = find(all, QStringLiteral("abcd:0003"));
    check(alone != nullptr && !alone->mounted, "a disk that is not in the filesystem is not");
    check(find(all, QStringLiteral("0bda:8153")) != nullptr, "a device reached through a symlink is read");
    check(all.size() >= 2 && all.first().bus == 1 && all.last().bus == 3, "in bus order");

    // A device node the user can open makes it accessible.
    write(root + QStringLiteral("/dev/001/005"), QString{});
    const auto again = HostUsbDevices::enumerate(roots);
    const auto* opened = find(again, QStringLiteral("056a:0374:ABC123"));
    check(opened != nullptr && opened->accessible, "a readable, writable node is accessible");

    // A swap on the mapping counts as in use too.
    write(root + QStringLiteral("/mountinfo"), QStringLiteral("22 1 259:2 / / rw - ext4 /dev/nvme0n1p2 rw\n"));
    write(root + QStringLiteral("/swaps"),
          QStringLiteral("Filename\tType\tSize\tUsed\tPriority\n/dev/dm-0 partition 1024 0 -2\n"));
    const auto swapped = HostUsbDevices::enumerate(roots);
    const auto* swap_stick = find(swapped, QStringLiteral("0781:5581"));
    check(swap_stick != nullptr && swap_stick->mounted, "swap on the mapping is in use");

    // ---- the snapshot: two of one kind, and what is left behind --------
    using lsw::host::services::UsbStatus;
    HostUsbDevice twin;
    twin.vendor = 0x046d;
    twin.product = 0xc52b;
    twin.name = QStringLiteral("Receiver");
    twin.bus = 1;
    twin.device = 4;
    HostUsbDevice twin2 = twin;
    twin2.device = 9;
    const auto snapshot = UsbStatus::build(
        {twin, twin2}, {{0x46d, 0xc52b, 1, 9}, {0x1050, 0x0407, 2, 3}}, {QStringLiteral("046d:c52b")}, true, 7,
        {{QStringLiteral("1050:0407"), QStringLiteral("YubiKey")}});
    check(snapshot.boot_id == 7 && snapshot.guest_running, "the boot the snapshot is about");
    const auto* first = snapshot.find(QStringLiteral("046d:c52b"), 1, 4);
    const auto* second = snapshot.find(QStringLiteral("046d:c52b"), 1, 9);
    check(first != nullptr && second != nullptr && first->key() != second->key(),
          "two devices of one kind are two rows with their own keys");
    check(first != nullptr && !first->attached() && second != nullptr && second->attached(),
          "which of them is in the guest is by its address");
    check(snapshot.stale.size() == 1 && snapshot.stale.first().vendor == 0x1050,
          "a hostdev with no device behind it is stale");
    const auto* gone = snapshot.find(QStringLiteral("1050:0407"), 2, 3);
    check(gone != nullptr && gone->attached() && !gone->present && gone->name == QStringLiteral("YubiKey"),
          "and a row of its own, in the guest and not plugged in, with the name it had");

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
