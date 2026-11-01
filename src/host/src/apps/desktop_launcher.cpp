#include "lsw/host/apps/desktop_launcher.hpp"

#include "lsw/host/apps/host_mime_handlers.hpp"
#include "lsw/host/apps/windows_badge.hpp"
#include "lsw/host/platform/tool_process.hpp"

#include "lsw/base/log.hpp"

#include <algorithm>
#include <utility>

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QMimeDatabase>
#include <QProcess>
#include <QSet>
#include <optional>
#include <QStandardPaths>
#include <QTextStream>

namespace lsw::host::apps {

namespace {

constexpr const char* kEntryPrefix = "lsw-app-";
// The submenu's name is a setting; its id is not, because the id is in
// the file names and renaming those would orphan every entry already
// written. So the id stays and the name is what the menus show.
QString g_category_name = QStringLiteral("Windows");
bool g_publishing = true;
bool g_file_associations = true;
// Whether an entry was written or removed since update-desktop-database
// last ran, and whether it is running now.
bool g_entries_changed = false;
QProcess* g_database_update = nullptr;
bool g_database_update_again = false;
constexpr const char* kCategoryId = "lsw-windows";

QString xdg_data_home() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

QString xdg_config_home() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString applications_dir()     { return QDir(xdg_data_home()).filePath("applications"); }
QString icons_dir()            { return QDir(xdg_data_home()).filePath("icons/lsw-manager"); }
QString directories_dir()      { return QDir(xdg_data_home()).filePath("desktop-directories"); }
QString menus_merged_dir()     { return QDir(xdg_config_home()).filePath("menus/applications-merged"); }

// Stable, filename-safe digest of the Windows-side path. The app's
// own ``id`` field is already a sha1-prefix of the path, but it can
// contain characters XDG dislikes; rehash for the launcher filename
// so we're robust to whatever the source produced.
QString entry_basename(const AppConfig& cfg) {
    return DesktopLauncher::entry_id(cfg.id);
}

QString desktop_path(const AppConfig& cfg) {
    return QDir(applications_dir()).filePath(entry_basename(cfg) + ".desktop");
}

QString icon_path(const AppConfig& cfg) {
    return QDir(icons_dir()).filePath(entry_basename(cfg) + ".png");
}

QString sanitise_name(const QString& s) {
    QString out = s;
    out.replace(QChar('\n'), QChar(' '));
    out.replace(QChar('\r'), QChar(' '));
    return out.trimmed();
}

// Resolves the absolute path of the currently running lsw-manager.
// Used as the ``Exec=`` target so each .desktop points at exactly the
// binary the user just installed from — important when there might
// be both an /usr/bin/lsw-manager and a developer build under
// ~/Coding/.../build/.
QString lsw_manager_executable() {
    QString self = QCoreApplication::applicationFilePath();
    if (!self.isEmpty() && QFile::exists(self)) {
        return self;
    }
    return QStringLiteral("lsw-manager");
}

// kmenuedit hides categories by writing
// ``~/.config/menus/applications-kmenuedit.menu`` with
// ``<Menu><Name>Windows</Name><Deleted/></Menu>``. The .desktop
// files stay on disk but KDE Plasma stops rendering the submenu.
// Surgically strip just our category's deletion entry so a user
// "delete" via kmenuedit doesn't permanently break the auto-sync
// path; other customizations in the same overlay stay intact.
void unhide_windows_category_in_kmenuedit() {
    const QString path = QDir(menus_merged_dir())
        .filePath(QStringLiteral("../applications-kmenuedit.menu"));
    const QString resolved = QDir::cleanPath(path);
    if (!QFile::exists(resolved)) {
        return;  // user hasn't touched kmenuedit — nothing to undo
    }
    QDomDocument doc;
    {
        QFile f(resolved);
        if (!f.open(QIODevice::ReadOnly)) return;
        if (!doc.setContent(&f)) return;
    }
    bool mutated = false;
    QDomNodeList menus = doc.elementsByTagName(QStringLiteral("Menu"));
    // Snapshot to a list — removeChild mutates the live NodeList.
    QList<QDomElement> targets;
    for (int i = 0; i < menus.size(); ++i) {
        QDomElement m = menus.at(i).toElement();
        if (m.isNull()) continue;
        QDomElement name = m.firstChildElement(QStringLiteral("Name"));
        if (name.isNull() || name.text() != g_category_name) continue;
        QDomElement deleted = m.firstChildElement(QStringLiteral("Deleted"));
        if (deleted.isNull()) continue;
        targets.append(m);
    }
    for (QDomElement& m : targets) {
        m.parentNode().removeChild(m);
        mutated = true;
    }
    if (!mutated) return;

    QFile f(resolved);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    doc.save(s, 1);
}

// First-touch installation of the "Windows" category. Cheap to call
// every time install() runs — file writes are skipped if the content
// is already correct.
void ensure_windows_category() {
    QDir().mkpath(directories_dir());
    QDir().mkpath(menus_merged_dir());
    unhide_windows_category_in_kmenuedit();

    const QString dir_path = QDir(directories_dir())
        .filePath(QStringLiteral("%1.directory").arg(kCategoryId));
    const QByteArray dir_body =
        QStringLiteral("[Desktop Entry]\n"
                       "Type=Directory\n"
                       "Name=%1\n"
                       "Icon=preferences-system-windows-actions\n")
            .arg(g_category_name).toUtf8();
    QFile dirf(dir_path);
    bool needs_write = true;
    if (dirf.open(QIODevice::ReadOnly)) {
        needs_write = dirf.readAll() != dir_body;
        dirf.close();
    }
    if (needs_write && dirf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        dirf.write(dir_body);
        dirf.close();
    }

    // applications-merged/lsw-windows.menu. The DOCTYPE + Menu Spec
    // reference are required for KDE Plasma's menu parser to accept
    // the fragment; older docs sometimes omit them and silently
    // ignore the file.
    const QString menu_path = QDir(menus_merged_dir())
        .filePath(QStringLiteral("%1.menu").arg(kCategoryId));
    const QByteArray menu_body =
        QStringLiteral(
            "<!DOCTYPE Menu PUBLIC \"-//freedesktop//DTD Menu 1.0//EN\"\n"
            "  \"http://www.freedesktop.org/standards/menu-spec/menu-1.0.dtd\">\n"
            "<Menu>\n"
            "  <Name>Applications</Name>\n"
            "  <Menu>\n"
            "    <Name>%1</Name>\n"
            "    <Directory>%2.directory</Directory>\n"
            "    <Include>\n"
            "      <Category>%1</Category>\n"
            "    </Include>\n"
            "  </Menu>\n"
            "</Menu>\n")
            .arg(g_category_name, QString::fromLatin1(kCategoryId)).toUtf8();
    QFile mf(menu_path);
    needs_write = true;
    if (mf.open(QIODevice::ReadOnly)) {
        needs_write = mf.readAll() != menu_body;
        mf.close();
    }
    if (needs_write && mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        mf.write(menu_body);
        mf.close();
    }
}

// ``update-desktop-database`` over the applications directory, so the MIME
// handlers just written are in the cache the file managers' Open With reads
// (mimeinfo.cache). Run behind the GUI thread's back, one at a time, and
// skipped when the tool is not installed: the cache is then built by
// whatever else the desktop runs.
void update_desktop_database() {
    if (g_database_update != nullptr) {
        g_database_update_again = true;
        return;
    }
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));
    if (tool.isEmpty()) {
        return;
    }
    g_database_update = new QProcess(QCoreApplication::instance());
    QObject::connect(g_database_update, &QProcess::finished, g_database_update,
                     [](int code, QProcess::ExitStatus) {
                         if (code != 0) {
                             LSW_WARN("update-desktop-database exited with {}", code);
                         }
                         g_database_update->deleteLater();
                         g_database_update = nullptr;
                         if (std::exchange(g_database_update_again, false)) {
                             update_desktop_database();
                         }
                     });
    QObject::connect(g_database_update, &QProcess::errorOccurred, g_database_update,
                     [](QProcess::ProcessError error) {
                         if (error == QProcess::FailedToStart) {
                             g_database_update->deleteLater();
                             g_database_update = nullptr;
                         }
                     });
    g_database_update->start(tool, {applications_dir()});
}

