// The disk size setting, end to end on a real image.
//
// "Grows only" is a promise the Settings page makes and the restart
// keeps: Lifecycle::grow_disk widens a qcow2 between the stop and the
// start, does nothing for a size at or below what the image is, and
// never shrinks one — a shrink under NTFS loses the end of the volume.
// Needs qemu-img; skipped (and passing) without it.

#include "lsw/host/vm/domain_description.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

using lsw::host::vm::DiskImage;
using lsw::host::vm::Lifecycle;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

[[nodiscard]] quint64 gib_of(const QString& path) {
    const auto size = DiskImage::virtual_size(path);
    return size ? (*size >> 30) : 0;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (QStandardPaths::findExecutable(QStringLiteral("qemu-img")).isEmpty()) {
        std::printf("skipped: no qemu-img\n");
        return 0;
    }
    QTemporaryDir dir;
    const QString disk = QDir(dir.path()).filePath(QStringLiteral("disk.qcow2"));
    check(QProcess::execute(QStringLiteral("qemu-img"),
                            {QStringLiteral("create"), QStringLiteral("-q"), QStringLiteral("-f"),
                             QStringLiteral("qcow2"), disk, QStringLiteral("1G")}) == 0,
          "a 1 GiB image to start from");

    QString error;
    check(Lifecycle::grow_disk(disk, 3, &error) && gib_of(disk) == 3, "grows to 3 GiB");
    check(Lifecycle::grow_disk(disk, 2, &error) && gib_of(disk) == 3,
          "a smaller size succeeds and changes nothing");
    check(Lifecycle::grow_disk(disk, 3, &error) && gib_of(disk) == 3,
          "the same size succeeds and changes nothing");
    error.clear();
    check(!Lifecycle::grow_disk(QDir(dir.path()).filePath(QStringLiteral("missing.qcow2")), 3,
                                &error) &&
              !error.isEmpty(),
          "a missing image fails with a reason");

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
