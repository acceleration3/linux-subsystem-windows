#include "lsw/host/provision/virtio_win_fetcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/curl_download.hpp"
#include "lsw/host/provision/iso/catalog.hpp"

#include <QDir>
#include <QFile>

namespace lsw::host::provision {

namespace {

// The real ISO is ~750 MiB; the anti-bot challenge page is ~4 KB.
constexpr qint64 kMinPlausibleSize = 100LL * 1024 * 1024;
// Offset of the ISO9660 primary volume descriptor's magic.
constexpr qint64 kPvdMagicOffset = 0x8001;

}  // namespace

VirtioWinFetcher::VirtioWinFetcher(QObject* parent) : QObject(parent) {}

bool VirtioWinFetcher::looks_like_iso(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (f.size() < kMinPlausibleSize) {
        return false;
    }
    if (!f.seek(kPvdMagicOffset)) {
        return false;
    }
    return f.read(5) == QByteArray("CD001");
}

void VirtioWinFetcher::fetch(const QString& preferred_path, const QString& cache_dir) {
    if (!preferred_path.isEmpty() && looks_like_iso(preferred_path)) {
        LSW_INFO("provision: using virtio-win.iso at {}", preferred_path.toStdString());
        Q_EMIT finished(preferred_path);
        return;
    }
    const QString dest = QDir(cache_dir).filePath("virtio-win.iso");
    if (looks_like_iso(dest)) {
        LSW_INFO("provision: reusing cached virtio-win.iso at {}", dest.toStdString());
        Q_EMIT finished(dest);
        return;
    }
    // Drop any stale challenge-page masquerade before re-fetching.
    QFile::remove(dest);
    download(dest);
}

void VirtioWinFetcher::download(const QString& dest) {
    auto* download = new CurlDownload(this);
    connect(download, &CurlDownload::progress, this, &VirtioWinFetcher::progress);
    connect(download, &CurlDownload::failed, this, [this, download](const QString& message) {
        download->deleteLater();
        Q_EMIT failed(message);
    });
    connect(download, &CurlDownload::finished, this, [this, download](const QString& path) {
        download->deleteLater();
        if (!looks_like_iso(path)) {
            Q_EMIT failed(QStringLiteral("not an ISO (anti-bot wall / bad mirror?) — "
                                         "refusing to bundle it"));
            return;
        }
        LSW_INFO("provision: virtio-win.iso downloaded + verified at {}", path.toStdString());
        Q_EMIT finished(path);
    });
    // Size first: 750 MiB is long enough to want a real percentage.
    download->start(iso::IsoCatalog::virtio_win_url(), dest, QStringLiteral("virtio-win.iso"),
                    /*probe_size=*/true);
}

}  // namespace lsw::host::provision
