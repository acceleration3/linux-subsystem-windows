#include "lsw/host/services/driver_catalog.hpp"

#include "lsw/base/log.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>
#include <QVersionNumber>

#include <algorithm>

namespace lsw::host::services {

namespace {

constexpr int kTimeoutMs = 8000;
constexpr int kDeadlineMs = 15000;
constexpr int kMostVersionPart = 65535;

const QRegularExpression& row_pattern() {
    static const QRegularExpression pattern(
        QStringLiteral(R"(<tr[^>]*\bid="[0-9a-fA-F-]+_R\d+"[^>]*>(.*?)</tr>)"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

const QRegularExpression& tag_pattern() {
    static const QRegularExpression pattern(QStringLiteral("<[^>]*>"));
    return pattern;
}

const QRegularExpression& version_pattern() {
    static const QRegularExpression pattern(QStringLiteral(R"(\b(\d{1,9}(?:\.\d{1,9}){3})\b)"));
    return pattern;
}

}

DriverCatalog::DriverCatalog(Fetch fetch, QString cache_path, QObject* parent)
    : QObject(parent), fetch_(std::move(fetch)), cache_path_(std::move(cache_path)) {
    load_cache();
    nam_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!fetch_) {
        fetch_ = [this](const QUrl& url, Done done) {
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LSW"));
            request.setTransferTimeout(kTimeoutMs);
            QNetworkReply* reply = nam_.get(request);
            connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
                const bool ok = reply->error() == QNetworkReply::NoError;
                done(ok ? reply->readAll() : QByteArray{}, ok ? QString{} : reply->errorString());
                reply->deleteLater();
            });
        };
    }
}

std::optional<QString> DriverCatalog::newest(const QByteArray& page) {
    std::optional<QString> best;
    QVersionNumber best_number;
    auto rows = row_pattern().globalMatch(QString::fromUtf8(page));
    while (rows.hasNext()) {
        const QString text = rows.next().captured(1).remove(tag_pattern());
        const QRegularExpressionMatch found = version_pattern().match(text);
        if (!found.hasMatch()) {
            continue;
        }
        const QVersionNumber number = QVersionNumber::fromString(found.captured(1));
        const QList<int> parts = number.segments();
        if (std::any_of(parts.begin(), parts.end(),
                        [](int part) { return part > kMostVersionPart; })) {
            continue;
        }
        if (!best || number > best_number) {
            best = found.captured(1);
            best_number = number;
        }
    }
    return best;
}

QUrl DriverCatalog::search_url(const QString& vendor, const QString& device) {
    return QUrl::fromEncoded(
        QStringLiteral("https://www.catalog.update.microsoft.com/Search.aspx"
                       "?q=PCI%5CVEN_%1%26DEV_%2")
            .arg(vendor.toUpper(), device.toUpper())
            .toLatin1());
}

void DriverCatalog::lookup(const QString& adapter_names) {
    if (started_) {
        return;
    }
    started_ = true;
    names_ = adapter_names;
    QStringList keys;
    for (const QString& line : adapter_names.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() >= 3 && !pending_.contains(fields[0] + QLatin1Char(':') + fields[1])) {
            keys << fields[0] + QLatin1Char(':') + fields[1];
            pending_.insert(keys.last(), true);
        }
    }
    if (keys.isEmpty()) {
        complete();
        return;
    }
    QTimer::singleShot(kDeadlineMs, this, [this] {
        if (!pending_.isEmpty()) {
            LSW_WARN("provision: the catalog lookup ran out of time with {} unanswered; those "
                     "GPUs take the built-in driver version",
                     pending_.size());
            pending_.clear();
            complete();
        }
    });
    QPointer<DriverCatalog> self(this);
    for (const QString& key : keys) {
        const QStringList ids = key.split(QLatin1Char(':'));
        fetch_(search_url(ids[0], ids[1]), [self, key](const QByteArray& page, const QString& error) {
            if (self) {
                self->answer(key, page, error);
            }
        });
        if (!self) {
            return;
        }
    }
}

void DriverCatalog::answer(const QString& key, const QByteArray& page, const QString& error) {
    if (!pending_.remove(key)) {
        return;
    }
    if (!error.isEmpty()) {
        LSW_WARN("provision: catalog lookup for {} failed ({}); the guest takes its built-in "
                 "driver version",
                 key.toStdString(), error.toStdString());
    } else if (const auto version = newest(page)) {
        versions_.insert(key, *version);
        if (cached_.value(key) != *version) {
            cached_.insert(key, *version);
            save_cache();
        }
        LSW_INFO("provision: catalog lists driver {} for {}", version->toStdString(),
                 key.toStdString());
    } else {
        LSW_WARN("provision: catalog lists no driver for {}; the guest takes its built-in "
                 "driver version",
                 key.toStdString());
    }
    if (pending_.isEmpty()) {
        complete();
    }
}

void DriverCatalog::load_cache() {
    if (cache_path_.isEmpty()) {
        return;
    }
    QFile file{cache_path_};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().isString() && !it.value().toString().isEmpty()) {
            cached_.insert(it.key(), it.value().toString());
        }
    }
}

void DriverCatalog::save_cache() const {
    if (cache_path_.isEmpty()) {
        return;
    }
    QJsonObject object;
    for (auto it = cached_.begin(); it != cached_.end(); ++it) {
        object.insert(it.key(), it.value());
    }
    QSaveFile file{cache_path_};
    if (!file.open(QIODevice::WriteOnly)) {
        LSW_WARN("provision: cannot keep the catalog's driver versions in {}: {}",
                 cache_path_.toStdString(), file.errorString().toStdString());
        return;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        LSW_WARN("provision: cannot keep the catalog's driver versions in {}", cache_path_.toStdString());
    }
}

void DriverCatalog::complete() {
    if (completed_) {
        return;
    }
    completed_ = true;
    QString out;
    for (const QString& line : names_.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() >= 3) {
            const QString key = fields[0] + QLatin1Char(':') + fields[1];
            const QString version = versions_.value(key, cached_.value(key));
            if (!version.isEmpty()) {
                fields = fields.mid(0, 3);
                fields << version;
            }
        }
        out += fields.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    Q_EMIT completed(out);
}

}
