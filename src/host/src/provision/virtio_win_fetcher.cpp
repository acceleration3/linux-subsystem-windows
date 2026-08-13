#include "lsw/host/provision/virtio_win_fetcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/iso/catalog.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace lsw::host::provision {

namespace {

// The real ISO is ~750 MiB; the anti-bot challenge page is ~4 KB.
constexpr qint64 kMinPlausibleSize = 100LL * 1024 * 1024;
// Offset of the ISO9660 primary volume descriptor's magic.
constexpr qint64 kPvdMagicOffset = 0x8001;
constexpr int kPollIntervalMs = 1000;

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
    probe_size_then_download(iso::IsoCatalog::virtio_win_url(), dest);
}

void VirtioWinFetcher::probe_size_then_download(const QString& url, const QString& dest) {
    auto* head = new QProcess(this);
    head->setProgram(QStringLiteral("curl"));
    head->setArguments({QStringLiteral("-sIL"), url});
    connect(head, &QProcess::finished, this,
            [this, head, url, dest](int, QProcess::ExitStatus) {
        qint64 total = 0;
        const auto headers = QString::fromLatin1(head->readAllStandardOutput());
        head->deleteLater();
        // -L follows redirects, so several header blocks come back; the
        // last Content-Length is the one for the actual file.
        for (const auto& line : headers.split('\n')) {
            const auto trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("content-length:"), Qt::CaseInsensitive)) {
                total = trimmed.mid(trimmed.indexOf(':') + 1).trimmed().toLongLong();
            }
        }
        download(url, dest, total);
    });
    connect(head, &QProcess::errorOccurred, this, [this, head](QProcess::ProcessError) {
        const QString error = head->errorString();
        head->deleteLater();
        Q_EMIT failed(QStringLiteral("could not run curl (HEAD) for virtio-win.iso: %1")
                          .arg(error));
    });
    head->start();
}

void VirtioWinFetcher::download(const QString& url, const QString& dest,
                                qint64 total_bytes) {
    auto* curl = new QProcess(this);
    // -f: HTTP errors become a non-zero exit rather than a saved error
    // body. -L: follow the redirect chain. No custom User-Agent — curl's
    // default passes the anti-bot wall, a browser-looking one does not.
    curl->setProgram(QStringLiteral("curl"));
    curl->setArguments({QStringLiteral("-fSL"), QStringLiteral("--retry"),
                        QStringLiteral("3"), QStringLiteral("--retry-delay"),
                        QStringLiteral("3"), QStringLiteral("-o"), dest, url});

    // curl writes to a file rather than a pipe we can read, so progress
    // is observed by watching the file grow.
    auto* poll = new QTimer(this);
    poll->setInterval(kPollIntervalMs);
    connect(poll, &QTimer::timeout, this, [this, dest, total_bytes] {
        const qint64 got = QFileInfo(dest).size();
        if (total_bytes > 0) {
            const double fraction =
                static_cast<double>(got) / static_cast<double>(total_bytes);
            Q_EMIT progress(fraction, QStringLiteral("%1%  (%2 / %3 MiB)")
                                          .arg(static_cast<int>(fraction * 100.0))
                                          .arg(got / (1024 * 1024))
                                          .arg(total_bytes / (1024 * 1024)));
        } else {
            Q_EMIT progress(-1.0, QStringLiteral("%1 MiB").arg(got / (1024 * 1024)));
        }
    });
    poll->start();

    connect(curl, &QProcess::finished, this,
            [this, curl, poll, dest](int code, QProcess::ExitStatus) {
        poll->stop();
        poll->deleteLater();
        curl->deleteLater();
        if (code != 0) {
            Q_EMIT failed(QStringLiteral("download failed (curl exit %1)").arg(code));
            return;
        }
        if (!looks_like_iso(dest)) {
            Q_EMIT failed(QStringLiteral("not an ISO (anti-bot wall / bad mirror?) — "
                                         "refusing to bundle it"));
            return;
        }
        LSW_INFO("provision: virtio-win.iso downloaded + verified at {}",
                 dest.toStdString());
        Q_EMIT finished(dest);
    });
    connect(curl, &QProcess::errorOccurred, this,
            [this, curl, poll](QProcess::ProcessError) {
        poll->stop();
        poll->deleteLater();
        const QString error = curl->errorString();
        curl->deleteLater();
        Q_EMIT failed(QStringLiteral("could not run curl for virtio-win.iso: %1").arg(error));
    });
    curl->start();
}

}  // namespace lsw::host::provision
