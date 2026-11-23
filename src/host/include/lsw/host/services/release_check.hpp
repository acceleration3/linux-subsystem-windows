#pragma once

#include "lsw/host/services/release_status.hpp"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

namespace lsw::host::services {
class StatusHub;

class ReleaseCheck : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(const QByteArray& page, const QString& error)>;
    using Fetch = std::function<void(const QUrl& url, Done done)>;

    ReleaseCheck(StatusHub& status, QString running, Fetch fetch = {}, QObject* parent = nullptr);

    void check();

    [[nodiscard]] static QUrl releases_url();

    [[nodiscard]] static std::optional<QVector<ReleaseStatus::Release>> newer_than(
        const QByteArray& page, const QString& running, QString* error);

    [[nodiscard]] static QStringList headlines(const QString& notes);

private:
    void answer(const QByteArray& page, const QString& error);

    StatusHub& status_;
    QString running_;
    Fetch fetch_;
    QNetworkAccessManager nam_;
};

}
