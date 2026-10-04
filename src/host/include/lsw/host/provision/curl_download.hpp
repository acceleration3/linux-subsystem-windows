#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class QProcess;

namespace lsw::host::provision {

// One file fetched by shelling out to ``curl``.
//
// Not QNetworkAccessManager, deliberately. fedorapeople sits behind an
// anti-bot wall that serves a ~4 KB HTML challenge page to anything
// that does not look like curl — QNetworkAccessManager included — and
// passes curl's default user agent. So no custom User-Agent here
// either: a browser-looking one is turned away too.
//
// The arguments are the same for every download: -f so an HTTP error
// is a non-zero exit rather than a saved error page, -L because release
// URLs are redirectors, and a few retries for a flaky mirror. curl
// writes straight to the file, so progress is observed by watching the
// file grow.
//
// What the bytes are is not this class's business. ``finished`` means
// curl said it succeeded; whether the file is the ISO or the package
// that was asked for is for the caller to check, and only it knows how.
class CurlDownload : public QObject {
    Q_OBJECT
public:
    explicit CurlDownload(QObject* parent = nullptr);

    // Fetch ``url`` into ``dest``, replacing anything already there.
    // ``what`` names the file in messages ("virtio-win.iso"). With
    // ``probe_size``, a HEAD request goes first so ``progress`` can
    // report a real fraction rather than a bare byte count — worth it
    // for a large file, not for a small one. Emits exactly one of
    // ``finished`` / ``failed``.
    void start(const QString& url, const QString& dest, const QString& what,
               bool probe_size = false);

Q_SIGNALS:
    // ``fraction`` is negative when the total size is unknown.
    void progress(double fraction, const QString& detail);
    void finished(const QString& dest);
    void failed(const QString& message);

private:
    // A curl process with the failure-to-start path already connected.
    [[nodiscard]] QProcess* spawn(const QStringList& args);
    void probe_size();
    void download();
    void report_progress();

    QString url_;
    QString dest_;
    QString what_;
    qint64 total_bytes_ = 0;
    QTimer poll_;
};

}  // namespace lsw::host::provision
