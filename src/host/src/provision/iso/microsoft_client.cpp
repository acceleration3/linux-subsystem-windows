#include "lsw/host/provision/iso/microsoft_client.hpp"

#include "lsw/base/log.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <nlohmann/json.hpp>
#include <vector>
#include <zlib.h>

namespace lsw::host::provision::iso {

namespace {

using nlohmann::json;

// Verbatim from microsoft_interface.cpp — these IDs and the User-Agent
// must match what a real browser session sees, otherwise vlscppe
// silently rejects the whitelist call.
constexpr auto kOrgId = "y6jn8c31";
constexpr auto kProfile = "606624d44113";
constexpr auto kUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64; rv:143.0) Gecko/20100101 Firefox/143.0";
constexpr auto kReferer = "https://www.microsoft.com/software-download/windows11";

// Replace UTF-8 NBSP, collapse runs of whitespace, trim. Mirrors the
// ``normalize_whitespace`` helper from the reference impl — Microsoft
// peppers product display names with NBSP that breaks UI alignment.
QString normalize_whitespace(const QString& input) {
    QString tmp = input;
    tmp.replace(QChar(0x00A0), QChar(' '));
    static const QRegularExpression kRunOfSpace("\\s+");
    return tmp.replace(kRunOfSpace, " ").trimmed();
}

// Get a JSON value as ``std::string``, accepting either string or
// number — Microsoft's responses sometimes ship integers as JSON
// numbers and sometimes as numeric strings, depending on the field.
std::string json_to_string(const json& v) {
    if (v.is_string()) {
        return v.get<std::string>();
    }
    if (v.is_number_integer()) {
        return std::to_string(v.get<long long>());
    }
    if (v.is_number()) {
        return std::to_string(v.get<long long>());
    }
    return {};
}

bool looks_gzipped(const QByteArray& body) {
    return body.size() >= 2 && static_cast<unsigned char>(body[0]) == 0x1F &&
           static_cast<unsigned char>(body[1]) == 0x8B;
}

// Defensive zlib gunzip. Only invoked when the body still looks
// gzipped after Qt's own decompression should have run — usually
// because some intermediary handed us back compressed bytes.
QByteArray gunzip(const QByteArray& body) {
    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(body.constData()));
    zs.avail_in = static_cast<uInt>(body.size());
    // 16 + MAX_WBITS = decode gzip-wrapped stream.
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        LSW_WARN("microsoft: zlib inflateInit2 failed");
        return {};
    }
    QByteArray out;
    std::vector<char> buf(64 * 1024);
    int rc = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf.data());
        zs.avail_out = static_cast<uInt>(buf.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc == Z_NEED_DICT || rc == Z_DATA_ERROR || rc == Z_MEM_ERROR) {
            LSW_WARN("microsoft: zlib inflate failed rc={}", rc);
            inflateEnd(&zs);
            return {};
        }
        out.append(buf.data(), static_cast<int>(buf.size() - zs.avail_out));
    } while (rc != Z_STREAM_END && zs.avail_in > 0);
    inflateEnd(&zs);
    return out;
}

}  // namespace

MicrosoftClient::MicrosoftClient(QObject* parent) : QObject(parent) {
    nam_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // QNAM's default cookie jar persists Set-Cookie headers across
    // requests — exactly what we need so the visit-page → whitelist
    // sequence works.
    LSW_DEBUG("microsoft: client constructed");
}

void MicrosoftClient::set_record_directory(const QString& dir) {
    record_dir_ = dir;
    record_counter_ = 0;
    if (!record_dir_.isEmpty()) {
        QDir().mkpath(record_dir_);
        LSW_INFO("microsoft: recording responses to {}", record_dir_.toStdString());
    }
}

void MicrosoftClient::set_host_overrides(const QString& microsoft_base,
                                         const QString& vlscppe_base) {
    microsoft_base_ = microsoft_base.isEmpty() ? "https://www.microsoft.com" : microsoft_base;
    vlscppe_base_ = vlscppe_base.isEmpty() ? "https://vlscppe.microsoft.com" : vlscppe_base;
    LSW_INFO("microsoft: host overrides ms={} vlscppe={}", microsoft_base_.toStdString(),
             vlscppe_base_.toStdString());
}

