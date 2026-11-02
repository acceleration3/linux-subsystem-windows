#include "lsw/host/platform/guest_bookmark_cleanup.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

using lsw::host::platform::GuestBookmarkCleanup;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void write(const QString& path, const QByteArray& text) {
    QFile file{path};
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text);
    }
}

QByteArray read(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QByteArray bookmark(const QByteArray& href, const QByteArray& title) {
    return "<bookmark href=\"" + href + "\"><title>" + title + "</title></bookmark>\n";
}

QByteArray places(const QByteArray& body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xbel version=\"1.0\">\n" + body +
           "</xbel>\n";
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};
    QTemporaryDir dir;
    const QString xbel = dir.filePath(QStringLiteral("user-places.xbel"));
    const QString gtk = dir.filePath(QStringLiteral("bookmarks"));

    const QByteArray guest = bookmark("smb://lsw:lsw@127.0.0.1:5445/C$", "Windows");
    const QByteArray home = bookmark("file:///home/user", "Home");
    const QByteArray nas = bookmark("smb://me:pw@192.168.1.5/share", "NAS");
    const QByteArray renamed = bookmark("smb://lsw:lsw@127.0.0.1:5445/C$", "My PC");
    const QByteArray plain = bookmark("file:///home/user/Windows", "Windows");

    write(xbel, places(home + guest + nas + renamed + plain));
    check(GuestBookmarkCleanup::remove_from_places(xbel),
          "Dolphin: the guest's entry is found and removed");
    const QByteArray after = read(xbel);
    check(after.count("127.0.0.1:5445") == 1,
          "Dolphin: the entry titled Windows pointing at the guest is gone");
    check(after.contains("file:///home/user\"") && after.contains("192.168.1.5") &&
              after.contains("My PC") && after.contains("file:///home/user/Windows"),
          "Dolphin: other entries stay, including a Windows entry that is a plain folder and "
          "another title");
    check(after.count("<bookmark ") == 4, "Dolphin: exactly one entry was removed");
    check(!GuestBookmarkCleanup::remove_from_places(xbel), "Dolphin: a second run changes nothing");

    const QByteArray untouched = places(home + nas);
    write(xbel, untouched);
    check(!GuestBookmarkCleanup::remove_from_places(xbel) && read(xbel) == untouched,
          "Dolphin: a file without the entry is not rewritten");
    write(xbel, "this is not xml");
    check(!GuestBookmarkCleanup::remove_from_places(xbel) && read(xbel) == "this is not xml",
          "Dolphin: an unparseable file is left alone");
    check(!GuestBookmarkCleanup::remove_from_places(dir.filePath(QStringLiteral("missing.xbel"))),
          "Dolphin: a missing file is fine");

    write(gtk, "file:///home/user/Documents Documents\n"
               "smb://lsw:lsw@127.0.0.1:5445/C$ Windows\n"
               "smb://me:pw@192.168.1.5/share NAS\n"
               "smb://lsw:lsw@127.0.0.1:5445/Users Windows\n"
               "smb://lsw:lsw@127.0.0.1:5445/C$ Something else\n");
    check(GuestBookmarkCleanup::remove_from_gtk(gtk), "GTK: the guest's entries are removed");
    check(read(gtk) == "file:///home/user/Documents Documents\n"
                       "smb://me:pw@192.168.1.5/share NAS\n"
                       "smb://lsw:lsw@127.0.0.1:5445/C$ Something else\n",
          "GTK: only lines titled Windows at the guest's address went, the rest is intact");
    check(!GuestBookmarkCleanup::remove_from_gtk(gtk), "GTK: a second run changes nothing");
    check(!GuestBookmarkCleanup::remove_from_gtk(dir.filePath(QStringLiteral("missing"))),
          "GTK: a missing file is fine");

    return g_failures == 0 ? 0 : 1;
}
