#include "lsw/host/provision/install/ivshmem_driver.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/tool_process.hpp"
#include "lsw/host/provision/install/seven_zip.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
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

// Looking Glass ships its host package as an NSIS installer. 7-Zip and
// innoextract can both unpack one; neither is worth making a hard
// dependency, so whichever is present wins and the absence of both is
// reported rather than worked around. 7-Zip is found the way every
// other reader of an archive here finds it, under whichever name the
// distribution installs.
[[nodiscard]] QString find_innoextract() {
    return QStandardPaths::findExecutable(QStringLiteral("innoextract"));
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
    constexpr int kTimeoutMs = 120000;
    QDir().mkpath(dest);
    QString error;
    bool ok = false;
    if (!find_seven_zip().isEmpty()) {
        ok = run_seven_zip(
            {QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-o") + dest, package},
            kTimeoutMs, &error);
    } else if (const QString innoextract = find_innoextract(); !innoextract.isEmpty()) {
        ok = platform::run_tool(innoextract,
                                {QStringLiteral("-e"), QStringLiteral("-d"), dest, package},
                                kTimeoutMs, nullptr, &error);
    } else {
        return false;
    }
    if (!ok) {
        LSW_WARN("ivshmem: could not unpack {}: {}", package.toStdString(), error.toStdString());
    }
    return ok;
}

// Archives that might contain the driver one level further down.
[[nodiscard]] QStringList nested_archives(const QString& root) {
    QStringList out;
    QDirIterator it(root,
                    {QStringLiteral("*.exe"), QStringLiteral("*.zip"), QStringLiteral("*.msi")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        out.append(it.next());
    }
    return out;
}

}  // namespace

bool IvshmemDriver::has_extractor() {
    return !find_seven_zip().isEmpty() || !find_innoextract().isEmpty();
}

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

bool IvshmemDriver::extract_from_package(const QString& package_path, const QString& dest_dir) {
    if (!QFileInfo::exists(package_path)) {
        return false;
    }
    QTemporaryDir unpacked;
    if (!unpacked.isValid()) {
        LSW_ERROR("ivshmem: could not create a temp dir to unpack {}", package_path.toStdString());
        return false;
    }

    if (!has_extractor()) {
        LSW_WARN("ivshmem: no extractor available (install 7-Zip or innoextract), or set "
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
    QString override_dir = QString::fromLocal8Bit(qgetenv("LSW_IVSHMEM_DRIVER_DIR")).trimmed();
    if (!override_dir.isEmpty()) {
        if (looks_like_driver_dir(override_dir)) {
            LSW_INFO("ivshmem driver: using LSW_IVSHMEM_DRIVER_DIR={}", override_dir.toStdString());
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
    const auto packages = dir.entryInfoList({QStringLiteral("looking-glass-host*.exe"),
                                             QStringLiteral("looking-glass-host*.zip"),
                                             QStringLiteral("*ivshmem*.zip")},
                                            QDir::Files, QDir::Time);
    for (const auto& package : packages) {
        if (extract_from_package(package.absoluteFilePath(), cached)) {
            LSW_INFO("ivshmem driver: unpacked from {}", package.fileName().toStdString());
            return cached;
        }
    }

    // No warning here. This is a query, and its commonest caller asks
    // it before downloading anything — so warning on an empty answer
    // announced a broken guest five seconds before the fetcher fixed
    // it. Whether an empty answer is a problem is the caller's to know:
    // the fetcher goes and gets one, and the payload stager, which
    // cannot, says so.
    return {};
}

}  // namespace lsw::host::provision::install
