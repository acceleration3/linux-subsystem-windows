#pragma once

#include "lsw/host/platform/host_usb_devices.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::services {
struct UsbStatus {
    enum class Place { Host, Guest, Absent };

    struct Device {
        QString identity;
        QString name;
        int vendor = 0;
        int product = 0;
        Place place = Place::Absent;
        bool present = false;
        int bus = 0;
        int device = 0;
        bool accessible = false;
        bool mounted = false;
        bool is_hid = false;
        bool is_mass_storage = false;
        bool always = false;

        [[nodiscard]] bool attached() const noexcept { return place == Place::Guest; }
        [[nodiscard]] QString key() const { return identity_key(identity, bus, device); }
        [[nodiscard]] QString type_id() const;
        [[nodiscard]] vm::DomainDescription::UsbDevice usb() const { return {vendor, product, bus, device}; }
        [[nodiscard]] bool operator==(const Device&) const = default;
    };

    [[nodiscard]] static QString identity_key(const QString& identity, int bus, int device) {
        return identity + QLatin1Char('@') + QString::number(bus) + QLatin1Char('.') + QString::number(device);
    }

    int boot_id = -1;
    bool read = false;
    bool guest_running = false;
    QVector<Device> devices;
    QVector<vm::DomainDescription::UsbDevice> stale;

    [[nodiscard]] const Device* find(const QString& identity) const;
    [[nodiscard]] const Device* find(const QString& identity, int bus, int device) const;
    [[nodiscard]] bool operator==(const UsbStatus&) const = default;

    [[nodiscard]] static UsbStatus build(const QVector<platform::HostUsbDevice>& host,
                                         const QVector<vm::DomainDescription::UsbDevice>& live,
                                         const QStringList& always, bool guest_running, int boot_id,
                                         const QHash<QString, QString>& names);
};

}
