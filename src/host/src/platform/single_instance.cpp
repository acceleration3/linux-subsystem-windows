#include "lsw/host/platform/single_instance.hpp"

#include "lsw/base/log.hpp"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace lsw::host::platform {

namespace {

constexpr auto kBusName = "org.lsw.Manager";
constexpr auto kObjectPath = "/org/lsw/Manager";
constexpr auto kInterface = "org.lsw.Manager.Lifecycle";

}  // namespace

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {}

bool SingleInstance::acquire() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        LSW_WARN("session DBus unavailable; running without single-instance lock");
        primary_ = true;
        return true;
    }
    if (!bus.registerService(kBusName)) {
        return false;
    }
    primary_ = true;
    bus.registerObject(kObjectPath, kInterface, this,
                       QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    return true;
}

void SingleInstance::release() {
    if (!primary_) {
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.unregisterObject(kObjectPath);
        bus.unregisterService(kBusName);
    }
    primary_ = false;
}

bool SingleInstance::raise_existing() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    QDBusInterface ifc{kBusName, kObjectPath, kInterface, bus};
    if (!ifc.isValid()) {
        return false;
    }
    auto reply = ifc.call("raise");
    return reply.type() == QDBusMessage::ReplyMessage;
}

bool SingleInstance::launch_app_existing(const QString& app_id, const QString& activation_token) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    QDBusInterface ifc{kBusName, kObjectPath, kInterface, bus};
    if (!ifc.isValid()) {
        return false;
    }
    auto reply = ifc.call("launch_app", app_id, activation_token);
    return reply.type() == QDBusMessage::ReplyMessage;
}

bool SingleInstance::open_documents_existing(const QString& app_id, const QStringList& files,
                                             const QString& activation_token) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    QDBusInterface ifc{kBusName, kObjectPath, kInterface, bus};
    if (!ifc.isValid()) {
        return false;
    }
    auto reply = ifc.call("open_documents", app_id, files, activation_token);
    return reply.type() == QDBusMessage::ReplyMessage;
}

void SingleInstance::raise() {
    Q_EMIT raise_requested();
}

void SingleInstance::launch_app(const QString& app_id, const QString& activation_token) {
    Q_EMIT launch_app_requested(app_id, activation_token);
}

void SingleInstance::open_documents(const QString& app_id, const QStringList& files,
                                    const QString& activation_token) {
    Q_EMIT open_documents_requested(app_id, files, activation_token);
}

}  // namespace lsw::host::platform