// Written only when it differs, so an unchanged entry neither touches the
// file nor asks for the database to be rebuilt.
bool write_if_changed(const QString& path, const QByteArray& body) {
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == body) {
        return true;
    }
    current.close();
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    out.write(body);
    g_entries_changed = true;
    return true;
}

}  // namespace

QString DesktopLauncher::entry_id(const QString& app_config_id) {
    const QByteArray digest = QCryptographicHash::hash(
        app_config_id.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QString::fromUtf8(kEntryPrefix) + QString::fromUtf8(digest);
}

void DesktopLauncher::set_category(const QString& name) {
    if (!name.trimmed().isEmpty()) {
        g_category_name = name.trimmed();
    }
}

QString DesktopLauncher::category() {
    return g_category_name;
}

void DesktopLauncher::set_publishing(bool enabled) {
    g_publishing = enabled;
}

bool DesktopLauncher::publishing() {
    return g_publishing;
}

void DesktopLauncher::set_file_associations(bool enabled) {
    g_file_associations = enabled;
}

bool DesktopLauncher::file_associations() {
    return g_file_associations;
}

QStringList DesktopLauncher::mime_types_of(const AppConfig& cfg) {
    QStringList types = cfg.mime_types.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString& type : types) {
        type = type.trimmed();
    }
    types << mime_types_of_extensions(cfg.file_extensions);
    QStringList out;
    for (const QString& type : std::as_const(types)) {
        if (!type.isEmpty() && type != QLatin1String("application/octet-stream") && !out.contains(type)) {
            out << type;
        }
    }
    return out;
}

