#include "lsw/host/services/usb_status.hpp"

#include <algorithm>

namespace lsw::host::services {

QString UsbStatus::Device::type_id() const {
    return platform::HostUsbDevice::identity_of(vendor, product, QString{});
}

const UsbStatus::Device* UsbStatus::find(const QString& identity) const {
    for (const auto& device : devices) {
        if (device.identity == identity) {
            return &device;
        }
    }
    return nullptr;
}

const UsbStatus::Device* UsbStatus::find(const QString& identity, int bus, int device) const {
    for (const auto& d : devices) {
        if (d.identity == identity && d.bus == bus && d.device == device) {
            return &d;
        }
    }
    return nullptr;
}

UsbStatus UsbStatus::build(const QVector<platform::HostUsbDevice>& host,
                           const QVector<vm::DomainDescription::UsbDevice>& live,
                           const QStringList& always, bool guest_running, int boot_id,
                           const QHash<QString, QString>& names) {
    UsbStatus out;
    out.read = true;
    out.boot_id = boot_id;
    out.guest_running = guest_running;
    QVector<bool> used(live.size(), false);
    for (const auto& device : host) {
        Device d;
        d.identity = device.identity();
        d.name = device.name;
        d.vendor = device.vendor;
        d.product = device.product;
        d.present = true;
        d.bus = device.bus;
        d.device = device.device;
        d.accessible = device.accessible;
        d.mounted = device.mounted;
        d.is_hid = device.is_hid;
        d.is_mass_storage = device.is_mass_storage;
        d.always = always.contains(d.identity);
        d.place = Place::Host;
        for (qsizetype i = 0; i < live.size(); ++i) {
            const auto& hostdev = live.at(i);
            if (hostdev.vendor == device.vendor && hostdev.product == device.product
                && hostdev.bus == device.bus && hostdev.device == device.device) {
                d.place = Place::Guest;
                used[i] = true;
            }
        }
        out.devices.push_back(std::move(d));
    }
    for (qsizetype i = 0; i < live.size(); ++i) {
        if (used.at(i)) {
            continue;
        }
        const auto& hostdev = live.at(i);
        out.stale.push_back(hostdev);
        Device d;
        d.vendor = hostdev.vendor;
        d.product = hostdev.product;
        d.bus = hostdev.bus;
        d.device = hostdev.device;
        d.identity = d.type_id();
        d.name = QStringLiteral("-");
        for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
            if (it.key() == d.identity || it.key().startsWith(d.identity + QLatin1Char(':'))) {
                d.name = it.value();
                d.identity = it.key();
                break;
            }
        }
        d.place = Place::Guest;
        d.present = false;
        out.devices.push_back(std::move(d));
    }
    for (const QString& identity : always) {
        if (out.find(identity) != nullptr) {
            continue;
        }
        const QStringList parts = identity.split(QLatin1Char(':'));
        bool vendor_ok = false;
        bool product_ok = false;
        Device d;
        d.identity = identity;
        d.vendor = parts.value(0).toInt(&vendor_ok, 16);
        d.product = parts.value(1).toInt(&product_ok, 16);
        if (!vendor_ok || !product_ok) {
            continue;
        }
        d.name = names.value(identity, QStringLiteral("-"));
        d.always = true;
        d.place = Place::Absent;
        out.devices.push_back(std::move(d));
    }
    return out;
}

}
