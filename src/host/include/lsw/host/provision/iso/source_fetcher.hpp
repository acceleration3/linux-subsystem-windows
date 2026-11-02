#pragma once

#include "lsw/host/provision/iso/windows_edition.hpp"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace lsw::host::provision::iso {
class IsoSourceFetcher : public QObject {
    Q_OBJECT
public:
    explicit IsoSourceFetcher(QObject* parent = nullptr);

    void fetch();

Q_SIGNALS:
    void ready(const QList<WindowsEdition>& editions);
    void failed(const QString& message);

private Q_SLOTS:
    void on_page_finished();

private:
    void start_one(const QString& url, const QString& version_label);
    void maybe_emit();

    QNetworkAccessManager nam_;
    int pending_ = 0;
    QList<WindowsEdition> collected_;
    QStringList errors_;
};

}
