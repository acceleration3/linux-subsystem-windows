#pragma once

#include "lsw/host/provision/iso/ms_sku.hpp"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace lsw::host::provision::iso {

// Minimal Microsoft download-portal client. Mirrors the working flow:
//
//   1. check_locale       — GET https://www.microsoft.com/<locale>/software-download/
//                            (falls back to en-US on non-200)
//   2. generate_session_id — UUID v4 (random)
//   3. visit_download_page — GET https://www.microsoft.com/software-download/windows11
//                            (just for the AKAM/MS cookies a real browser would have)
//   4. whitelist_session  — GET https://vlscppe.microsoft.com/tags?org_id=...&session_id=...
//
// After ``initialize`` succeeds, two follow-up calls are available:
//
//   * ``fetch_skus``        — GET .../api/getskuinformationbyproductedition
//   * ``fetch_download_url``— GET .../api/GetProductDownloadLinksBySku
//
// We deliberately skip the ``ov-df.microsoft.com/mdt.js`` round-trip
// Fido does — Microsoft has rotated that endpoint and it's no longer
// required to get a working URL. Browser cookies + the vlscppe
// whitelist are enough.
//
// Cookie state persists automatically via QNetworkAccessManager's
// built-in cookie jar; that's why we keep one ``nam_`` for the whole
// session.
class MicrosoftClient : public QObject {
    Q_OBJECT
public:
    explicit MicrosoftClient(QObject* parent = nullptr);

    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

    [[nodiscard]] bool is_banned() const noexcept { return banned_; }

    [[nodiscard]] const QString& session_id() const noexcept { return session_id_; }

    [[nodiscard]] const QString& locale() const noexcept { return locale_; }

    // Async pipeline. Locale is "en-US" by default; on a non-200
    // locale-check the client transparently falls back to en-US.
    void initialize(const QString& locale = "en-US");

    // Stage 5 — list languages/SKUs for a product edition id.
    void fetch_skus(int product_edition_id);

    // Stage 6 — turn a SKU id into a real ESD download URL.
    void fetch_download_url(const MsSku& sku);

    // ------------------------------------------------------------------
    // Diagnostics / testing hooks
    // ------------------------------------------------------------------

    // When set to a non-empty directory, every HTTP response body is
    // written to ``<dir>/NN_<step>.bin`` (NN counter, ``step`` one of
    // ``locale`` / ``visit`` / ``whitelist`` / ``skus`` / ``download_url``).
    // The directory is created if it doesn't exist. Useful for capturing
    // a real session for later replay in the unit test.
    void set_record_directory(const QString& dir);

    // Override the hostnames used when constructing request URLs. Call
    // with two ``http://127.0.0.1:PORT`` values from a test's MockHttp
    // server to redirect every request without touching DNS. Empty
    // strings restore the defaults.
    void set_host_overrides(const QString& microsoft_base, const QString& vlscppe_base);

Q_SIGNALS:
    void initialized();
    void initialization_failed(const QString& message);
    void skus_ready(const QList<MsSku>& skus);
    void download_url_ready(const QString& url);
    void failed(const QString& message);
    void banned_detected();  // 715-123130; the client is unusable

private Q_SLOTS:
    void on_locale_check_finished();
    void on_visit_page_finished();
    void on_whitelist_finished();
    void on_skus_finished();
    void on_download_url_finished();

private:
    [[nodiscard]] QNetworkRequest make_request(const QString& url, bool with_referer = false) const;
    [[nodiscard]] static QString generate_session_id();
    // ``json_out`` is ``nlohmann::json*``, kept void* so the header
    // doesn't need to drag the json single-include in.
    void parse_microsoft_envelope(const QByteArray& body, bool* error_out, bool* banned_out,
                                  void* json_out);

    // Resolve a URL using the active host overrides. If both overrides
    // are empty this returns ``url`` unchanged.
    [[nodiscard]] QString rewrite_host(const QString& url) const;

    // Read body, log a single status/size line, and (if a record
    // directory is configured) dump the raw bytes to disk.
    [[nodiscard]] QByteArray finish_reply(QNetworkReply* reply, const char* step,
                                          int* http_status_out);

    QNetworkAccessManager nam_;
    QString locale_;
    QString session_id_;
    bool initialized_ = false;
    bool banned_ = false;

    // Host overrides + record dir.
    QString microsoft_base_ = "https://www.microsoft.com";
    QString vlscppe_base_ = "https://vlscppe.microsoft.com";
    QString record_dir_;
    int record_counter_ = 0;
};

}  // namespace lsw::host::provision::iso
