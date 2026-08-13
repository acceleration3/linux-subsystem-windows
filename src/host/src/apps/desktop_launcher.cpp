#include "lsw/host/apps/desktop_launcher.hpp"

#include "lsw/base/log.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

namespace lsw::host::apps {

namespace {

constexpr const char* kEntryPrefix = "lsw-app-";
constexpr const char* kCategoryName = "Windows";
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
    const QByteArray digest = QCryptographicHash::hash(
        cfg.id.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QString::fromUtf8(kEntryPrefix) + QString::fromUtf8(digest);
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
    const QString self = QCoreApplication::applicationFilePath();
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
        if (name.isNull() || name.text() != QStringLiteral("Windows")) continue;
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
            .arg(kCategoryName).toUtf8();
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
            .arg(kCategoryName, kCategoryId).toUtf8();
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

}  // namespace

bool DesktopLauncher::install(const AppConfig& cfg) {
    if (cfg.id.isEmpty() || cfg.path.isEmpty()) {
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
        const QByteArray png =
            QByteArray::fromBase64(cfg.icon_png_base64.toUtf8());
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
    QFile df(dp);
    if (!df.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LSW_WARN("DesktopLauncher: cannot write {}", dp.toStdString());
        return false;
    }
    QTextStream s(&df);
    s.setEncoding(QStringConverter::Utf8);
    const QString name = sanitise_name(cfg.name.isEmpty() ? cfg.path : cfg.name);
    // ``Exec`` quotes the app id in case it contains spaces (Windows
    // paths often do — "C:\\Program Files\\..."). XDG escapes the
    // contained backslashes via doubled escape; we use single quotes
    // around the value so the shell treats them as literal.
    QString exec_id = cfg.id;
    exec_id.replace(QChar('\\'), QStringLiteral("\\\\"));
    exec_id.replace(QChar('\''), QStringLiteral("\\'"));
    s << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Version=1.0\n"
      << "Name=" << name << "\n"
      << "Comment=Run " << name << " in the LSW Windows VM\n";
    if (!icon_abs.isEmpty()) {
        s << "Icon=" << icon_abs << "\n";
    }
    s << "Exec=" << lsw_manager_executable() << " --launch '" << exec_id << "'\n"
      << "Categories=" << kCategoryName << ";\n"
      // StartupWMClass is the X11 grouping hint; on Wayland the
      // taskbar matches against the per-window app_id stamped by
      // WindowView::setVisible (``lsw-window-<hwnd>``), so this is
      // mostly for the launcher-side bounce / startup-id flow on X11.
      << "StartupNotify=true\n"
      << "Terminal=false\n";
    df.close();
    return true;
}

void DesktopLauncher::uninstall(const AppConfig& cfg) {
    QFile::remove(desktop_path(cfg));
    QFile::remove(icon_path(cfg));
}

bool DesktopLauncher::is_installed(const AppConfig& cfg) {
    return QFile::exists(desktop_path(cfg));
}

void DesktopLauncher::sync(const QList<AppConfig>& live) {
    // First pass: install/refresh entries for every live config. Build
    // the set of basenames we just produced so the prune pass can
    // distinguish "ours, current" from "ours, orphan".
    QSet<QString> live_bases;
    for (const auto& cfg : live) {
        if (cfg.id.isEmpty() || cfg.path.isEmpty()) continue;
        (void)install(cfg);
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
}

}  // namespace lsw::host::apps
