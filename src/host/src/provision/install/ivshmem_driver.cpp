#include "lsw/host/provision/install/ivshmem_driver.hpp"

#include "lsw/base/log.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>

namespace lsw::host::provision::install {

namespace {

// A driver directory is only useful to offline injection if the catalog
// and the binary are beside the .inf; DISM rejects an .inf whose
// CatalogFile is missing, and an unsigned install is not an option on
// x64 Windows.
constexpr const char* kRequiredSuffixes[] = {".inf", ".cat", ".sys"};

// Looking Glass ships its host package as an NSIS installer. 7z and
// innoextract can both unpack one; neither is worth making a hard
// dependency, so whichever is present wins and the absence of both is
// reported rather than worked around.
[[nodiscard]] QString find_program(const QString& name) {
    return QStandardPaths::findExecutable(name);
}

// Copy every driver file found anywhere under ``root`` into ``dest``.
// NSIS packages nest the driver a few levels down and the exact layout
// has changed between releases, so search rather than hardcode a path.
[[nodiscard]] bool harvest_driver_files(const QString& root, const QString& dest) {
    QDir().mkpath(dest);
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    int copied = 0;
    while (it.hasNext()) {
        const QFileInfo info{it.next()};
        const QString lower = info.fileName().toLower();
        if (!lower.startsWith("ivshmem")) {
            continue;
        }
        const QString target = QDir(dest).filePath(info.fileName());
        QFile::remove(target);
        if (QFile::copy(info.absoluteFilePath(), target)) {
            ++copied;
        }
    }
    return copied > 0;
}

// Unpack ``package`` into ``dest`` with whichever extractor is present.
// Returns false when neither is installed (reported by the caller).
[[nodiscard]] bool unpack_into(const QString& package, const QString& dest) {
    const QString seven_zip = find_program(QStringLiteral("7z"));
    const QString innoextract = find_program(QStringLiteral("innoextract"));
    QString program;
    QStringList args;
    if (!seven_zip.isEmpty()) {
        program = seven_zip;
        args = {QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-o") + dest, package};
    } else if (!innoextract.isEmpty()) {
        program = innoextract;
        args = {QStringLiteral("-e"), QStringLiteral("-d"), dest, package};
    } else {
        return false;
    }
    QDir().mkpath(dest);
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(120000) || proc.exitCode() != 0) {
        LSW_WARN("ivshmem: {} failed to unpack {} (exit {})", program.toStdString(),
                 package.toStdString(), proc.exitCode());
        return false;
    }
    return true;
}

// Archives that might contain the driver one level further down.
[[nodiscard]] QStringList nested_archives(const QString& root) {
    QStringList out;
    QDirIterator it(root, {QStringLiteral("*.exe"), QStringLiteral("*.zip"),
                           QStringLiteral("*.msi")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        out.append(it.next());
    }
    return out;
}

}  // namespace

QString IvshmemDriver::cache_path(const QString& cache_dir) {
    return QDir(cache_dir).filePath(QStringLiteral("ivshmem"));
}

bool IvshmemDriver::looks_like_driver_dir(const QString& dir) {
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        return false;
    }
    const auto names = QDir(dir).entryList(QDir::Files);
    for (const char* suffix : kRequiredSuffixes) {
        const bool found = std::any_of(names.begin(), names.end(), [&](const QString& n) {
            return n.toLower().endsWith(QLatin1String(suffix));
        });
        if (!found) {
            return false;
        }
    }
    return true;
}

bool IvshmemDriver::extract_from_package(const QString& package_path,
                                         const QString& dest_dir) {
    if (!QFileInfo::exists(package_path)) {
        return false;
    }
    QTemporaryDir unpacked;
    if (!unpacked.isValid()) {
        LSW_ERROR("ivshmem: could not create a temp dir to unpack {}",
                  package_path.toStdString());
        return false;
    }

    if (find_program(QStringLiteral("7z")).isEmpty()
        && find_program(QStringLiteral("innoextract")).isEmpty()) {
        LSW_WARN("ivshmem: no extractor available (install p7zip or innoextract), or set "
                 "LSW_IVSHMEM_DRIVER_DIR to an unpacked driver directory");
        return false;
    }
    if (!unpack_into(package_path, unpacked.path())) {
        return false;
    }
    if (harvest_driver_files(unpacked.path(), dest_dir)) {
        return looks_like_driver_dir(dest_dir);
    }

    // Nothing at the top level. The download from looking-glass.io is a
    // ZIP whose only useful member is the NSIS installer, so the driver
    // sits one archive further in. One extra level, not a recursive
    // descent: two is what the real packaging needs, and an unbounded
    // walk over arbitrary downloaded archives is not something to invite.
    for (const QString& nested : nested_archives(unpacked.path())) {
        QTemporaryDir inner;
        if (!inner.isValid() || !unpack_into(nested, inner.path())) {
            continue;
        }
        if (harvest_driver_files(inner.path(), dest_dir)) {
            LSW_INFO("ivshmem: driver found inside {} within {}",
                     QFileInfo(nested).fileName().toStdString(),
                     QFileInfo(package_path).fileName().toStdString());
            return looks_like_driver_dir(dest_dir);
        }
    }

    LSW_WARN("ivshmem: no ivshmem* files inside {}", package_path.toStdString());
    return false;
}

QString IvshmemDriver::locate(const QString& cache_dir) {
    // 1. Explicit override. Checked first so a broken cache can always be
    //    stepped around without clearing anything.
    QString override_dir =
        QString::fromLocal8Bit(qgetenv("LSW_IVSHMEM_DRIVER_DIR")).trimmed();
    if (!override_dir.isEmpty()) {
        if (looks_like_driver_dir(override_dir)) {
            LSW_INFO("ivshmem driver: using LSW_IVSHMEM_DRIVER_DIR={}",
                     override_dir.toStdString());
            return override_dir;
        }
        LSW_WARN("ivshmem driver: LSW_IVSHMEM_DRIVER_DIR={} does not contain "
                 "ivshmem.inf/.cat/.sys — ignoring it",
                 override_dir.toStdString());
    }

    // 2. Already unpacked by a previous run (or dropped in by hand).
    QString cached = cache_path(cache_dir);
    if (looks_like_driver_dir(cached)) {
        return cached;
    }

    // 3. A package sitting in the cache, waiting to be unpacked.
    const QDir dir{cache_dir};
    const auto packages = dir.entryInfoList(
        {QStringLiteral("looking-glass-host*.exe"), QStringLiteral("looking-glass-host*.zip"),
         QStringLiteral("*ivshmem*.zip")},
        QDir::Files, QDir::Time);
    for (const auto& package : packages) {
        if (extract_from_package(package.absoluteFilePath(), cached)) {
            LSW_INFO("ivshmem driver: unpacked from {}",
                     package.fileName().toStdString());
            return cached;
        }
    }

    LSW_WARN("ivshmem driver not found — the guest will boot WITHOUT the shared-memory "
             "device, so window frames and the shmem channel are unavailable. Put an "
             "unpacked driver in {} or set LSW_IVSHMEM_DRIVER_DIR.",
             cached.toStdString());
    return {};
}

}  // namespace lsw::host::provision::install