QString MicrosoftClient::rewrite_host(const QString& url) const {
    QString out = url;
    if (microsoft_base_ != "https://www.microsoft.com") {
        out.replace("https://www.microsoft.com", microsoft_base_);
    }
    if (vlscppe_base_ != "https://vlscppe.microsoft.com") {
        out.replace("https://vlscppe.microsoft.com", vlscppe_base_);
    }
    return out;
}

QNetworkRequest MicrosoftClient::make_request(const QString& url, bool with_referer) const {
    const QString final_url = rewrite_host(url);
    LSW_DEBUG("microsoft: GET {}", final_url.toStdString());
    QNetworkRequest req{QUrl{final_url}};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.5");
    // NOTE: do NOT set Accept-Encoding here. Qt's QNetworkAccessManager
    // only auto-decompresses gzip/deflate when it owns that header — if
    // we set it ourselves, autoDecompress flips off and we get raw
    // gzip bytes back. That manifests as "response not valid JSON: ...
    // first byte 0x1F" downstream. Letting Qt set its own header means
    // it advertises only what it can decompress.
    req.setRawHeader("Connection", "keep-alive");
    if (with_referer) {
        req.setRawHeader("Referer", kReferer);
    }
    return req;
}

QString MicrosoftClient::generate_session_id() {
    // Random UUID v4. QUuid::createUuid uses a CSPRNG underneath and
    // formats with lowercase hex when serialised WithoutBraces — which
    // is what Microsoft expects.
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QByteArray MicrosoftClient::finish_reply(QNetworkReply* reply, const char* step,
                                         int* http_status_out) {
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (http_status_out != nullptr) {
        *http_status_out = status;
    }
    QByteArray body = reply->readAll();
    const auto net_err = reply->error();
    const auto err_str = reply->errorString();
    reply->deleteLater();

    if (looks_gzipped(body)) {
        LSW_WARN("microsoft: {} body still gzipped after QNAM "
                 "(size={}); decompressing manually",
                 step, body.size());
        QByteArray inflated = gunzip(body);
        if (!inflated.isEmpty()) {
            body = std::move(inflated);
        }
    }

    {
        QString line = QStringLiteral("microsoft: %1 <- HTTP %2 (%3 bytes)")
                           .arg(QString::fromUtf8(step))
                           .arg(status)
                           .arg(body.size());
        if (net_err != QNetworkReply::NoError) {
            line += QStringLiteral(" net_err=") + err_str;
        }
        LSW_DEBUG("{}", line.toStdString());
    }

    if (!record_dir_.isEmpty()) {
        const QString name = QStringLiteral("%1_%2.bin")
                                 .arg(record_counter_++, 2, 10, QChar('0'))
                                 .arg(QString::fromUtf8(step));
        const QString path = QDir(record_dir_).filePath(name);
        QFile f{path};
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(body);
            LSW_DEBUG("microsoft: recorded {} -> {}", step, path.toStdString());
        } else {
            LSW_WARN("microsoft: could not record {} to {}: {}", step, path.toStdString(),
                     f.errorString().toStdString());
        }
    }
    return body;
}

void MicrosoftClient::initialize(const QString& locale) {
    locale_ = locale;
    initialized_ = false;
    banned_ = false;
    LSW_INFO("microsoft: initialize locale={}", locale_.toStdString());

    // Stage 1 — check_locale. We chain into visit_download_page from
    // ``on_locale_check_finished``; on failure we emit
    // ``initialization_failed``. Falling back to en-US is handled in
    // the slot itself.
    const QString url = "https://www.microsoft.com/" + locale_ + "/software-download/";
    auto* reply = nam_.get(make_request(url));
    connect(reply, &QNetworkReply::finished, this, &MicrosoftClient::on_locale_check_finished);
}

