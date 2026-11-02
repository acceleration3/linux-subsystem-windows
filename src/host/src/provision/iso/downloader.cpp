#include "lsw/host/provision/iso/downloader.hpp"

#include "lsw/base/log.hpp"

#include <QFileInfo>
#include <QNetworkRequest>

namespace lsw::host::provision::iso {

Downloader::Downloader(QObject* parent) : QObject(parent) {}

void Downloader::start(const QString& url, const QString& dest_path) {
    cancel();

    out_.setFileName(dest_path);
    QFileInfo fi{dest_path};
    resume_offset_ = fi.exists() ? fi.size() : 0;
    QIODevice::OpenMode mode = QIODevice::ReadWrite;
    if (!fi.exists()) {
        mode |= QIODevice::Truncate;
    }
    if (!out_.open(mode)) {
        Q_EMIT failed(out_.errorString());
        return;
    }
    out_.seek(resume_offset_);
    wrote_body_ = false;
    requested_range_ = false;

    QNetworkRequest req{QUrl{url}};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    if (resume_offset_ > 0) {
        req.setRawHeader("Range", QStringLiteral("bytes=%1-").arg(resume_offset_).toLatin1());
        requested_range_ = true;
        LSW_INFO("download resume: {} from offset {}", dest_path.toStdString(), resume_offset_);
    }
    reply_ = nam_.get(req);
    connect(reply_, &QNetworkReply::readyRead, this, &Downloader::on_ready_read);
    connect(reply_, &QNetworkReply::finished, this, &Downloader::on_finished);
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 base = wrote_body_ ? resume_offset_ : 0;
        Q_EMIT progress(base + received, total > 0 ? base + total : -1);
    });
}

void Downloader::cancel() {
    if (reply_ != nullptr) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    if (out_.isOpen()) {
        out_.close();
    }
}

void Downloader::on_ready_read() {
    if (reply_ == nullptr) {
        return;
    }
    if (!wrote_body_) {
        const auto status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (requested_range_ && status == 200) {
            LSW_WARN("download server ignored Range (status 200); "
                     "restarting from offset 0");
            out_.seek(0);
            out_.resize(0);
            resume_offset_ = 0;
        }
        wrote_body_ = true;
    }
    out_.write(reply_->readAll());
}

void Downloader::on_finished() {
    if (reply_ == nullptr) {
        return;
    }
    if (reply_->error() != QNetworkReply::NoError) {
        const QString err = reply_->errorString();
        cancel();
        Q_EMIT failed(err);
        return;
    }
    out_.write(reply_->readAll());
    out_.close();
    const QString dest = out_.fileName();
    reply_->deleteLater();
    reply_ = nullptr;
    Q_EMIT finished(dest);
}

}
