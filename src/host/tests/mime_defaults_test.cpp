// Which file types an entry of LSW's declares. It must never take a type a
// host application opens, because the desktop's choice among several
// handlers is not one an entry can steer (KDE's varies from one rebuild of
// its cache to the next), and a double click on a .xlsx must not start
// Excel in Windows for somebody who has ONLYOFFICE. So an entry declares
// the types nothing else handles, what the person typed, and what their
// mimeapps.list already names it for; and nothing is written anywhere.
// Also the extension to MIME type rule that keeps .pub away from SSH keys.

#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/apps/host_mime_handlers.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

using lsw::host::apps::AppConfig;
using lsw::host::apps::DesktopLauncher;
using lsw::host::apps::HostMimeHandlers;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void write(const QString& path, const QByteArray& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text);
    }
}

QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};
    QTemporaryDir tree;
    const QString root = tree.path();
    HostMimeHandlers::Dirs dirs;
    dirs.config_home = root + QStringLiteral("/config");
    dirs.data_home = root + QStringLiteral("/data");
    dirs.data_dirs = {root + QStringLiteral("/flatpak"), root + QStringLiteral("/usr")};

    // The applications that exist (one cached name has no file), and what the
    // caches say opens what.
    for (const QString& id : {QStringLiteral("onlyoffice.desktop"), QStringLiteral("vlc.desktop"),
                              QStringLiteral("kate.desktop"), QStringLiteral("ark.desktop")}) {
        write(root + QStringLiteral("/flatpak/applications/") + id, "[Desktop Entry]\n");
        write(root + QStringLiteral("/usr/applications/") + id, "[Desktop Entry]\n");
    }
    write(root + QStringLiteral("/data/applications/lsw-app-0123.desktop"), "[Desktop Entry]\nMimeType=application/vnd.ms-publisher;\n");
    write(root + QStringLiteral("/data/applications/mimeinfo.cache"),
          "[MIME Cache]\nimage/gif=lsw-app-0123.desktop;\napplication/vnd.ms-publisher=lsw-app-0123.desktop;\n");
    write(root + QStringLiteral("/flatpak/applications/mimeinfo.cache"),
          "[MIME Cache]\n"
          "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet=gone.desktop;onlyoffice.desktop;\n"
          "text/plain=kate.desktop;\n"
          "application/vnd.ms-gone=gone.desktop;\n");
    write(root + QStringLiteral("/usr/applications/mimeinfo.cache"),
          "[MIME Cache]\n"
          "video/x-matroska=vlc.desktop;\n"
          "application/zip=ark.desktop;\n");
    const QByteArray mimeapps = "[Default Applications]\nimage/gif=lsw-app-0123.desktop;\n";
    write(dirs.config_home + QStringLiteral("/mimeapps.list"), mimeapps);

    const HostMimeHandlers host{dirs};
    const QString xlsx = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    check(host.has_host_handler(xlsx), "a type another application opens has a host handler");
    check(!host.has_host_handler(QStringLiteral("application/vnd.ms-publisher")),
          "a type only LSW's entry lists has none: lsw- entries are ignored");
    check(!host.has_host_handler(QStringLiteral("application/vnd.ms-gone")),
          "a handler that is not installed is ignored");
    check(host.has_host_handler(QStringLiteral("video/x-matroska")) && host.has_host_handler(QStringLiteral("video/matroska")),
          "the alias counts both ways: cached as x-matroska, asked as matroska");
    check(host.has_host_handler(QStringLiteral("text/x-log")), "a text type is handled through text/plain");
    check(host.has_host_handler(QStringLiteral("text/plain")), "text/plain itself");
    check(!host.has_host_handler(QStringLiteral("application/vnd.ms-excel")),
          "a type that is a zip is not handled by the archiver: only the type, its aliases and text/plain count");
    check(!host.has_host_handler(QStringLiteral("application/x-lsw-nothing")), "an unknown type has no handler");
    check(host.explicit_default(QStringLiteral("image/gif"), QStringLiteral("lsw-app-0123.desktop")) &&
              !host.explicit_default(QStringLiteral("image/gif"), QStringLiteral("other.desktop")) &&
              !host.explicit_default(QStringLiteral("image/png"), QStringLiteral("lsw-app-0123.desktop")),
          "mimeapps.list's default for a type, for that entry");

    // What an entry declares. Its id makes its entry name lsw-app-<hash>; the
    // explicit default below names that one.
    AppConfig excel;
    excel.id = QStringLiteral("excel-1");
    excel.name = QStringLiteral("Excel");
    excel.path = QStringLiteral("C:\\Office\\EXCEL.EXE");
    excel.file_extensions = {QStringLiteral(".xlsx"), QStringLiteral(".xls"), QStringLiteral(".pub")};
    const QStringList declared = DesktopLauncher::declared_mime_types(excel, host);
    check(!declared.contains(xlsx), "a type a host application opens is not declared");
    check(declared.contains(QStringLiteral("application/vnd.ms-excel")),
          "xlsx's neighbour that only a zip handler could claim is declared: Ark is not an xls handler");
    check(declared.contains(QStringLiteral("application/vnd.ms-publisher")) &&
              !declared.contains(QStringLiteral("text/x-ssh-public-key")),
          "an LSW-only type is declared, and .pub is Publisher, never an SSH key");
    excel.mime_types = QStringLiteral("video/x-matroska\napplication/x-typed");
    const QStringList with_typed = DesktopLauncher::declared_mime_types(excel, host);
    check(with_typed.contains(QStringLiteral("video/x-matroska")) && with_typed.contains(QStringLiteral("application/x-typed")),
          "what the person typed is declared, host handler or not");
    // An explicit default of the entry's own is declared too.
    AppConfig gif;
    gif.id = QStringLiteral("gif-viewer");
    gif.path = QStringLiteral("C:\\Gif.exe");
    gif.file_extensions = {QStringLiteral(".gif")};
    write(dirs.config_home + QStringLiteral("/mimeapps.list"),
          "[Default Applications]\nimage/gif=" + (DesktopLauncher::entry_id(gif.id) + QStringLiteral(".desktop")).toUtf8() + "\n");
    write(root + QStringLiteral("/flatpak/applications/mimeinfo.cache"), "[MIME Cache]\nimage/gif=kate.desktop;\n");
    const HostMimeHandlers chosen{dirs};
    check(chosen.has_host_handler(QStringLiteral("image/gif")) &&
              DesktopLauncher::declared_mime_types(gif, chosen).contains(QStringLiteral("image/gif")),
          "a type the person's mimeapps.list names this entry for is declared though a host app handles it");
    HostMimeHandlers::Dirs without = dirs;
    without.config_home = root + QStringLiteral("/no-config");
    check(!DesktopLauncher::declared_mime_types(gif, HostMimeHandlers{without}).contains(QStringLiteral("image/gif")),
          "and without that choice it is not");

    // A host application installed later takes the type out at the next sync.
    write(root + QStringLiteral("/usr/applications/mimeinfo.cache"),
          "[MIME Cache]\napplication/vnd.ms-publisher=kate.desktop;\n");
    const HostMimeHandlers later{dirs};
    check(!DesktopLauncher::declared_mime_types(excel, later).contains(QStringLiteral("application/vnd.ms-publisher")),
          "a handler installed later removes the type, with nothing remembered");
    check(read(dirs.config_home + QStringLiteral("/mimeapps.list")).contains("[Default Applications]"),
          "and mimeapps.list was only read");
    (void)mimeapps;

    // ---- which MIME types an application has -------------------------
    const auto words = DesktopLauncher::mime_types_of_extensions(
        {QStringLiteral(".doc"), QStringLiteral(".docx"), QStringLiteral(".docm"), QStringLiteral(".dot"),
         QStringLiteral(".dotx"), QStringLiteral(".rtf"), QStringLiteral(".asd"), QStringLiteral(".txt")});
    check(words.contains(QStringLiteral("application/msword-template")) && !words.contains(QStringLiteral("text/vnd.graphviz")),
          ".dot is Word's template and not graphviz for a document application");
    check(!words.contains(QStringLiteral("text/x-common-lisp")),
          "Word's autosave .asd is not Lisp: odd text types are not offered to a document application");
    check(words.contains(QStringLiteral("text/plain")), "plain text still is");
    check(DesktopLauncher::mime_types_of_extensions({QStringLiteral(".pub")}) ==
              QStringList{QStringLiteral("application/vnd.ms-publisher")},
          ".pub is Publisher, not an SSH key, even with nothing else to go by");
    const auto publisher_all = DesktopLauncher::mime_types_of_extensions(
        {QStringLiteral(".pub"), QStringLiteral(".pubx"), QStringLiteral(".puz")});
    check(!publisher_all.contains(QStringLiteral("text/x-ssh-public-key")),
          "Publisher's extensions, together, do not declare an SSH key");
    const auto powerpoint = DesktopLauncher::mime_types_of_extensions(
        {QStringLiteral(".ppt"), QStringLiteral(".pptx"), QStringLiteral(".pot"), QStringLiteral(".potx")});
    check(powerpoint.contains(QStringLiteral("application/vnd.ms-powerpoint")) &&
              !powerpoint.contains(QStringLiteral("text/x-gettext-translation-template")),
          ".pot is PowerPoint's, not gettext's");
    const auto player = DesktopLauncher::mime_types_of_extensions(
        {QStringLiteral(".mkv"), QStringLiteral(".flac"), QStringLiteral(".ts"), QStringLiteral(".mp3")});
    check(player.contains(QStringLiteral("video/mp2t")) &&
              !player.contains(QStringLiteral("text/vnd.trolltech.linguist")),
          ".ts is video for a media player, not Qt linguist");
    const auto editor = DesktopLauncher::mime_types_of_extensions(
        {QStringLiteral(".txt"), QStringLiteral(".md"), QStringLiteral(".cpp"), QStringLiteral(".ts")});
    check(editor.size() >= 3, "a text application keeps its text types");
    check(DesktopLauncher::mime_types_of_extensions({QStringLiteral(".nosuchext")}).isEmpty(),
          "an extension the database does not know declares nothing");

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
