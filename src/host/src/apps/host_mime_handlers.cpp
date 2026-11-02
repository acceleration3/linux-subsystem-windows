#include "lsw/host/apps/host_mime_handlers.hpp"

#include <QDir>
#include <QFile>
#include <QMimeDatabase>
#include <QStandardPaths>

namespace lsw::host::apps {

namespace {

constexpr auto kLswPrefix = "lsw-";

QHash<QString, QStringList> section_of(const QString& path, const QString& section) {
    QHash<QString, QStringList> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return out;
    }
    bool inside = false;
    for (const QByteArray& raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inside = line == section;
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (inside && eq > 0 && !line.startsWith(QLatin1Char('#'))) {
            out[line.left(eq).trimmed()] << line.mid(eq + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        }
    }
    return out;
}

QStringList spellings(const QString& type) {
    QStringList out;
    const QMimeType known = QMimeDatabase().mimeTypeForName(type);
    if (known.isValid()) {
        out << known.name() << known.aliases();
    }
    if (!out.contains(type)) {
        out << type;
    }
    out.removeDuplicates();
    return out;
}

}

HostMimeHandlers::Dirs HostMimeHandlers::Dirs::from_environment() {
    Dirs dirs;
    dirs.config_home = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    dirs.data_home = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString data_dirs = qEnvironmentVariable("XDG_DATA_DIRS");
    dirs.data_dirs = (data_dirs.isEmpty() ? QStringLiteral("/usr/local/share:/usr/share") : data_dirs)
                         .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return dirs;
}

HostMimeHandlers::HostMimeHandlers(const Dirs& dirs) : dirs_(dirs) {
    QStringList roots{dirs_.data_home};
    roots << dirs_.data_dirs;
    for (const QString& root : std::as_const(roots)) {
        caches_.push_back(section_of(QDir(root).filePath(QStringLiteral("applications/mimeinfo.cache")),
                                     QStringLiteral("[MIME Cache]")));
    }
    defaults_ = section_of(QDir(dirs_.config_home).filePath(QStringLiteral("mimeapps.list")),
                           QStringLiteral("[Default Applications]"));
}

bool HostMimeHandlers::installed(const QString& id) const {
    const auto known = installed_.constFind(id);
    if (known != installed_.constEnd()) {
        return *known;
    }
    QStringList roots{dirs_.data_home};
    roots << dirs_.data_dirs;
    bool found = false;
    for (const QString& root : std::as_const(roots)) {
        if (QFile::exists(QDir(root).filePath(QStringLiteral("applications/") + id))) {
            found = true;
            break;
        }
    }
    installed_.insert(id, found);
    return found;
}

bool HostMimeHandlers::has_host_handler(const QString& type) const {
    QStringList names = spellings(type);
    if (type != QLatin1String("text/plain") &&
        QMimeDatabase().mimeTypeForName(type).inherits(QStringLiteral("text/plain"))) {
        names << QStringLiteral("text/plain");
    }
    for (const auto& cache : caches_) {
        for (const QString& name : std::as_const(names)) {
            for (const QString& id : cache.value(name)) {
                if (!id.startsWith(QLatin1String(kLswPrefix)) && installed(id)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool HostMimeHandlers::explicit_default(const QString& type, const QString& entry) const {
    for (const QString& name : spellings(type)) {
        if (defaults_.value(name).contains(entry)) {
            return true;
        }
    }
    return false;
}

}
