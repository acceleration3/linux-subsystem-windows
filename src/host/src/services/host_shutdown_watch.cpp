#include "lsw/host/services/host_shutdown_watch.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace lsw::host::services {

namespace {

constexpr auto kService = "org.freedesktop.login1";
constexpr auto kPath = "/org/freedesktop/login1";
constexpr auto kInterface = "org.freedesktop.login1.Manager";

}

HostShutdownWatch::HostShutdownWatch(vm::Lifecycle& lifecycle, StatusHub& status, QObject* parent)
    : QObject(parent), lifecycle_(lifecycle), status_(status) {}

void HostShutdownWatch::start() {
    if (started_) {
        return;
    }
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        LSW_WARN("system DBus unavailable; the guest is not powered off when the host shuts down");
        return;
    }
    started_ = bus.connect(kService, kPath, kInterface, QStringLiteral("PrepareForShutdown"), this,
                           SLOT(prepare_for_shutdown(bool)));
    if (!started_) {
        LSW_WARN("could not listen for logind's shutdown announcement: {}",
                 bus.lastError().message().toStdString());
        return;
    }
    take_lock();
}

void HostShutdownWatch::take_lock() {
    QDBusInterface login{kService, kPath, kInterface, QDBusConnection::systemBus()};
    const QDBusReply<QDBusUnixFileDescriptor> reply =
        login.call(QStringLiteral("Inhibit"), QStringLiteral("shutdown"), QStringLiteral("LSW"),
                   QStringLiteral("Powering the Windows guest off"), QStringLiteral("delay"));
    if (!reply.isValid()) {
        LSW_WARN("logind refused LSW's shutdown delay lock: {}", reply.error().message().toStdString());
        return;
    }
    lock_ = reply.value();
}

void HostShutdownWatch::prepare_for_shutdown(bool starting) {
    if (!starting) {
        if (!lock_.isValid()) {
            take_lock();
        }
        return;
    }
    Q_EMIT shutting_down();
    const QUuid uuid = status_.vm().uuid;
    if (!uuid.isNull() && lifecycle_.is_running(uuid)) {
        LSW_INFO("the host is shutting down: powering the guest off");
        if (lifecycle_.force_off(uuid) != 0) {
            LSW_WARN("the guest could not be powered off before the host shut down");
        }
    }
    lock_ = QDBusUnixFileDescriptor{};
}

}
