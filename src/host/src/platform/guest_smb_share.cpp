#include "lsw/host/platform/guest_smb_share.hpp"

#include <QDir>

namespace lsw::host::platform {

namespace {
constexpr auto kHost = "127.0.0.1";
constexpr int kPort = 5445;

}

QString GuestSmbShare::endpoint(const QString& share) {
    return QStringLiteral("%1:%2/%3").arg(QLatin1String(kHost)).arg(kPort).arg(share);
}

QString GuestSmbShare::mount_source(const QString& share) {
    return QStringLiteral("//%1/%2").arg(QLatin1String(kHost), share);
}

bool GuestSmbShare::is_mount_source(const QString& source) {
    return source == mount_source(QStringLiteral("C$")) || source == mount_source(QStringLiteral("Users"));
}

QString GuestSmbShare::mount_point() {
    return QDir::homePath() + QStringLiteral("/Windows");
}

QString GuestSmbShare::signature() {
    return QStringLiteral("@%1:%2/").arg(QLatin1String(kHost)).arg(kPort);
}

QString GuestSmbShare::share_for_scope(const QString& scope) {
    return scope == QLatin1String("profile") ? QStringLiteral("Users") : QStringLiteral("C$");
}

}