void MicrosoftClient::on_locale_check_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr) {
        return;
    }
    int status = 0;
    (void)finish_reply(reply, "locale", &status);
    if (status != 200) {
        if (locale_ != "en-US") {
            LSW_INFO("microsoft: locale {} not available — falling back to en-US",
                     locale_.toStdString());
            locale_ = "en-US";
        } else {
            Q_EMIT initialization_failed(
                QStringLiteral("locale check returned HTTP %1").arg(status));
            return;
        }
    }
    session_id_ = generate_session_id();
    LSW_INFO("microsoft: session_id={}", session_id_.toStdString());

    // Stage 3 — visit the Win11 download page so the AKAM cookies
    // land in our jar. Microsoft inspects cookies on the vlscppe
    // request that follows.
    auto* reply2 = nam_.get(make_request(kReferer));
    connect(reply2, &QNetworkReply::finished, this, &MicrosoftClient::on_visit_page_finished);
}

void MicrosoftClient::on_visit_page_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr) {
        return;
    }
    int status = 0;
    (void)finish_reply(reply, "visit", &status);

    // Stage 4 — vlscppe.microsoft.com/tags whitelist. Failure here is
    // usually a regional block; we surface it as ``initialization_failed``.
    const QString url = QStringLiteral("https://vlscppe.microsoft.com/tags?org_id=%1&session_id=%2")
                            .arg(kOrgId, session_id_);
    auto* reply2 = nam_.get(make_request(url, /*with_referer=*/true));
    connect(reply2, &QNetworkReply::finished, this, &MicrosoftClient::on_whitelist_finished);
}

void MicrosoftClient::on_whitelist_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr) {
        return;
    }
    int status = 0;
    (void)finish_reply(reply, "whitelist", &status);
    if (status != 200) {
        Q_EMIT initialization_failed(
            QStringLiteral("vlscppe whitelist returned HTTP %1").arg(status));
        return;
    }
    initialized_ = true;
    LSW_INFO("microsoft: session whitelisted");
    Q_EMIT initialized();
}

void MicrosoftClient::fetch_skus(int product_edition_id) {
    if (!initialized_) {
        LSW_WARN("microsoft: fetch_skus called before initialize()");
        Q_EMIT failed("MicrosoftClient not initialized");
        return;
    }
    LSW_INFO("microsoft: fetch_skus product_edition_id={}", product_edition_id);
    // NB: Microsoft expects ``ProductEditionId`` (capital P) in the
    // current API revision. Old Fido docs spell it ``productEditionId``;
    // that variant 404s now.
    const QString url =
        QStringLiteral("https://www.microsoft.com/software-download-connector/api/"
                       "getskuinformationbyproductedition"
                       "?profile=%1&ProductEditionId=%2&SKU=undefined&friendlyFileName=undefined"
                       "&Locale=%3&sessionID=%4")
            .arg(kProfile)
            .arg(product_edition_id)
            .arg(locale_, session_id_);
    auto* reply = nam_.get(make_request(url));
    connect(reply, &QNetworkReply::finished, this, &MicrosoftClient::on_skus_finished);
}

void MicrosoftClient::on_skus_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr) {
        return;
    }
    int status = 0;
    const auto body = finish_reply(reply, "skus", &status);

    json doc;
    bool err = false;
    bool ban = false;
    parse_microsoft_envelope(body, &err, &ban, &doc);
    if (ban) {
        Q_EMIT banned_detected();
        return;
    }
    if (err) {
        Q_EMIT failed("getskuinformationbyproductedition returned an error envelope");
        return;
    }

    QList<MsSku> out;
    if (doc.contains("Skus") && doc["Skus"].is_array()) {
        for (const auto& sku : doc["Skus"]) {
            MsSku s;
            s.id = QString::fromStdString(json_to_string(sku.value("Id", json{})));
            s.language = QString::fromStdString(sku.value("Language", std::string{}));
            s.product_name = normalize_whitespace(
                QString::fromStdString(sku.value("LocalizedProductDisplayName", std::string{})));
            if (sku.contains("FriendlyFileNames") && sku["FriendlyFileNames"].is_array() &&
                !sku["FriendlyFileNames"].empty()) {
                s.file_name =
                    QString::fromStdString(sku["FriendlyFileNames"][0].get<std::string>());
            }
            out.append(s);
        }
    }
    {
        const QString line = QStringLiteral("microsoft: parsed %1 SKUs").arg(out.size());
        LSW_INFO("{}", line.toStdString());
    }
    if (out.isEmpty()) {
        Q_EMIT failed("Microsoft returned no SKUs");
    } else {
        Q_EMIT skus_ready(out);
    }
}