QStringList DesktopLauncher::declared_mime_types(const AppConfig& cfg, const HostMimeHandlers& handlers) {
    // What the person typed is an explicit request: always declared.
    QStringList out;
    for (QString type : cfg.mime_types.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        type = type.trimmed();
        if (!type.isEmpty() && type != QLatin1String("application/octet-stream") && !out.contains(type)) {
            out << type;
        }
    }
    const QString entry = entry_basename(cfg) + QStringLiteral(".desktop");
    for (const QString& type : mime_types_of_extensions(cfg.file_extensions)) {
        // What Windows says it opens, where nothing on this machine does, or
        // where the person's mimeapps.list already chose this entry for it.
        if ((!handlers.has_host_handler(type) || handlers.explicit_default(type, entry)) && !out.contains(type)) {
            out << type;
        }
    }
    return out;
}

QStringList DesktopLauncher::mime_types_of_extensions(const QStringList& extensions) {
    const QMimeDatabase database;
    // What the host's database calls a file of each extension.
    QList<QStringList> candidates;
    for (const QString& extension : extensions) {
        const QString name = QStringLiteral("file") +
                             (extension.startsWith(QLatin1Char('.')) ? QString{} : QStringLiteral(".")) + extension;
        QStringList names;
        for (const QMimeType& type : database.mimeTypesForFileName(name)) {
            if (type.name() != QLatin1String("application/octet-stream") && !names.contains(type.name())) {
                names << type.name();
            }
        }
        candidates.append(names);
    }
    const auto top_level = [](const QString& type) { return type.section(QLatin1Char('/'), 0, 0); };

    // The kind of thing this application opens: the top-level type most of
    // its extensions have one candidate for.
    QHash<QString, int> votes;
    for (const QStringList& names : candidates) {
        if (names.size() == 1) {
            ++votes[top_level(names.first())];
        }
    }
    QString dominant;
    int best = 0;
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it) {
        if (it.value() > best || (it.value() == best && it.key() < dominant)) {
            dominant = it.key();
            best = it.value();
        }
    }

    // Text types the host's database hands out for odd extensions (.asd as
    // Lisp for Word's autosave) are not what a document application opens;
    // these few are.
    static const QSet<QString> kPlainTexts = {
        QStringLiteral("text/plain"), QStringLiteral("text/html"), QStringLiteral("text/csv"),
        QStringLiteral("text/xml"), QStringLiteral("text/markdown"), QStringLiteral("text/rtf"),
        QStringLiteral("text/richtext"), QStringLiteral("text/calendar"), QStringLiteral("text/vcard"),
        QStringLiteral("text/tab-separated-values")};

    // Never declared, for any application: what the host's database calls an
    // extension that Windows uses for a document, and that is not one.
    static const QSet<QString> kNeverDocuments = {QStringLiteral("text/x-ssh-public-key")};

    QStringList out;
    for (QStringList names : candidates) {
        names.removeIf([](const QString& n) { return kNeverDocuments.contains(n); });
        if (dominant != QLatin1String("text")) {
            // An application of documents or media is not offered other kinds of text.
            names.removeIf([&](const QString& n) {
                return top_level(n) == QLatin1String("text") && !kPlainTexts.contains(n);
            });
        }
        if (names.size() > 1) {
            // .pub is Publisher and an SSH key, .ts is video and a Qt
            // linguist file: the application's own kind of thing decides,
            // else the one that is not text, else none - a wrong default is
            // worse than no entry for the type.
            QStringList kept;
            for (const QString& n : std::as_const(names)) {
                if (top_level(n) == dominant) {
                    kept << n;
                }
            }
            if (kept.isEmpty()) {
                for (const QString& n : std::as_const(names)) {
                    if (top_level(n) != QLatin1String("text")) {
                        kept << n;
                    }
                }
            }
            names = kept;
        }
        if (names.size() == 1) {
            out << names.first();
        }
    }
    out.removeDuplicates();
    return out;
}

