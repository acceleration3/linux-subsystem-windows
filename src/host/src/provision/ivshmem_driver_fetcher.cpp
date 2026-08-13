#include "lsw/host/provision/ivshmem_driver_fetcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/install/ivshmem_driver.hpp"
#include "lsw/host/provision/iso/catalog.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace lsw::host::provision {

namespace {

constexpr int kPollIntervalMs = 500;
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
    if (const QString existing = install::IvshmemDriver::locate(cache_dir);
        !existing.isEmpty()) {
        Q_EMIT finished(existing);
        return;
    }
    download_package(cache_dir);
}

void IvshmemDriverFetcher::download_package(const QString& cache_dir) {
    QDir().mkpath(cache_dir);
    const QString dest = QDir(cache_dir).filePath(iso::IsoCatalog::looking_glass_host_filename());
    QFile::remove(dest);  // drop a truncated or stale attempt

    auto* curl = new QProcess(this);
    // -f so an HTTP error is a non-zero exit rather than a saved error
    // page; -L because the artifact URL is a redirector.
    curl->setProgram(QStringLiteral("curl"));
    curl->setArguments({QStringLiteral("-fSL"), QStringLiteral("--retry"), QStringLiteral("3"),
                        QStringLiteral("--retry-delay"), QStringLiteral("3"),
                        QStringLiteral("-o"), dest, iso::IsoCatalog::looking_glass_host_url()});

    auto* poll = new QTimer(this);
    poll->setInterval(kPollIntervalMs);
    connect(poll, &QTimer::timeout, this, [this, dest] {
        const qint64 got = QFileInfo(dest).size();
        Q_EMIT progress(-1.0, QStringLiteral("%1 KiB").arg(got / 1024));
    });
    poll->start();

    connect(curl, &QProcess::finished, this,
            [this, curl, poll, dest, cache_dir](int code, QProcess::ExitStatus) {
        poll->stop();
        poll->deleteLater();
        curl->deleteLater();
        if (code != 0) {
            Q_EMIT failed(
                QStringLiteral("download failed (curl exit %1)").arg(code));
            return;
        }
        if (QFileInfo(dest).size() < kMinPlausibleSize) {
            QFile::remove(dest);
            Q_EMIT failed(QStringLiteral("downloaded file is too small to be the host "
                                         "package — bad mirror or redirect"));
            return;
        }
        Q_EMIT progress(-1.0, QStringLiteral("unpacking..."));
        // Same call the cache path uses, so a downloaded package and a
        // hand-placed one take identical routes from here on.
        const QString dir = install::IvshmemDriver::locate(cache_dir);
        if (dir.isEmpty()) {
            Q_EMIT failed(QStringLiteral("downloaded the host package but could not unpack "
                                         "a driver from it — install p7zip or innoextract, "
                                         "or set LSW_IVSHMEM_DRIVER_DIR"));
            return;
        }
        LSW_INFO("provision: ivshmem driver ready at {}", dir.toStdString());
        Q_EMIT finished(dir);
    });
    connect(curl, &QProcess::errorOccurred, this, [this, curl, poll](QProcess::ProcessError) {
        poll->stop();
        poll->deleteLater();
        const QString error = curl->errorString();
        curl->deleteLater();
        Q_EMIT failed(
            QStringLiteral("could not run curl for the ivshmem host package: %1").arg(error));
    });
    curl->start();
}

}  // namespace lsw::host::provision
