#include "lsw/host/platform/dolphin_places.hpp"

#include "lsw/base/log.hpp"

#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <QUrl>

namespace lsw::host::platform {

namespace {

constexpr const char* kBookmarkTitle = "Windows";
constexpr const char* kBookmarkIcon = "network-workgroup";

QString places_xbel_path() {
    const QString share =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(share).filePath(QStringLiteral("user-places.xbel"));
}

QString build_smb_url(const QString& username, const QString& password,
                      const QString& share) {
    QUrl u;
    u.setScheme(QStringLiteral("smb"));
    u.setHost(QStringLiteral("127.0.0.1"));
    u.setPort(5445);
    u.setUserName(username);
    u.setPassword(password);
    u.setPath(QLatin1Char('/') + share);
    return QString::fromUtf8(u.toEncoded());
}

// Already-present check uses URL-prefix-up-to-userinfo so the
// caller can rotate passwords on reinstall without producing
// duplicate entries. Returns the matching <bookmark> node if
// found, else a null QDomElement.
QDomElement find_existing_bookmark(const QDomElement& root,
                                   const QString& host_path) {
    const QDomNodeList bookmarks = root.elementsByTagName(QStringLiteral("bookmark"));
    for (int i = 0; i < bookmarks.size(); ++i) {
        const QDomElement b = bookmarks.at(i).toElement();
        if (b.isNull()) continue;
        const QString href = b.attribute(QStringLiteral("href"));
        if (href.contains(host_path)) {
            return b;
        }
    }
    return {};
}

}  // namespace

bool DolphinPlaces::register_windows_bookmark(const QString& username,
                                              const QString& password,
                                              const QString& share) {
    const QString path = places_xbel_path();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QDomDocument doc;
    QDomElement root;
    {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly)) {
            QString err;
            if (!doc.setContent(&in, &err)) {
                LSW_WARN("DolphinPlaces: {} is not parseable XML ({}); refusing to overwrite",
                         path.toStdString(), err.toStdString());
                return false;
            }
            in.close();
            root = doc.documentElement();
        }
    }

    if (root.isNull()) {
        // No file (or file existed but had no root). Synthesise the
        // minimal XBEL skeleton Dolphin emits on first run. Dolphin
        // will repopulate Home/Desktop/etc. on its next start; we
        // only need the document to be well-formed.
        doc.appendChild(doc.createProcessingInstruction(
            QStringLiteral("xml"),
            QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
        QDomDocumentType dtype = doc.implementation().createDocumentType(
            QStringLiteral("xbel"), QString{}, QString{});
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

    // Idempotency: ``smb://...@127.0.0.1:5445/C$`` is the unique
    // signature. Don't add a duplicate if the user (or an earlier
    // install) already wrote one — just refresh the credentials
    // by overwriting its href.
    // The host and port identify the entry as ours whatever the share
    // or the credentials happen to be, so changing either refreshes the
    // one line rather than adding a second.
    const QString unique_signature = QStringLiteral("@127.0.0.1:5445/");
    const QString url = build_smb_url(username, password, share);
    QDomElement existing = find_existing_bookmark(root, unique_signature);
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
        md_fdo.setAttribute(QStringLiteral("owner"),
                            QStringLiteral("http://freedesktop.org"));
        QDomElement icon = doc.createElement(QStringLiteral("bookmark:icon"));
        icon.setAttribute(QStringLiteral("name"), QString::fromUtf8(kBookmarkIcon));
        md_fdo.appendChild(icon);
        info.appendChild(md_fdo);

        QDomElement md_kde = doc.createElement(QStringLiteral("metadata"));
        md_kde.setAttribute(QStringLiteral("owner"),
                            QStringLiteral("http://www.kde.org"));
        QDomElement id = doc.createElement(QStringLiteral("ID"));
        // Dolphin's own ID format is ``<unix-seconds>/<seq>``. The
        // value just needs to be unique within this file; current
        // time + "/0" matches what Dolphin writes for new entries.
        id.appendChild(doc.createTextNode(
            QStringLiteral("%1/0").arg(QDateTime::currentSecsSinceEpoch())));
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
    LSW_INFO("DolphinPlaces: registered {} bookmark in {}",
             kBookmarkTitle, path.toStdString());
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
    QDomElement existing =
        find_existing_bookmark(root, QStringLiteral("@127.0.0.1:5445/"));
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

}  // namespace lsw::host::platform
