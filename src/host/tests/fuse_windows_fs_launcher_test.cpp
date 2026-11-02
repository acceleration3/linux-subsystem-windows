#include "lsw/host/platform/fuse_windows_fs_launcher.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

using lsw::host::platform::FuseWindowsFsLauncher;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

}  // namespace

int main() {
    QTemporaryDir home;
    const QString point = home.filePath(QStringLiteral("Windows"));
    check(FuseWindowsFsLauncher::prepare_mount_point(point).isEmpty() && QDir(point).exists(),
          "a missing mount point is created");
    check(FuseWindowsFsLauncher::prepare_mount_point(point).isEmpty(), "an empty one is accepted");
    QFile hidden{point + QStringLiteral("/.keep")};
    hidden.open(QIODevice::WriteOnly);
    hidden.close();
    check(FuseWindowsFsLauncher::prepare_mount_point(point).contains(QStringLiteral("not empty")),
          "a folder holding anything, even a hidden file, is refused");
    QFile file{home.filePath(QStringLiteral("plain"))};
    file.open(QIODevice::WriteOnly);
    file.close();
    check(FuseWindowsFsLauncher::prepare_mount_point(file.fileName())
              .contains(QStringLiteral("not a folder")),
          "a file in the way is refused");
    return g_failures == 0 ? 0 : 1;
}
