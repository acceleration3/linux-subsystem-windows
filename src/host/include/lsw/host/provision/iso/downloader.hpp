#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

namespace lsw::host::provision::iso {

// Async HTTP(S) downloader with resume. Wraps QNetworkAccessManager so
// the wizard can hook progress / completion to widgets without having
// to deal with QNetworkReply directly.
class Downloader : public QObject {
    Q_OBJECT
public:
    explicit Downloader(QObject* parent = nullptr);

    // Start the download. ``dest_path`` is the file to write; if it
    // exists, an HTTP Range request resumes from its current size.
    // Emits progress/finished/error signals.
    void start(const QString& url, const QString& dest_path);

    // Best-effort cancellation. Closes the file but does not delete it.
    void cancel();

Q_SIGNALS:
    void progress(qint64 bytes_received, qint64 bytes_total);
    void finished(const QString& dest_path);
    void failed(const QString& error_message);

private Q_SLOTS:
    void on_ready_read();
    void on_finished();

private:
    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;
    QFile out_;
    qint64 resume_offset_ = 0;
    bool wrote_body_ = false;       // first body chunk seen → headers locked in
    bool requested_range_ = false;  // we sent Range; expect 206 or treat as restart
};

}  // namespace lsw::host::provision::iso
