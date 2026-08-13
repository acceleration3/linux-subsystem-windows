#include "lsw/host/provision/iso/windows_edition.hpp"
#include "lsw/host/provision/iso/source_fetcher.hpp"

#include "lsw/base/log.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace lsw::host::provision::iso {

namespace {

constexpr auto kUserAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                            "(KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36";

constexpr auto kWin11Url = "https://www.microsoft.com/en-us/software-download/windows11";
constexpr auto kWin10Url = "https://www.microsoft.com/en-us/software-download/windows10ISO";

// Match every ``<option value="<digits>">label</option>`` inside the
// page. ``value="null"`` and other non-numeric values are excluded by
// the ``\d+`` capture.
QList<WindowsEdition> parse_options(const QString& html, const QString& version) {
    QList<WindowsEdition> out;
    static const QRegularExpression kOption(
        QString::fromUtf8("<option\\s+value=\"(\\d+)\"[^>]*>([^<]+)</option>"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = kOption.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        bool ok = false;
        const int id = m.captured(1).toInt(&ok);
        if (!ok) {
            continue;
        }
        const QString label = m.captured(2).trimmed();
        WindowsEdition ed;
        ed.display = label;
        ed.version = version;
        ed.product_edition_ids = {id};
        out.append(std::move(ed));
    }
    return out;
}

}  // namespace

IsoSourceFetcher::IsoSourceFetcher(QObject* parent) : QObject(parent) {
    nam_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

void IsoSourceFetcher::fetch() {
    LSW_INFO("iso source: fetching edition list from microsoft.com");
    pending_ = 0;
    collected_.clear();
    errors_.clear();
    start_one(kWin11Url, "Windows 11");
    start_one(kWin10Url, "Windows 10");
}

void IsoSourceFetcher::start_one(const QString& url, const QString& version_label) {
    LSW_DEBUG("iso source: GET {} ({})", url.toStdString(), version_label.toStdString());
    QNetworkRequest req{QUrl{url}};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setRawHeader("Accept", "text/html,application/xhtml+xml");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    auto* reply = nam_.get(req);
    reply->setProperty("version_label", version_label);
    ++pending_;
    connect(reply, &QNetworkReply::finished, this, &IsoSourceFetcher::on_page_finished);
}

void IsoSourceFetcher::on_page_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr) {
        return;
    }
    const QString version = reply->property("version_label").toString();
    if (reply->error() != QNetworkReply::NoError) {
        const QString line =
            QStringLiteral("iso source fetch failed for %1: %2").arg(version, reply->errorString());
        errors_.append(line);
        LSW_WARN("{}", line.toStdString());
    } else {
        const QString html = QString::fromUtf8(reply->readAll());
        const auto found = parse_options(html, version);
        if (found.isEmpty()) {
            errors_.append(QStringLiteral("%1: no <option> entries in page").arg(version));
        } else {
            collected_.append(found);
            const QString line = QStringLiteral("microsoft.com listed %1 editions for %2")
                                     .arg(found.size())
                                     .arg(version);
            LSW_INFO("{}", line.toStdString());
        }
    }
    reply->deleteLater();
    --pending_;
    maybe_emit();
}

void IsoSourceFetcher::maybe_emit() {
    if (pending_ > 0) {
        return;
    }
    if (collected_.isEmpty()) {
        const QString msg = errors_.isEmpty() ? QStringLiteral("Microsoft returned no editions.")
                                              : errors_.join(" / ");
        LSW_WARN("iso source: fetch failed: {}", msg.toStdString());
        Q_EMIT failed(msg);
    } else {
        const QString line =
            QStringLiteral("iso source: emitting %1 total editions").arg(collected_.size());
        LSW_INFO("{}", line.toStdString());
        Q_EMIT ready(collected_);
    }
}

}  // namespace lsw::host::provision::iso
