#include "lsw/host/services/mount_status.hpp"

#include <QDir>

namespace lsw::host::services {

namespace {

QString shown(const QString& path) {
    const QString home = QDir::homePath();
    if (path == home || path.startsWith(home + QLatin1Char('/'))) {
        return QLatin1Char('~') + path.mid(home.size());
    }
    return path;
}

}  // namespace

QString MountStatus::text() const {
    switch (state) {
    case State::Off:
        return QStringLiteral("Not mounted: this is turned off.");
    case State::NoGuest:
        return QStringLiteral("Not mounted: Windows is not running.");
    case State::Waiting:
        return QStringLiteral("Not mounted: waiting for Windows to finish starting.");
    case State::Mounting:
        return QStringLiteral("Mounting at %1...").arg(shown(path));
    case State::Mounted:
        return QStringLiteral("Mounted at %1.").arg(shown(path));
    case State::Unmounting:
        return QStringLiteral("Unmounting %1...").arg(shown(path));
    case State::Unavailable:
        return QStringLiteral("Not mounted: %1.").arg(detail);
    case State::Failed:
        return QStringLiteral("Not mounted: %1").arg(detail);
    }
    return {};
}

}  // namespace lsw::host::services
