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

class UsbPassthrough : public QObject {
    Q_OBJECT
public:
    UsbPassthrough(vm::Lifecycle& lifecycle, StatusHub& status, config::ManagerSettings& settings,
                   QObject* parent = nullptr);

    void attach(const QString& identity, int bus, int device);
    void detach(const QString& identity, int bus, int device);
    void set_always(const QString& identity, bool on);
    void grant_access(const QString& identity, int bus, int device);

    [[nodiscard]] QString error_for(const QString& identity, int bus, int device) const;
    [[nodiscard]] bool granting(const QString& identity, int bus, int device) const;
    [[nodiscard]] bool busy(const QString& identity, int bus, int device) const;

Q_SIGNALS:
    void changed();

private:
    void reconcile();
    void fail(const QString& key, const QString& why);
    void forget_attempts();
    void run_attach(const UsbStatus::Device& device);
    void run_detach(const UsbStatus::Device& device, bool by_hand);

    vm::Lifecycle& lifecycle_;
    StatusHub& status_;
    config::ManagerSettings& settings_;
    platform::UsbAccess access_;
    QSet<QString> tried_;
    QSet<QString> returned_;
    QHash<QString, QString> errors_;
    QSet<QString> busy_;
    struct Backoff {
        int failures = 0;
        qint64 next_ms = 0;
    };
    QHash<QString, Backoff> stale_;
    QTimer retry_;
    QHash<QString, QString> granting_;
    bool was_running_ = false;
    int boot_id_ = -1;
};

}
