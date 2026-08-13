// A4: the host can get an ivshmem driver without a VM existing.
//
// The staging half — answer file asks for \ivshmem, files land on the
// ISO — is covered by autounattend_test. What was missing was everything
// before it: nothing ever put a package where ``locate`` looks, so on a
// clean machine the scan always came up empty and the guest booted
// without the device.
//
// This exercises the discovery path with no network: drop a package in
// the cache, ask ``locate``, and require a directory that
// ``looks_like_driver_dir`` accepts. It also covers the nested case,
// which is the shape the real download actually has — looking-glass.io
// serves a ZIP whose useful member is an NSIS installer, so the driver
// is two archives deep.

#include "lsw/host/provision/install/ivshmem_driver.hpp"
#include "lsw/host/provision/iso/catalog.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

using namespace lsw::host::provision::install;
using lsw::host::provision::iso::IsoCatalog;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

[[nodiscard]] QString seven_zip() {
    return QStandardPaths::findExecutable(QStringLiteral("7z"));
}

// A directory holding the three files an offline injection needs.
void write_driver_files(const QString& dir) {
    QDir().mkpath(dir);
    for (const char* name : {"ivshmem.inf", "ivshmem.cat", "ivshmem.sys"}) {
        QFile f{QDir(dir).filePath(QLatin1String(name))};
        if (f.open(QIODevice::WriteOnly)) {
            f.write("stub");
        }
    }
}

// ``7z a <archive> <contents>/.`` — the trailing /. keeps the archive
// flat rather than nesting the temp dir's name.
[[nodiscard]] bool make_archive(const QString& archive, const QString& contents) {
    QProcess p;
    p.start(seven_zip(), {QStringLiteral("a"), QStringLiteral("-y"), archive,
                          QDir(contents).filePath(QStringLiteral("."))});
    return p.waitForFinished(60000) && p.exitCode() == 0;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    // The download step and the discovery step have to agree on the
    // filename or a fetched package is invisible to the scan that is
    // supposed to find it.
    check(IsoCatalog::looking_glass_host_filename().startsWith(
              QStringLiteral("looking-glass-host")),
          "the download filename matches the pattern locate() scans for");
    check(IsoCatalog::looking_glass_host_url().startsWith(QStringLiteral("https://")),
          "the host package URL is https");

    // An empty cache must report nothing rather than something broken.
    {
        QTemporaryDir empty;
        check(IvshmemDriver::locate(empty.path()).isEmpty(),
              "an empty cache locates no driver");
    }

    // An already-unpacked cache directory is used as-is, no extractor
    // needed. This is the path a second install takes.
    {
        QTemporaryDir cache;
        write_driver_files(IvshmemDriver::cache_path(cache.path()));
        const QString found = IvshmemDriver::locate(cache.path());
        check(found == IvshmemDriver::cache_path(cache.path()),
              "an unpacked cache directory is found");
        check(IvshmemDriver::looks_like_driver_dir(found),
              "and it is accepted as a driver directory");
    }

    if (seven_zip().isEmpty()) {
        std::printf("skip  archive extraction checks: 7z not installed\n");
        return g_failures == 0 ? 0 : 1;
    }

    // A package in the cache, one archive deep.
    {
        QTemporaryDir cache;
        QTemporaryDir contents;
        write_driver_files(contents.path());
        const QString pkg = QDir(cache.path()).filePath(QStringLiteral("ivshmem-driver.zip"));
        check(make_archive(pkg, contents.path()), "built a one-level package");

        const QString found = IvshmemDriver::locate(cache.path());
        check(!found.isEmpty(), "locate() unpacks a package sitting in the cache");
        check(IvshmemDriver::looks_like_driver_dir(found),
              "the unpacked directory is accepted as a driver directory");
    }

    // The shape the real download has: an outer archive whose only
    // useful member is the installer that actually contains the driver.
    {
        QTemporaryDir cache;
        QTemporaryDir inner_contents;
        write_driver_files(inner_contents.path());

        QTemporaryDir middle;
        // Named .exe because the real one is an NSIS installer, and
        // because that is what the nested scan looks for.
        const QString installer =
            QDir(middle.path()).filePath(QStringLiteral("looking-glass-host-setup.exe"));
        check(make_archive(installer, inner_contents.path()),
              "built the inner installer archive");

        const QString outer =
            QDir(cache.path()).filePath(IsoCatalog::looking_glass_host_filename());
        check(make_archive(outer, middle.path()), "built the outer download archive");

        const QString found = IvshmemDriver::locate(cache.path());
        check(!found.isEmpty(),
              "locate() reaches a driver nested two archives deep — the real layout");
        check(IvshmemDriver::looks_like_driver_dir(found),
              "the nested result is accepted as a driver directory");
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("OK");
    return 0;
}
