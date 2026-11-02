#include "lsw/host/platform/guest_bookmark_cleanup.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>

namespace lsw::host::platform {

namespace {

constexpr auto kTitle = "Windows";

bool is_guest_bookmark(const QDomElement& bookmark) {
    const QString title = bookmark.firstChildElement(QStringLiteral("title")).text();
    return title == QLatin1String(kTitle) &&
           bookmark.attribute(QStringLiteral("href")).contains(GuestSmbShare::signature());
}

}  // namespace

void GuestBookmarkCleanup::run() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (remove_from_places(QDir(data).filePath(QStringLiteral("user-places.xbel")))) {
        LSW_INFO("removed the old Windows bookmark from Dolphin's places");
    }
    if (remove_from_gtk(QDir(config).filePath(QStringLiteral("gtk-3.0/bookmarks")))) {
        LSW_INFO("removed the old Windows bookmark from the GTK file managers' bookmarks");
    }
}

bool GuestBookmarkCleanup::remove_from_places(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QDomDocument doc;
    const bool parsed = static_cast<bool>(doc.setContent(&file));
    file.close();
    if (!parsed) {
        return false;
    }
    QList<QDomElement> found;
    const QDomNodeList bookmarks =
        doc.documentElement().elementsByTagName(QStringLiteral("bookmark"));
    for (int i = 0; i < bookmarks.size(); ++i) {
        const QDomElement bookmark = bookmarks.at(i).toElement();
        if (!bookmark.isNull() && is_guest_bookmark(bookmark)) {
            found.push_back(bookmark);
        }
    }
    if (found.isEmpty()) {
        return false;
    }
    for (QDomElement& bookmark : found) {
        bookmark.parentNode().removeChild(bookmark);
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LSW_WARN("cannot rewrite {}", path.toStdString());
        return false;
    }
    file.write(doc.toByteArray(1));
    file.close();
    return true;
}

bool GuestBookmarkCleanup::remove_from_gtk(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    const qsizetype before = lines.size();
    const QString signature = GuestSmbShare::signature();
    lines.removeIf([&](const QString& line) {
        const QString trimmed = line.trimmed();
        return trimmed.contains(signature) &&
               trimmed.endsWith(QLatin1Char(' ') + QLatin1String(kTitle));
    });
    if (lines.size() == before) {
        return false;
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        LSW_WARN("cannot rewrite {}", path.toStdString());
        return false;
    }
    file.write(lines.join(QLatin1Char('\n')).toUtf8());
    file.close();
    return true;
}

}  // namespace lsw::host::platform
