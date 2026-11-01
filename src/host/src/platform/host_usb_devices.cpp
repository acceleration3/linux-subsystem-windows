#include "lsw/host/platform/host_usb_devices.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <algorithm>

#include <unistd.h>

namespace lsw::host::platform {

namespace {

constexpr auto kHubClass = "09";
constexpr auto kHidClass = "03";

// A one-line sysfs attribute, trimmed; empty when absent.
QString attribute(const QString& dir, const QString& name) {
    QFile file(QDir(dir).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

// The block device a mount source or swap file names: ``/dev/sdb1``, or a
// link to one (``/dev/mapper/luks-x`` is ``dm-0``). The name as given when
// there is no such file, which is all a test tree has.
QString block_name_of(const QString& source) {
    if (!source.startsWith(QLatin1Char('/'))) {
        return {};
    }
    const QString real = QFileInfo(source).canonicalFilePath();
    return QFileInfo(real.isEmpty() ? source : real).fileName();
}

struct Busy {
    // "major:minor" of every mount; btrfs reports an anonymous 0:N here, so
    // the numbers alone say nothing of it.
    QSet<QString> numbers;
    // The block devices mounts and swaps name as their source.
    QSet<QString> names;
};

Busy busy_sources(const UsbRoots& roots) {
    Busy busy;
    QFile mounts(roots.mountinfo);
    if (mounts.open(QIODevice::ReadOnly)) {
        // id parent major:minor root mount-point options [tags] - fstype source super-options
        for (const QByteArray& line : mounts.readAll().split('\n')) {
            const QList<QByteArray> fields = line.split(' ');
            if (fields.size() > 2) {
                busy.numbers.insert(QString::fromLatin1(fields.at(2)));
            }
            const qsizetype dash = fields.indexOf("-");
            if (dash >= 0 && fields.size() > dash + 2) {
                if (const QString name = block_name_of(QString::fromUtf8(fields.at(dash + 2))); !name.isEmpty()) {
                    busy.names.insert(name);
                }
            }
        }
    }
    QFile swaps(roots.swaps);
    if (swaps.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = swaps.readAll().split('\n');
        for (qsizetype i = 1; i < lines.size(); ++i) {  // the first is the header
            if (const QString name = block_name_of(QString::fromLatin1(lines.at(i).split(' ').value(0)));
                !name.isEmpty()) {
                busy.names.insert(name);
            }
        }
    }
    return busy;
}

// The block devices in use: those a mount or swap names or has the number of,
// and everything below them that they are stacked on (the partition under a
// LUKS mapping, the mapping under an LVM volume), through each device's
// slaves/, and every member of a btrfs filesystem one of whose members is.
QSet<QString> busy_blocks(const UsbRoots& roots) {
    const Busy sources = busy_sources(roots);
    QSet<QString> busy;
    QStringList queue;
    const QDir blocks(roots.sysfs_class_block);
    const auto add = [&](const QString& name) {
        if (!busy.contains(name)) {
            busy.insert(name);
            queue << name;
        }
    };
    for (const QString& name : blocks.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        if (sources.names.contains(name) ||
            sources.numbers.contains(attribute(blocks.filePath(name), QStringLiteral("dev")))) {
            add(name);
        }
    }
    for (const QString& name : std::as_const(sources.names)) {
        add(name);  // named by a source with no entry of its own here
    }
    // A btrfs filesystem with several devices has them all in use.
    const QDir btrfs(roots.sysfs_btrfs);
    for (const QString& uuid : btrfs.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QStringList members =
            QDir(btrfs.filePath(uuid) + QStringLiteral("/devices")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        if (std::any_of(members.begin(), members.end(), [&](const QString& m) { return busy.contains(m); })) {
            for (const QString& member : members) {
                add(member);
            }
        }
    }
    while (!queue.isEmpty()) {
        const QString name = queue.takeLast();
        const QDir slaves(blocks.filePath(name) + QStringLiteral("/slaves"));
        for (const QString& below : slaves.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
            add(below);
        }
    }
    return busy;
}

// The block devices that belong to the USB device at ``dir`` (or to
// anything plugged into it, as a hub's): each directory under a "block"
// directory is one, and its own subdirectories named after it are its
// partitions.
QStringList blocks_below(const QString& dir) {
    QStringList names;
    QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (it.fileName() != QLatin1String("block")) {
            continue;
        }
        for (const QString& disk : QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            names << disk;
            for (const QString& part : QDir(path + QLatin1Char('/') + disk)
                                           .entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (part.startsWith(disk)) {
                    names << part;
                }
            }
        }
    }
    return names;
}

bool has_interface_class(const QString& dir, const char* wanted) {
    for (const QString& entry : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        // Interfaces are "<device>:<config>.<n>".
        if (entry.contains(QLatin1Char(':')) &&
            attribute(QDir(dir).filePath(entry), QStringLiteral("bInterfaceClass")) == QLatin1String(wanted)) {
            return true;
        }
    }
    return false;
}

}  // namespace

QString HostUsbDevice::identity_of(int vendor, int product, const QString& serial) {
    const QString type = QStringLiteral("%1:%2")
                             .arg(vendor, 4, 16, QLatin1Char('0'))
                             .arg(product, 4, 16, QLatin1Char('0'));
    return serial.isEmpty() ? type : type + QLatin1Char(':') + serial;
}

QString HostUsbDevice::identity() const {
    return identity_of(vendor, product, serial);
}

QString HostUsbDevice::type_id() const {
    return identity_of(vendor, product, QString{});
}

QVector<HostUsbDevice> HostUsbDevices::enumerate() {
    return enumerate(UsbRoots{});
}

QVector<HostUsbDevice> HostUsbDevices::enumerate(const UsbRoots& roots) {
    QVector<HostUsbDevice> out;
    const QDir devices(roots.sysfs_devices);
    QSet<QString> busy;
    bool busy_read = false;
    for (const QString& entry : devices.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        // Interfaces ("1-2:1.0") are not devices, and "usbN" is a root hub.
        if (entry.contains(QLatin1Char(':')) || entry.startsWith(QLatin1String("usb"))) {
            continue;
        }
        const QString dir = QFileInfo(devices.filePath(entry)).canonicalFilePath();
        if (dir.isEmpty() || attribute(dir, QStringLiteral("bDeviceClass")) == QLatin1String(kHubClass)) {
            continue;
        }
        bool vendor_ok = false;
        bool product_ok = false;
        HostUsbDevice device;
        device.vendor = attribute(dir, QStringLiteral("idVendor")).toInt(&vendor_ok, 16);
        device.product = attribute(dir, QStringLiteral("idProduct")).toInt(&product_ok, 16);
        // Waits on the device's lock, which a reset holds (44 s for a phone a
        // guest has just let go of). Through a reset devnum reads 0, and the
        // device would look unplugged; after it, the number is the real one.
        (void)attribute(dir, QStringLiteral("bConfigurationValue"));
        device.bus = attribute(dir, QStringLiteral("busnum")).toInt();
        device.device = attribute(dir, QStringLiteral("devnum")).toInt();
        if (!vendor_ok || !product_ok || device.bus <= 0 || device.device <= 0) {
            continue;
        }
        device.serial = attribute(dir, QStringLiteral("serial"));
        const QString product = attribute(dir, QStringLiteral("product"));
        const QString manufacturer = attribute(dir, QStringLiteral("manufacturer"));
        device.name = !product.isEmpty() ? product : !manufacturer.isEmpty() ? manufacturer : QStringLiteral("-");
        device.speed = attribute(dir, QStringLiteral("speed"));
        device.is_hid = has_interface_class(dir, kHidClass);
        device.is_mass_storage = has_interface_class(dir, "08");
        const QStringList blocks = blocks_below(dir);
        if (!blocks.isEmpty()) {
            if (!busy_read) {
                busy = busy_blocks(roots);
                busy_read = true;
            }
            device.mounted = std::any_of(blocks.begin(), blocks.end(),
                                         [&busy](const QString& name) { return busy.contains(name); });
        }
        const QByteArray node = QStringLiteral("%1/%2/%3")
                                    .arg(roots.dev_bus_usb)
                                    .arg(device.bus, 3, 10, QLatin1Char('0'))
                                    .arg(device.device, 3, 10, QLatin1Char('0'))
                                    .toLocal8Bit();
        device.accessible = ::access(node.constData(), R_OK | W_OK) == 0;
        out.push_back(std::move(device));
    }
    std::sort(out.begin(), out.end(), [](const HostUsbDevice& a, const HostUsbDevice& b) {
        return a.bus != b.bus ? a.bus < b.bus : a.device < b.device;
    });
    return out;
}

}  // namespace lsw::host::platform
