#include "lsw/host/provision/ivshmem_driver_fetcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/curl_download.hpp"
#include "lsw/host/provision/install/ivshmem_driver.hpp"
#include "lsw/host/provision/iso/catalog.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace lsw::host::provision {

namespace {

// The host package is a few MiB. Anything under this is an error page,
// a redirect stub, or a truncated transfer — all of which unpack to
// nothing and would otherwise be reported as "no ivshmem files inside",
// which sends the reader looking in the wrong place.
constexpr qint64 kMinPlausibleSize = 256 * 1024;

}  // namespace

IvshmemDriverFetcher::IvshmemDriverFetcher(QObject* parent) : QObject(parent) {}

void IvshmemDriverFetcher::fetch(const QString& cache_dir) {
    // ``locate`` already covers the override, the unpacked cache and a
    // package a previous run left behind. Asking it first means a
    // second install does no network I/O at all.
    if (const QString existing = install::IvshmemDriver::locate(cache_dir); !existing.isEmpty()) {
        Q_EMIT finished(existing);
        return;
    }
    download_package(cache_dir);
}

void IvshmemDriverFetcher::download_package(const QString& cache_dir) {
    QDir().mkpath(cache_dir);
    const QString dest = QDir(cache_dir).filePath(iso::IsoCatalog::looking_glass_host_filename());

    auto* download = new CurlDownload(this);
    connect(download, &CurlDownload::progress, this, &IvshmemDriverFetcher::progress);
    connect(download, &CurlDownload::failed, this, [this, download](const QString& message) {
        download->deleteLater();
        Q_EMIT failed(message);
    });
    connect(download, &CurlDownload::finished, this,
            [this, download, cache_dir](const QString& path) {
                download->deleteLater();
                if (QFileInfo(path).size() < kMinPlausibleSize) {
                    QFile::remove(path);
                    Q_EMIT failed(QStringLiteral("downloaded file is too small to be the host "
                                                 "package — bad mirror or redirect"));
                    return;
                }
                Q_EMIT progress(-1.0, QStringLiteral("unpacking..."));
                // Same call the cache path uses, so a downloaded package
                // and a hand-placed one take identical routes from here on.
                const QString dir = install::IvshmemDriver::locate(cache_dir);
                if (dir.isEmpty()) {
                    Q_EMIT failed(
                        install::IvshmemDriver::has_extractor()
                            ? QStringLiteral("downloaded the host package but found no driver "
                                             "inside it — see the log, or set "
                                             "LSW_IVSHMEM_DRIVER_DIR")
                            : QStringLiteral("downloaded the host package but nothing here can "
                                             "unpack it. Install 7-Zip (Arch: 7zip, "
                                             "Debian/Ubuntu: p7zip-full, Fedora: p7zip-plugins) "
                                             "or innoextract and try again, or set "
                                             "LSW_IVSHMEM_DRIVER_DIR"));
                    return;
                }
                LSW_INFO("provision: ivshmem driver ready at {}", dir.toStdString());
                Q_EMIT finished(dir);
            });
    download->start(iso::IsoCatalog::looking_glass_host_url(), dest,
                    QStringLiteral("the ivshmem host package"));
}

}  // namespace lsw::host::provision
