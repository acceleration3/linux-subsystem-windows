#include "lsw/host/platform/dolphin_places.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"

#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

namespace lsw::host::platform {

namespace {

constexpr const char* kBookmarkTitle = "Windows";
constexpr const char* kBookmarkIcon = "network-workgroup";

QString places_xbel_path() {
    const QString share = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(share).filePath(QStringLiteral("user-places.xbel"));
}

QDomElement find_existing_bookmark(const QDomElement& root, const QString& host_path) {
    const QDomNodeList bookmarks = root.elementsByTagName(QStringLiteral("bookmark"));
    for (int i = 0; i < bookmarks.size(); ++i) {
        const QDomElement b = bookmarks.at(i).toElement();
        if (b.isNull())
            continue;
        const QString href = b.attribute(QStringLiteral("href"));
        if (href.contains(host_path)) {
            return b;
        }
    }
    return {};
}

}

bool DolphinPlaces::register_windows_bookmark(const QString& username, const QString& password,
                                              const QString& share) {
    const QString path = places_xbel_path();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QDomDocument doc;
    QDomElement root;
    {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly)) {
            if (const QDomDocument::ParseResult parsed = doc.setContent(&in); !parsed) {
                LSW_WARN("DolphinPlaces: {} is not parseable XML ({}); refusing to overwrite",
                         path.toStdString(), parsed.errorMessage.toStdString());
                return false;
            }
            in.close();
            root = doc.documentElement();
        }
    }

    if (root.isNull()) {
        doc.appendChild(doc.createProcessingInstruction(
            QStringLiteral("xml"), QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
        QDomDocumentType dtype =
            doc.implementation().createDocumentType(QStringLiteral("xbel"), QString{}, QString{});
        doc.appendChild(dtype);
        root = doc.createElement(QStringLiteral("xbel"));
        root.setAttribute(QStringLiteral("xmlns:bookmark"),
                          QStringLiteral("http://www.freedesktop.org/standards/desktop-bookmarks"));
        root.setAttribute(QStringLiteral("xmlns:kdepriv"),
                          QStringLiteral("http://www.kde.org/kdepriv"));
        root.setAttribute(QStringLiteral("xmlns:mime"),
                          QStringLiteral("http://www.freedesktop.org/standards/shared-mime-info"));
        doc.appendChild(root);
    }

    const QString url = GuestSmbShare::url(username, password, share);
    QDomElement existing = find_existing_bookmark(root, GuestSmbShare::signature());
    if (!existing.isNull()) {
        existing.setAttribute(QStringLiteral("href"), url);
    } else {
        QDomElement bookmark = doc.createElement(QStringLiteral("bookmark"));
        bookmark.setAttribute(QStringLiteral("href"), url);

        QDomElement title = doc.createElement(QStringLiteral("title"));
        title.appendChild(doc.createTextNode(QString::fromUtf8(kBookmarkTitle)));
        bookmark.appendChild(title);

        QDomElement info = doc.createElement(QStringLiteral("info"));

        QDomElement md_fdo = doc.createElement(QStringLiteral("metadata"));
        md_fdo.setAttribute(QStringLiteral("owner"), QStringLiteral("http://freedesktop.org"));
        QDomElement icon = doc.createElement(QStringLiteral("bookmark:icon"));
        icon.setAttribute(QStringLiteral("name"), QString::fromUtf8(kBookmarkIcon));
        md_fdo.appendChild(icon);
        info.appendChild(md_fdo);

        QDomElement md_kde = doc.createElement(QStringLiteral("metadata"));
        md_kde.setAttribute(QStringLiteral("owner"), QStringLiteral("http://www.kde.org"));
        QDomElement id = doc.createElement(QStringLiteral("ID"));
        id.appendChild(
            doc.createTextNode(QStringLiteral("%1/0").arg(QDateTime::currentSecsSinceEpoch())));
        md_kde.appendChild(id);
        info.appendChild(md_kde);

        bookmark.appendChild(info);
        root.appendChild(bookmark);
    }

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LSW_WARN("DolphinPlaces: cannot write {}", path.toStdString());
        return false;
    }
    QTextStream s(&out);
    s.setEncoding(QStringConverter::Utf8);
    doc.save(s, 1);
    out.close();
    LSW_INFO("DolphinPlaces: registered {} bookmark in {}", kBookmarkTitle, path.toStdString());
    return true;
}

void DolphinPlaces::remove_windows_bookmark() {
    const QString path = places_xbel_path();
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QDomDocument doc;
    const bool parsed = static_cast<bool>(doc.setContent(&file));
    file.close();
    if (!parsed) {
        return;
    }
    QDomElement root = doc.documentElement();
    QDomElement existing = find_existing_bookmark(root, GuestSmbShare::signature());
    if (existing.isNull()) {
        return;
    }
    existing.parentNode().removeChild(existing);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(doc.toByteArray(2));
        file.close();
        LSW_INFO("DolphinPlaces: removed the Windows bookmark");
    }
}

}
