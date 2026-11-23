#include "lsw/host/services/release_check.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/services/status_hub.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QVersionNumber>
#include <algorithm>
#include <utility>

namespace lsw::host::services {

namespace {

constexpr int kTimeoutMs = 10000;

QVersionNumber number_of(const QString& version) {
    QString text = version.trimmed();
    if (text.startsWith(QLatin1Char('v')) || text.startsWith(QLatin1Char('V'))) {
        text.remove(0, 1);
    }
    qsizetype rest = 0;
    QVersionNumber number = QVersionNumber::fromString(text, &rest);
    return rest == text.size() ? number : QVersionNumber{};
}

const QRegularExpression& headline_pattern() {
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*[-*]\s+\*\*(.+?)\*\*)"),
                                            QRegularExpression::MultilineOption);
    return pattern;
}

}  // namespace

ReleaseCheck::ReleaseCheck(StatusHub& status, QString running, Fetch fetch, QObject* parent)
    : QObject(parent), status_(status), running_(std::move(running)), fetch_(std::move(fetch)) {
    nam_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!fetch_) {
        fetch_ = [this](const QUrl& url, Done done) {
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LSW"));
            request.setRawHeader("Accept", "application/vnd.github+json");
            request.setTransferTimeout(kTimeoutMs);
            QNetworkReply* reply = nam_.get(request);
            connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
                const QByteArray page = reply->readAll();
                const bool ok = reply->error() == QNetworkReply::NoError;
                done(page, ok ? QString{} : reply->errorString());
                reply->deleteLater();
            });
        };
    }
}

QUrl ReleaseCheck::releases_url() {
    return QUrl(QStringLiteral(
        "https://api.github.com/repos/acceleration3/linux-subsystem-windows/releases?per_page=50"));
}

void ReleaseCheck::check() {
    if (status_.release().state == ReleaseStatus::State::Checking) {
        return;
    }
    ReleaseStatus checking = status_.release();
    checking.state = ReleaseStatus::State::Checking;
    checking.error.clear();
    status_.set_release(checking);
    LSW_INFO("release check: asking GitHub for LSW's releases (running {})",
             running_.toStdString());
    fetch_(releases_url(),
           [this](const QByteArray& page, const QString& error) { answer(page, error); });
}

void ReleaseCheck::answer(const QByteArray& page, const QString& error) {
    ReleaseStatus result;
    result.checked = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString problem = error;
    std::optional<QVector<ReleaseStatus::Release>> newer;
    if (problem.isEmpty()) {
        newer = newer_than(page, running_, &problem);
    } else {
        const QJsonObject body = QJsonDocument::fromJson(page).object();
        const QString message = body.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            problem = QStringLiteral("%1 (%2)").arg(problem, message);
        }
    }
    if (!newer) {
        result.state = ReleaseStatus::State::Failed;
        result.error = problem;
        LSW_WARN("release check failed: {}", problem.toStdString());
    } else {
        result.newer = *newer;
        result.state =
            newer->isEmpty() ? ReleaseStatus::State::Latest : ReleaseStatus::State::Newer;
        LSW_INFO("release check: {}",
                 newer->isEmpty() ? std::string{"this is the latest release"}
                                  : "LSW " + newer->first().version.toStdString() + " is out");
    }
    status_.set_release(result);
}

std::optional<QVector<ReleaseStatus::Release>>
ReleaseCheck::newer_than(const QByteArray& page, const QString& running, QString* error) {
    const auto fail =
        [error](const QString& text) -> std::optional<QVector<ReleaseStatus::Release>> {
        if (error != nullptr) {
            *error = text;
        }
        return std::nullopt;
    };
    const QVersionNumber mine = number_of(running);
    if (mine.isNull()) {
        return fail(
            QStringLiteral("this build of LSW has no version to compare (%1)").arg(running));
    }
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(page, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isArray()) {
        return fail(QStringLiteral("GitHub's answer is not a list of releases"));
    }
    QVector<std::pair<QVersionNumber, ReleaseStatus::Release>> found;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("draft")).toBool() ||
            entry.value(QStringLiteral("prerelease")).toBool()) {
            continue;
        }
        const QString tag = entry.value(QStringLiteral("tag_name")).toString();
        const QVersionNumber number = number_of(tag);
        if (number.isNull() || number <= mine) {
            continue;
        }
        ReleaseStatus::Release release;
        release.version = number.toString();
        release.published = entry.value(QStringLiteral("published_at")).toString();
        release.page = QUrl(entry.value(QStringLiteral("html_url")).toString());
        release.headlines = headlines(entry.value(QStringLiteral("body")).toString());
        found.append({number, release});
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    QVector<ReleaseStatus::Release> newer;
    for (const auto& [number, release] : found) {
        if (newer.isEmpty() || newer.last().version != release.version) {
            newer.append(release);
        }
    }
    return newer;
}

QStringList ReleaseCheck::headlines(const QString& notes) {
    QStringList lines;
    auto matches = headline_pattern().globalMatch(notes);
    while (matches.hasNext()) {
        QString line = matches.next().captured(1).remove(QLatin1Char('`')).trimmed();
        while (line.endsWith(QLatin1Char('.')) || line.endsWith(QLatin1Char(':'))) {
            line.chop(1);
            line = line.trimmed();
        }
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

}  // namespace lsw::host::services
