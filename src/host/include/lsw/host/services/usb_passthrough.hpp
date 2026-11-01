#pragma once

#include "lsw/host/platform/usb_access.hpp"
#include "lsw/host/services/usb_status.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

namespace lsw::host::config {
class ManagerSettings;
}
namespace lsw::host::vm {
class Lifecycle;
}

namespace lsw::host::services {

class StatusHub;

// The one place that acts on USB devices: hands them to the running guest
// and takes them back, remembers which to hand over whenever they are
// plugged in, and asks for the right to open them.
//
// What is true of the devices is StatusHub's USB snapshot, which reads the
// live definition; this only acts and reports what went wrong. The domain
// definition holds no host device on purpose: one that is missing at the
// next start must not stop the machine booting, so devices are attached to
// the running guest, and attached again here after each start.
//
// A device of a kind there are two of is told apart by its address on the
// bus, which changes at every replug: everything done at run time is about
// (identity, bus, device) - UsbStatus::Device::key() - and only the list of
// devices to pass whenever plugged in is by identity alone.
//
// libvirt calls run on a worker thread; their answers are applied here.
//
// Reconciling, on every change of the snapshot while the guest runs:
//  - a device on the always list that is plugged in, may be opened, has
//    nothing mounted and is not in the guest yet is attached;
//  - a hostdev whose device is gone from the host is detached, because
//    QEMU keeps it and a replug reuses nothing of it. One that will not
//    go is tried again after a longer wait each time, and the row says so.
// An attach that fails is not tried again until that device's address
// changes (it was replugged) or the guest boots again, so a device libvirt
// keeps refusing costs one log line. A device returned by hand stays out
// the same way, or reconciling would take it straight back.
class UsbPassthrough : public QObject {
    Q_OBJECT
public:
    UsbPassthrough(vm::Lifecycle& lifecycle, StatusHub& status, config::ManagerSettings& settings,
                   QObject* parent = nullptr);

    // Hand the device to the guest, or take it back. Both refuse with a
    // reason (``error_for``) when the device cannot be moved. The caller
    // asks the user first for a keyboard or mouse.
    void attach(const QString& identity, int bus, int device);
    void detach(const QString& identity, int bus, int device);
    // Whether the device is passed whenever it is plugged in and the guest
    // runs. By identity: it is the kind of device that is remembered.
    void set_always(const QString& identity, bool on);
    // Ask for the right to open this kind of device (a password prompt).
    void grant_access(const QString& identity, int bus, int device);

    // Why the last attempt on this device failed; empty if it did not.
    [[nodiscard]] QString error_for(const QString& identity, int bus, int device) const;
    // Waiting for the password prompt.
    [[nodiscard]] bool granting(const QString& identity, int bus, int device) const;
    // A pass or a return of it is under way.
    [[nodiscard]] bool busy(const QString& identity, int bus, int device) const;

Q_SIGNALS:
    // An error appeared or went, or a request started or ended.
    void changed();

private:
    void reconcile();
    void fail(const QString& key, const QString& why);
    void forget_attempts();
    // One attach or detach on the worker; ``by_hand`` is a person's
    // doing, which reconciling must not undo.
    void run_attach(const UsbStatus::Device& device);
    void run_detach(const UsbStatus::Device& device, bool by_hand);

    vm::Lifecycle& lifecycle_;
    StatusHub& status_;
    config::ManagerSettings& settings_;
    platform::UsbAccess access_;
    // Devices reconciling has attached or given up on, by key.
    QSet<QString> tried_;
    // Always devices returned by hand, by key.
    QSet<QString> returned_;
    QHash<QString, QString> errors_;
    // Actions on the worker now, by key.
    QSet<QString> busy_;
    // A stale hostdev that would not detach: how many times it was tried,
    // and when it may be again (ms since the epoch).
    struct Backoff {
        int failures = 0;
        qint64 next_ms = 0;
    };
    QHash<QString, Backoff> stale_;
    QTimer retry_;
    // The vendor:product types being asked about, with the key that asked.
    QHash<QString, QString> granting_;
    bool was_running_ = false;
    int boot_id_ = -1;
};

}  // namespace lsw::host::services
