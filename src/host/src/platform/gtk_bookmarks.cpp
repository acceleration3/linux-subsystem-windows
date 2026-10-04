#include "lsw/host/platform/gtk_bookmarks.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace lsw::host::platform {

namespace {

QString bookmarks_path() {
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(config).filePath(QStringLiteral("gtk-3.0/bookmarks"));
}

}  // namespace

bool GtkBookmarks::register_windows_bookmark(const QString& username, const QString& password,
                                             const QString& share) {
    const QString path = bookmarks_path();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QStringList lines;
    QFile file{path};
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        file.close();
    }
    // Drop any previous LSW line before appending, so a rotated
    // password or a changed share replaces rather than accumulates.
    const QString signature = GuestSmbShare::signature();
    lines.removeIf([&](const QString& line) { return line.contains(signature); });
    lines.removeIf([](const QString& line) { return line.trimmed().isEmpty(); });
    lines << QStringLiteral("%1 Windows").arg(GuestSmbShare::url(username, password, share));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        LSW_WARN("gtk bookmarks: cannot write {}", path.toStdString());
        return false;
    }
    file.write(lines.join(QLatin1Char('\n')).toUtf8());
    file.write("\n");
    file.close();
    return true;
}

void GtkBookmarks::remove_windows_bookmark() {
    const QString path = bookmarks_path();
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    const auto before = lines.size();
    const QString signature = GuestSmbShare::signature();
    lines.removeIf([&](const QString& line) { return line.contains(signature); });
    if (lines.size() == before) {
        return;
    }
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        file.write(lines.join(QLatin1Char('\n')).toUtf8());
        file.close();
    }
}

}  // namespace lsw::host::platform
