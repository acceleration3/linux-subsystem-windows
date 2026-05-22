#include "lsw/manager/ui/manager/taskbar_entry.hpp"

#include "lsw/log.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace lsw::manager::ui::manager {

namespace {

constexpr const char* kEntryPrefix = "lsw-window-";

QString applications_dir() {
    const QString share =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(share).filePath(QStringLiteral("applications"));
}

QString icons_dir() {
    const QString cache =
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(cache).filePath(QStringLiteral("lsw-manager/icons"));
}

QString desktop_path_for(std::uint64_t hwnd) {
    return QDir(applications_dir())
        .filePath(QStringLiteral("%1%2.desktop").arg(kEntryPrefix).arg(hwnd));
}

QString icon_path_for(std::uint64_t hwnd) {
    return QDir(icons_dir())
        .filePath(QStringLiteral("%1.png").arg(hwnd));
}

// The Desktop Entry spec's "Name" field is a unicode string, but
// ``localestring`` keys can't contain raw newlines and accept very
// little escaping. Strip the only chars that actually break parsers.
QString sanitise_name(const QString& s) {
    QString out = s;
    out.replace(QChar('\n'), QChar(' '));
    out.replace(QChar('\r'), QChar(' '));
    return out.trimmed();
}

}  // namespace

void TaskbarEntry::register_window(std::uint64_t hwnd, const QString& title,
                                   const QByteArray& icon_png) {
    QDir().mkpath(applications_dir());
    QDir().mkpath(icons_dir());

    QString icon_abs;
    if (!icon_png.isEmpty()) {
        const QString icon_path = icon_path_for(hwnd);
        QFile pf(icon_path);
        if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            pf.write(icon_png);
            pf.close();
            icon_abs = icon_path;
        } else {
            LSW_WARN("TaskbarEntry: cannot write icon {}",
                     icon_path.toStdString());
        }
    }

    const QString desktop_path = desktop_path_for(hwnd);
    QFile df(desktop_path);
    if (!df.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LSW_WARN("TaskbarEntry: cannot write {}", desktop_path.toStdString());
        return;
    }
    QTextStream s(&df);
    s.setEncoding(QStringConverter::Utf8);
    s << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Version=1.0\n"
      << "Name=" << sanitise_name(title) << "\n"
      << "Comment=LSW guest window mirror\n";
    if (!icon_abs.isEmpty()) {
        s << "Icon=" << icon_abs << "\n";
    }
    // ``Exec`` is mandatory in the spec but never invoked — the entry
    // exists solely to attach metadata to a live window via
    // ``StartupWMClass`` + filename → app_id matching. ``true`` is the
    // POSIX no-op so a stray launcher click stays harmless.
    s << "Exec=true\n"
      << "NoDisplay=true\n"
      << "StartupWMClass=" << kEntryPrefix << hwnd << "\n";
    df.close();
}

void TaskbarEntry::unregister_window(std::uint64_t hwnd) {
    QFile::remove(desktop_path_for(hwnd));
    QFile::remove(icon_path_for(hwnd));
}

void TaskbarEntry::cleanup_stale_entries() {
    QDir apps(applications_dir());
    if (apps.exists()) {
        const QStringList stale = apps.entryList(
            {QStringLiteral("%1*.desktop").arg(kEntryPrefix)},
            QDir::Files);
        for (const QString& name : stale) {
            QFile::remove(apps.filePath(name));
        }
    }
    QDir icons(icons_dir());
    if (icons.exists()) {
        const QStringList stale =
            icons.entryList({QStringLiteral("*.png")}, QDir::Files);
        for (const QString& name : stale) {
            QFile::remove(icons.filePath(name));
        }
    }
}

}  // namespace lsw::manager::ui::manager