bool DesktopLauncher::install(const AppConfig& cfg) {
    return install(cfg, HostMimeHandlers{HostMimeHandlers::Dirs::from_environment()});
}

bool DesktopLauncher::install(const AppConfig& cfg, const HostMimeHandlers& handlers) {
    if (cfg.id.isEmpty() || cfg.target().isEmpty()) {
        LSW_WARN("DesktopLauncher::install: empty id/path; skipping");
        return false;
    }
    QDir().mkpath(applications_dir());
    QDir().mkpath(icons_dir());
    ensure_windows_category();

    // Write the icon PNG to disk if we have one. The XDG .desktop
    // Icon= field is followed by absolute path or theme name; the
    // absolute path is the no-config-needed path that works under any
    // icon theme.
    QString icon_abs;
    if (!cfg.icon_png_base64.isEmpty()) {
        // Marked as Windows', so its mirrored windows are told apart from
        // the host's own on the task manager, which shows this icon.
        const QByteArray png =
            WindowsBadge::applied(QByteArray::fromBase64(cfg.icon_png_base64.toUtf8()));
        if (!png.isEmpty()) {
            const QString ip = icon_path(cfg);
            QFile pf(ip);
            if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                pf.write(png);
                pf.close();
                icon_abs = ip;
            } else {
                LSW_WARN("DesktopLauncher: cannot write icon {}", ip.toStdString());
            }
        }
    }

    const QString dp = desktop_path(cfg);
    QString body;
    QTextStream s(&body);
    const QString name = sanitise_name(cfg.name.isEmpty() ? cfg.target() : cfg.name);
    // ``Exec`` quotes the app id in case it contains spaces (Windows
    // paths often do — "C:\\Program Files\\..."). XDG escapes the
    // contained backslashes via doubled escape; we use single quotes
    // around the value so the shell treats them as literal.
    QString exec_id = cfg.id;
    exec_id.replace(QChar('\\'), QStringLiteral("\\\\"));
    exec_id.replace(QChar('\''), QStringLiteral("\\'"));
    // %F: the files a file manager opens with it, which the second
    // process hands to the running manager (see CliParser).
    s << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Version=1.0\n"
      << "Name=" << name << "\n"
      << "Comment=Run " << name << " in the LSW Windows VM\n";
    if (!icon_abs.isEmpty()) {
        s << "Icon=" << icon_abs << "\n";
    }
    s << "Exec=" << lsw_manager_executable() << " --launch '" << exec_id << "' %F\n"
      << "Categories=" << g_category_name << ";\n"
      // StartupWMClass is the X11 grouping hint; on Wayland the taskbar
      // matches against the app id the mirror presents, which is this
      // entry's own, so this is mostly for the launcher-side bounce /
      // startup-id flow on X11.
      << "StartupNotify=true\n"
      << "Terminal=false\n";
    // The Windows file types this application opens that nothing on this
    // machine does, so the application is their handler and a double click
    // on one starts it in Windows. The others are the host's: which of
    // several applications the desktop picks for a type is not something an
    // entry can influence (KDE's choice varies from one rebuild of its cache
    // to the next, whatever preference an entry names), and putting this one
    // among them would sometimes take a person's default. Those are the
    // application's own "Make it the default", an explicit choice.
    if (g_file_associations) {
        if (const QStringList types = declared_mime_types(cfg, handlers); !types.isEmpty()) {
            s << "MimeType=" << types.join(QLatin1Char(';')) << ";\n";
        }
    }
    s.flush();
    if (!write_if_changed(dp, body.toUtf8())) {
        LSW_WARN("DesktopLauncher: cannot write {}", dp.toStdString());
        return false;
    }
    return true;
}

