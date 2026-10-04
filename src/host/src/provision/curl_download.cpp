#include "lsw/host/provision/curl_download.hpp"

#include "lsw/host/platform/byte_size.hpp"

#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace lsw::host::provision {

namespace {

constexpr int kPollIntervalMs = 500;

// -L follows redirects, so several header blocks come back; the last
// Content-Length is the one for the actual file.
[[nodiscard]] qint64 content_length(const QByteArray& headers) {
    qint64 total = 0;
    for (const auto& line : QString::fromLatin1(headers).split(QLatin1Char('\n'))) {
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("content-length:"), Qt::CaseInsensitive)) {
            total = trimmed.mid(trimmed.indexOf(QLatin1Char(':')) + 1).trimmed().toLongLong();
        }
    }
    return total;
}

// curl's own sentence for why it gave up — "Could not resolve host",
// "The requested URL returned error: 404" — which says more than its
// exit code does. -S makes it print one even with the meter silenced.
[[nodiscard]] QString curl_reason(const QByteArray& stderr_bytes) {
    const QString text = QString::fromLocal8Bit(stderr_bytes);
    const auto at = text.lastIndexOf(QLatin1String("curl: ("));
    if (at < 0) {
        return {};
    }
    const QString line = text.mid(at).section(QLatin1Char('\n'), 0, 0);
    // "curl: (22) The requested URL ..." -> "The requested URL ..."
    return line.section(QLatin1Char(')'), 1).trimmed();
}

}  // namespace

CurlDownload::CurlDownload(QObject* parent) : QObject(parent) {
    poll_.setInterval(kPollIntervalMs);
    connect(&poll_, &QTimer::timeout, this, &CurlDownload::report_progress);
}

void CurlDownload::start(const QString& url, const QString& dest, const QString& what,
                         bool probe_size) {
    url_ = url;
    dest_ = dest;
    what_ = what;
    total_bytes_ = 0;
    if (probe_size) {
        this->probe_size();
    } else {
        download();
    }
}

QProcess* CurlDownload::spawn(const QStringList& args) {
    auto* proc = new QProcess(this);
    proc->setProgram(QStringLiteral("curl"));
    proc->setArguments(args);
    // Only the start failure is handled here. A crash is reported by
    // ``finished`` as well, and answering both would emit twice.
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        poll_.stop();
        const QString reason = proc->errorString();
        proc->deleteLater();
        Q_EMIT failed(QStringLiteral("could not run curl for %1: %2").arg(what_, reason));
    });
    return proc;
}

void CurlDownload::probe_size() {
    auto* head = spawn({QStringLiteral("-sIL"), url_});
    // Whatever HEAD says, the download goes ahead: a mirror that will
    // not answer HEAD costs the percentage, not the file.
    connect(head, &QProcess::finished, this, [this, head] {
        total_bytes_ = content_length(head->readAllStandardOutput());
        head->deleteLater();
        download();
    });
    head->start();
}

void CurlDownload::download() {
    QFile::remove(dest_);  // a truncated attempt or a saved challenge page
    auto* curl = spawn({QStringLiteral("-fsSL"), QStringLiteral("--retry"), QStringLiteral("3"),
                        QStringLiteral("--retry-delay"), QStringLiteral("3"), QStringLiteral("-o"),
                        dest_, url_});
    connect(curl, &QProcess::finished, this, [this, curl](int code, QProcess::ExitStatus status) {
        poll_.stop();
        const QString reason = curl_reason(curl->readAllStandardError());
        curl->deleteLater();
        if (status != QProcess::NormalExit) {
            Q_EMIT failed(QStringLiteral("curl crashed downloading %1").arg(what_));
            return;
        }
        if (code != 0) {
            Q_EMIT failed(
                reason.isEmpty()
                    ? QStringLiteral("download failed (curl exit %1)").arg(code)
                    : QStringLiteral("download failed (curl exit %1): %2").arg(code).arg(reason));
            return;
        }
        Q_EMIT finished(dest_);
    });
    poll_.start();
    curl->start();
}

void CurlDownload::report_progress() {
    const qint64 got = QFileInfo(dest_).size();
    const QString have = platform::ByteSize::human(static_cast<quint64>(got));
    if (total_bytes_ <= 0) {
        Q_EMIT progress(-1.0, have);
        return;
    }
    const double fraction = static_cast<double>(got) / static_cast<double>(total_bytes_);
    Q_EMIT progress(fraction,
                    QStringLiteral("%1%  (%2 / %3)")
                        .arg(static_cast<int>(fraction * 100.0))
                        .arg(have, platform::ByteSize::human(static_cast<quint64>(total_bytes_))));
}

}  // namespace lsw::host::provision