void MicrosoftClient::fetch_download_url(const MsSku& sku) {
    if (!initialized_) {
        LSW_WARN("microsoft: fetch_download_url called before initialize()");
        Q_EMIT failed("MicrosoftClient not initialized");
        return;
    }
    LSW_INFO("microsoft: fetch_download_url sku_id={} lang={}", sku.id.toStdString(),
             sku.language.toStdString());
    const QString url =
        QStringLiteral("https://www.microsoft.com/software-download-connector/api/"
                       "GetProductDownloadLinksBySku"
                       "?profile=%1&ProductEditionId=undefined&SKU=%2&friendlyFileName=undefined"
                       "&Locale=%3&sessionID=%4")
            .arg(kProfile, sku.id, locale_, session_id_);
    auto* reply = nam_.get(make_request(url, /*with_referer=*/true));
    connect(reply, &QNetworkReply::finished, this, &MicrosoftClient::on_download_url_finished);
}

void MicrosoftClient::on_download_url_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr) {
        return;
    }
    int status = 0;
    const auto body = finish_reply(reply, "download_url", &status);

    json doc;
    bool err = false;
    bool ban = false;
    parse_microsoft_envelope(body, &err, &ban, &doc);
    if (ban) {
        Q_EMIT banned_detected();
        return;
    }
    if (err) {
        Q_EMIT failed("GetProductDownloadLinksBySku returned an error envelope");
        return;
    }

    if (!doc.contains("ProductDownloadOptions") || !doc["ProductDownloadOptions"].is_array() ||
        doc["ProductDownloadOptions"].empty()) {
        Q_EMIT failed("Microsoft returned no ProductDownloadOptions");
        return;
    }
    const auto& first = doc["ProductDownloadOptions"][0];
    const std::string uri = first.value("Uri", std::string{});
    if (uri.empty()) {
        Q_EMIT failed("Microsoft returned an empty Uri");
        return;
    }
    {
        const QString line = QStringLiteral("microsoft: download URL resolved (%1 bytes)")
                                 .arg(static_cast<qulonglong>(uri.size()));
        LSW_INFO("{}", line.toStdString());
    }
    Q_EMIT download_url_ready(QString::fromStdString(uri));
}

void MicrosoftClient::parse_microsoft_envelope(const QByteArray& body, bool* error_out,
                                               bool* banned_out, void* json_out) {
    *error_out = false;
    *banned_out = false;
    auto* doc = static_cast<json*>(json_out);
    *doc = json{};
    try {
        *doc = json::parse(body.constData(), body.constData() + body.size());
    } catch (const json::exception& e) {
        // Log a clipped preview of the body — most parse failures are
        // either an HTML error page (block / WAF challenge) or bytes
        // we couldn't decompress; both are easy to spot in the first
        // ~256 chars.
        const auto preview = QString::fromUtf8(body.left(256)).toStdString();
        LSW_WARN("microsoft: response not valid JSON: {}; body[0..256]={}", e.what(), preview);
        *error_out = true;
        return;
    }
    if (!doc->is_object()) {
        LSW_WARN("microsoft: response is JSON but not an object");
        *error_out = true;
        return;
    }
    if (doc->contains("Errors") && (*doc)["Errors"].is_array() && !(*doc)["Errors"].empty()) {
        const auto& first = (*doc)["Errors"][0];
        const int type = first.value("Type", -1);
        LSW_WARN("microsoft: response carries Errors envelope (Type={})", type);
        if (type == 9) {
            banned_ = true;
            *banned_out = true;
            const std::string line = "Microsoft 715-123130: IP banned or region restricted "
                                     "(session_id=" +
                                     session_id_.toStdString() + ")";
            LSW_ERROR("{}", line);
        }
        *error_out = true;
    }
}

}  // namespace lsw::host::provision::iso
