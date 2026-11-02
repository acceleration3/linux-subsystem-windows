#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

namespace lsw::host::provision::iso {
class Downloader : public QObject {
    Q_OBJECT
public:
    explicit Downloader(QObject* parent = nullptr);

    void start(const QString& url, const QString& dest_path);

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
    bool wrote_body_ = false;
    bool requested_range_ = false;
};

}