std::optional<DesktopLauncher::DefaultHandler> DesktopLauncher::default_handler(const QString& mime_type) {
    QByteArray out;
    if (!platform::run_tool(QStringLiteral("xdg-mime"),
                            {QStringLiteral("query"), QStringLiteral("default"), mime_type}, 10'000,
                            &out, nullptr)) {
        return std::nullopt;
    }
    const QString entry = QString::fromUtf8(out).trimmed();
    if (entry.isEmpty()) {
        return std::nullopt;
    }
    DefaultHandler handler{entry, QString{}};
    // The entry's own Name, from the applications directories.
    const QString file = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, entry);
    QFile f(file);
    if (!file.isEmpty() && f.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : f.readAll().split('\n')) {
            if (line.startsWith("Name=")) {
                handler.name = QString::fromUtf8(line.mid(5)).trimmed();
                break;
            }
        }
    }
    return handler;
}

bool DesktopLauncher::make_default(const AppConfig& cfg, QString* error) {
    const QStringList types = mime_types_of(cfg);
    if (types.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("it opens no file types");
        }
        return false;
    }
    QStringList arguments{QStringLiteral("default"), entry_basename(cfg) + QStringLiteral(".desktop")};
    arguments << types;
    return platform::run_tool(QStringLiteral("xdg-mime"), arguments, 10'000, nullptr, error);
}

void DesktopLauncher::uninstall(const AppConfig& cfg) {
    QFile::remove(desktop_path(cfg));
    QFile::remove(icon_path(cfg));
}

bool DesktopLauncher::is_installed(const AppConfig& cfg) {
    return QFile::exists(desktop_path(cfg));
}

void DesktopLauncher::sync(const QList<AppConfig>& live) {
    // The caches are read once for the whole pass.
    write_entries(live, HostMimeHandlers{HostMimeHandlers::Dirs::from_environment()});
}

