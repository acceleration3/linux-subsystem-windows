#include "lsw/host/services/usb_passthrough.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/platform/host_usb_devices.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QDateTime>
#include <QtConcurrent>

#include <algorithm>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

struct Outcome {
    bool ok = false;
    QString error;
};

constexpr qint64 kBackoffFirstMs = 4000;
constexpr qint64 kBackoffMaxMs = 60000;

}

UsbPassthrough::UsbPassthrough(vm::Lifecycle& lifecycle, StatusHub& status,
                               config::ManagerSettings& settings, QObject* parent)
    : QObject(parent), lifecycle_(lifecycle), status_(status), settings_(settings), access_(this) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &UsbPassthrough::reconcile);
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area == StatusHub::Area::Usb) {
            reconcile();
        }
    });
    connect(&access_, &platform::UsbAccess::finished, this,
            [this](int vendor, int product, bool ok, const QString& error) {
                const QString type = QStringLiteral("%1:%2")
                                         .arg(vendor, 4, 16, QLatin1Char('0'))
                                         .arg(product, 4, 16, QLatin1Char('0'));
                const QString asked = granting_.take(type);
                if (!ok && !asked.isEmpty()) {
                    fail(asked, error);
                } else if (ok) {
                    for (const auto& device : status_.usb().devices) {
                        if (device.type_id() == type) {
                            tried_.remove(device.key());
                            errors_.remove(device.key());
                        }
                    }
                }
                Q_EMIT changed();
                status_.refresh(StatusHub::Area::Usb);
            });
}

void UsbPassthrough::fail(const QString& key, const QString& why) {
    LSW_WARN("USB {}: {}", key.toStdString(), why.toStdString());
    errors_.insert(key, why);
    Q_EMIT changed();
}

QString UsbPassthrough::error_for(const QString& identity, int bus, int device) const {
    return errors_.value(UsbStatus::identity_key(identity, bus, device));
}

bool UsbPassthrough::granting(const QString& identity, int bus, int device) const {
    return std::any_of(granting_.constBegin(), granting_.constEnd(), [&](const QString& key) {
        return key == UsbStatus::identity_key(identity, bus, device);
    });
}

bool UsbPassthrough::busy(const QString& identity, int bus, int device) const {
    return busy_.contains(UsbStatus::identity_key(identity, bus, device));
}

void UsbPassthrough::attach(const QString& identity, int bus, int device) {
    const auto* found = status_.usb().find(identity, bus, device);
    if (found == nullptr || !found->present) {
        return;
    }
    const UsbStatus::Device row = *found;
    errors_.remove(row.key());
    returned_.remove(row.key());
    tried_.remove(row.key());
    if (!status_.usb().guest_running) {
        fail(row.key(), QStringLiteral("start the guest to pass a device"));
        return;
    }
    if (row.attached()) {
        return;
    }
    run_attach(row);
}

void UsbPassthrough::run_attach(const UsbStatus::Device& row) {
    const QString key = row.key();
    if (busy_.contains(key)) {
        return;
    }
    busy_.insert(key);
    Q_EMIT changed();
    LSW_DEBUG("USB {}: passing to the guest", key.toStdString());
    const QUuid uuid = status_.vm().uuid;
    const auto usb = row.usb();
    const QString identity = row.identity;
    (void)QtConcurrent::run([this, uuid, usb, identity] {
        Outcome out;
        const auto devices = platform::HostUsbDevices::enumerate();
        const auto it = std::find_if(devices.begin(), devices.end(), [&](const platform::HostUsbDevice& d) {
            return d.identity() == identity && d.bus == usb.bus && d.device == usb.device;
        });
        if (it == devices.end()) {
            out.error = QStringLiteral("it was unplugged");
        } else if (it->mounted) {
            out.error = QStringLiteral("something on it is mounted on this machine; unmount it first");
        } else if (!it->accessible) {
            out.error = QStringLiteral("this user may not open it yet; allow access first");
        } else {
            out.ok = lifecycle_.attach_usb(uuid, usb, &out.error);
        }
        return out;
    }).then(this, [this, key](const Outcome& out) {
        busy_.remove(key);
        if (out.ok) {
            LSW_INFO("USB {} passed to the guest", key.toStdString());
            errors_.remove(key);
        } else {
            fail(key, out.error);
        }
        Q_EMIT changed();
        status_.refresh(StatusHub::Area::Usb);
    });
}

void UsbPassthrough::detach(const QString& identity, int bus, int device) {
    const auto* found = status_.usb().find(identity, bus, device);
    if (found == nullptr || !found->attached()) {
        return;
    }
    errors_.remove(found->key());
    if (!found->present) {
        stale_.remove(found->key());
    }
    run_detach(*found, true);
}

