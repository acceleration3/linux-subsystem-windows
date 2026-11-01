#include "lsw/host/provision/install/driver_catalog.hpp"

#include "lsw/base/log.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QVersionNumber>

#include <algorithm>

namespace lsw::host::provision::install {

namespace {

constexpr int kTimeoutMs = 8000;
// A transfer's timeout restarts with every byte, so a proxy that trickles them
// never trips it; setup and an update must not wait on it.
constexpr int kDeadlineMs = 15000;
// A file version holds four parts of sixteen bits.
constexpr int kMostVersionPart = 65535;

// The catalog lists a driver per row, with an id that ends in the row's
// number.
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

}  // namespace

DriverCatalog::DriverCatalog(Fetch fetch, QObject* parent)
    : QObject(parent), fetch_(std::move(fetch)) {
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
            continue;  // no driver's: the highest would otherwise beat every real one
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
    // Every search is registered before the first can answer, so a fetch
    // that answers at once does not end the lookup early.
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
        return;  // answered already
    }
    if (!error.isEmpty()) {
        LSW_WARN("provision: catalog lookup for {} failed ({}); the guest takes its built-in "
                 "driver version",
                 key.toStdString(), error.toStdString());
    } else if (const auto version = newest(page)) {
        versions_.insert(key, *version);
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

void DriverCatalog::complete() {
    if (completed_) {
        return;
    }
    completed_ = true;
    QString out;
    for (const QString& line : names_.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() >= 3) {
            const auto found = versions_.constFind(fields[0] + QLatin1Char(':') + fields[1]);
            if (found != versions_.constEnd()) {
                fields = fields.mid(0, 3);
                fields << *found;
            }
        }
        out += fields.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    Q_EMIT completed(out);
}

}  // namespace lsw::host::provision::install
