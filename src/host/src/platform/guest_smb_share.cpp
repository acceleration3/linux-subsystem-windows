#include "lsw/host/platform/guest_smb_share.hpp"

#include <QUrl>

namespace lsw::host::platform {

namespace {

// Must match the hostfwd in embedded/domain.xml.
constexpr auto kHost = "127.0.0.1";
constexpr int kPort = 5445;

}  // namespace

QString GuestSmbShare::url(const QString& username, const QString& password, const QString& share) {
    QUrl url;
    url.setScheme(QStringLiteral("smb"));
    url.setHost(QLatin1String(kHost));
    url.setPort(kPort);
    url.setUserName(username);
    url.setPassword(password);
    url.setPath(QLatin1Char('/') + share);
    return QString::fromUtf8(url.toEncoded());
}

QString GuestSmbShare::signature() {
    return QStringLiteral("@%1:%2/").arg(QLatin1String(kHost)).arg(kPort);
}

QString GuestSmbShare::share_for_scope(const QString& scope) {
    return scope == QLatin1String("profile") ? QStringLiteral("Users") : QStringLiteral("C$");
}

}  // namespace lsw::host::platform