void UsbPassthrough::run_detach(const UsbStatus::Device& row, bool by_hand) {
    const QString key = row.key();
    if (busy_.contains(key)) {
        return;
    }
    busy_.insert(key);
    const QUuid uuid = status_.vm().uuid;
    const auto usb = row.usb();
    const bool always = row.always;
    LSW_DEBUG("USB {}: returning to the host{}", key.toStdString(), by_hand ? "" : " (unplugged)");
    Q_EMIT changed();
    (void)QtConcurrent::run([this, uuid, usb] {
        Outcome out;
        out.ok = lifecycle_.detach_usb(uuid, usb, &out.error);
        return out;
    }).then(this, [this, key, by_hand, always](const Outcome& out) {
        busy_.remove(key);
        if (!out.ok) {
            fail(key, out.error);
            if (!by_hand) {
                Backoff& wait = stale_[key];
                ++wait.failures;
                const qint64 delay = std::min(kBackoffMaxMs, kBackoffFirstMs << std::min(wait.failures - 1, 5));
                wait.next_ms = QDateTime::currentMSecsSinceEpoch() + delay;
                retry_.start(static_cast<int>(delay));
            }
        } else {
            LSW_INFO("USB {} returned to the host", key.toStdString());
            stale_.remove(key);
            if (by_hand && always) {
                returned_.insert(key);
            }
        }
        Q_EMIT changed();
        status_.refresh(StatusHub::Area::Usb);
    });
}

void UsbPassthrough::set_always(const QString& identity, bool on) {
    QStringList list = settings_.string_list_value(K::kUsbAlways);
    if (on == list.contains(identity)) {
        return;
    }
    if (on) {
        list << identity;
    } else {
        list.removeAll(identity);
        for (const auto& device : status_.usb().devices) {
            if (device.identity == identity) {
                returned_.remove(device.key());
                tried_.remove(device.key());
            }
        }
    }
    LSW_INFO("USB {}: always pass to the guest is {}", identity.toStdString(), on ? "on" : "off");
    settings_.set_value(K::kUsbAlways, list);
}

void UsbPassthrough::grant_access(const QString& identity, int bus, int device) {
    const auto* found = status_.usb().find(identity, bus, device);
    if (found == nullptr || !found->present) {
        return;
    }
    if (granting_.contains(found->type_id())) {
        return;
    }
    errors_.remove(found->key());
    granting_.insert(found->type_id(), found->key());
    if (!access_.grant(found->vendor, found->product)) {
        granting_.remove(found->type_id());
        return;
    }
    Q_EMIT changed();
}

void UsbPassthrough::forget_attempts() {
    tried_.clear();
    returned_.clear();
    stale_.clear();
    errors_.clear();
}

void UsbPassthrough::reconcile() {
    const UsbStatus& usb = status_.usb();
    if (usb.guest_running != was_running_ || (usb.boot_id != boot_id_ && usb.guest_running)) {
        LSW_DEBUG("USB: guest running {}, boot {}; attempts forgotten", usb.guest_running, usb.boot_id);
        was_running_ = usb.guest_running;
        boot_id_ = usb.boot_id;
        forget_attempts();
        Q_EMIT changed();
    }
    if (!usb.guest_running) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QSet<QString> live;
    for (const auto& device : usb.devices) {
        live.insert(device.key());
    }
    const auto drop_gone = [&live](auto& keys) {
        for (auto it = keys.begin(); it != keys.end();) {
            it = live.contains(*it) ? std::next(it) : keys.erase(it);
        }
    };
    drop_gone(tried_);
    drop_gone(returned_);
    for (auto it = stale_.begin(); it != stale_.end();) {
        it = live.contains(it.key()) ? std::next(it) : stale_.erase(it);
    }

    for (const auto& device : usb.devices) {
        const QString key = device.key();
        if (device.attached() && !device.present) {
            const auto wait = stale_.constFind(key);
            if (!busy_.contains(key) && (wait == stale_.constEnd() || wait->next_ms <= now)) {
                if (wait == stale_.constEnd()) {
                    stale_.insert(key, Backoff{});
                }
                LSW_INFO("USB {} is gone from the host; detaching it from the guest", key.toStdString());
                run_detach(device, false);
            }
            continue;
        }
        if (!device.always || !device.present || device.attached() || !device.accessible
            || device.mounted || returned_.contains(key) || tried_.contains(key) || busy_.contains(key)) {
            continue;
        }
        tried_.insert(key);
        run_attach(device);
    }
}

}