void DesktopLauncher::write_entries(const QList<AppConfig>& live, const HostMimeHandlers& handlers) {
    // First pass: install/refresh entries for every live config. Build
    // the set of basenames we just produced so the prune pass can
    // distinguish "ours, current" from "ours, orphan".
    QSet<QString> live_bases;
    for (const auto& cfg : live) {
        if (cfg.id.isEmpty() || cfg.target().isEmpty()) continue;
        // An application the user un-published, or a manager with
        // publishing off entirely, leaves no basename behind, so the
        // prune pass below removes whatever it had.
        if (!g_publishing || !cfg.publish_entry) continue;
        (void)install(cfg, handlers);
        live_bases.insert(entry_basename(cfg));
    }

    // Second pass: prune. Anything matching ``lsw-app-*.desktop``
    // whose basename isn't in ``live_bases`` belongs to an app that
    // was removed from the manager / no longer exists in the guest.
    QDir apps(applications_dir());
    if (apps.exists()) {
        const QStringList stale = apps.entryList(
            {QStringLiteral("%1*.desktop").arg(kEntryPrefix)},
            QDir::Files);
        for (const QString& name : stale) {
            QString base = name;
            base.chop(QStringLiteral(".desktop").size());
            if (!live_bases.contains(base)) {
                QFile::remove(apps.filePath(name));
                g_entries_changed = true;
            }
        }
    }
    QDir icons(icons_dir());
    if (icons.exists()) {
        const QStringList stale_icons = icons.entryList(
            {QStringLiteral("%1*.png").arg(kEntryPrefix)},
            QDir::Files);
        for (const QString& name : stale_icons) {
            QString base = name;
            base.chop(QStringLiteral(".png").size());
            if (!live_bases.contains(base)) {
                QFile::remove(icons.filePath(name));
            }
        }
    }
    if (std::exchange(g_entries_changed, false)) {
        update_desktop_database();
    }
}

void DesktopLauncher::sync_manager_entry() {
    const QString file_name = QGuiApplication::desktopFileName() + QStringLiteral(".desktop");
    const QString own = QDir(applications_dir()).filePath(file_name);
    const QString own_icon = QDir(icons_dir()).filePath(QStringLiteral("lsw-manager.svg"));
    const QStringList found =
        QStandardPaths::locateAll(QStandardPaths::ApplicationsLocation, file_name);
    if (std::any_of(found.begin(), found.end(), [&](const QString& path) {
            return QFileInfo(path).absoluteFilePath() != QFileInfo(own).absoluteFilePath();
        })) {
        QFile::remove(own);
        QFile::remove(own_icon);
        return;
    }

    QDir().mkpath(applications_dir());
    QDir().mkpath(icons_dir());
    QFile::remove(own_icon);
    if (!QFile::copy(QStringLiteral(":/lsw/lsw-manager.svg"), own_icon)) {
        LSW_WARN("DesktopLauncher: cannot write {}", own_icon.toStdString());
        return;
    }
    // Copied out of the resources read-only; rewritten on the next run.
    QFile::setPermissions(own_icon, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup |
                                        QFile::ReadOther);

    const QByteArray body =
        QStringLiteral("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=LSW Manager\n"
                       "GenericName=Windows Virtual Machine Manager\n"
                       "Comment=Run Windows applications in a managed virtual machine\n"
                       "Exec=%1 %F\n"
                       "Icon=%2\n"
                       "Terminal=false\n"
                       "Categories=System;Emulator;\n"
                       "StartupWMClass=%3\n")
            .arg(lsw_manager_executable(), own_icon, QGuiApplication::desktopFileName())
            .toUtf8();
    QFile entry(own);
    if (entry.open(QIODevice::ReadOnly) && entry.readAll() == body) {
        return;
    }
    entry.close();
    if (!entry.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LSW_WARN("DesktopLauncher: cannot write {}", own.toStdString());
        return;
    }
    entry.write(body);
}

}  // namespace lsw::host::apps
