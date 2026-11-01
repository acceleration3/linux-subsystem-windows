#pragma once

#include "lsw/host/platform/host_usb_devices.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::services {

// The USB devices as StatusHub last saw them: the ones plugged in here,
// the ones the machine has been told to pass whenever they are, and where
// each is now.
struct UsbStatus {
    // Where a device is. Guest is the running machine's own definition
    // saying it has the device, not anything this manager asked for.
    enum class Place { Host, Guest, Absent };

    struct Device {
        // "vvvv:pppp" or "vvvv:pppp:serial".
        QString identity;
        QString name;
        int vendor = 0;
        int product = 0;
        Place place = Place::Absent;
        // Whether the kernel has it now. A device in the guest is present
        // too: a hostdev with no device behind it is not in this list at
        // all, see ``stale``.
        bool present = false;
        int bus = 0;
        int device = 0;
        bool accessible = false;
        bool mounted = false;
        bool is_hid = false;
        bool is_mass_storage = false;
        // In the "always pass" list.
        bool always = false;

        [[nodiscard]] bool attached() const noexcept { return place == Place::Guest; }
        // What a runtime action or an error is about: the identity is the
        // device's type (two of one kind have the same), the address says
        // which of them, and changes at every replug. Absent devices have
        // none.
        [[nodiscard]] QString key() const { return identity_key(identity, bus, device); }
        [[nodiscard]] QString type_id() const;
        [[nodiscard]] vm::DomainDescription::UsbDevice usb() const { return {vendor, product, bus, device}; }
        [[nodiscard]] bool operator==(const Device&) const = default;
    };

    [[nodiscard]] static QString identity_key(const QString& identity, int bus, int device) {
        return identity + QLatin1Char('@') + QString::number(bus) + QLatin1Char('.') + QString::number(device);
    }

    // libvirt's id of the boot this is about (VmStatus::domain_id), so what
    // was attempted in one boot is not held against the next.
    int boot_id = -1;
    // False until the machine's USB bus has been read once.
    bool read = false;
    // The guest is running: devices can be passed, and not before.
    bool guest_running = false;
    // Plugged in devices in bus order, then those remembered but absent.
    QVector<Device> devices;
    // Hostdevs of the live definition whose device is gone from the host.
    // QEMU keeps them, and a replug reuses nothing of them. Each is also a
    // row of ``devices`` (in the guest, not plugged in), so a detach that
    // fails can say so.
    QVector<vm::DomainDescription::UsbDevice> stale;

    // The remembered one by identity alone (the settings list's), or the
    // plugged-in one at an address.
    [[nodiscard]] const Device* find(const QString& identity) const;
    [[nodiscard]] const Device* find(const QString& identity, int bus, int device) const;
    [[nodiscard]] bool operator==(const UsbStatus&) const = default;

    // The status those readings make. ``names`` are the names devices had
    // when last seen, for the remembered ones that are unplugged now.
    [[nodiscard]] static UsbStatus build(const QVector<platform::HostUsbDevice>& host,
                                         const QVector<vm::DomainDescription::UsbDevice>& live,
                                         const QStringList& always, bool guest_running, int boot_id,
                                         const QHash<QString, QString>& names);
};

}  // namespace lsw::host::services
