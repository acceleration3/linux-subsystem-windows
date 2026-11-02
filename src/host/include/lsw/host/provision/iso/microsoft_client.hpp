#pragma once

#include "lsw/host/provision/iso/ms_sku.hpp"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkReply;

namespace lsw::host::provision::iso {
class MicrosoftClient : public QObject {
    Q_OBJECT
public:
    explicit MicrosoftClient(QObject* parent = nullptr);

    [[nodiscard]] const QString& session_id() const noexcept { return session_id_; }

    [[nodiscard]] const QString& locale() const noexcept { return locale_; }

    void initialize(const QString& locale = "en-US");

    void fetch_skus(int product_edition_id);

    void fetch_download_url(const MsSku& sku);

    void set_record_directory(const QString& dir);

    void set_host_overrides(const QString& microsoft_base);

Q_SIGNALS:
    void initialized();
    void initialization_failed(const QString& message);
    void skus_ready(const QList<MsSku>& skus);
    void download_url_ready(const QString& url);
    void failed(const QString& message);
    void banned_detected();

private Q_SLOTS:
    void on_locale_check_finished();
    void on_visit_page_finished();
    void on_skus_finished();
    void on_download_url_finished();

private:
    [[nodiscard]] QNetworkRequest make_page_request(const QString& url) const;
    [[nodiscard]] QNetworkRequest make_api_request(const QString& url) const;

    void after_human_pause(std::function<void()> work);
    [[nodiscard]] static QString generate_session_id();
    void parse_microsoft_envelope(const QByteArray& body, bool* error_out, bool* banned_out,
                                  void* json_out);

    [[nodiscard]] QString rewrite_host(const QString& url) const;

    [[nodiscard]] QByteArray finish_reply(QNetworkReply* reply, const char* step,
                                          int* http_status_out);

    QNetworkAccessManager nam_;
    QString locale_;
    QString session_id_;
    bool initialized_ = false;

    QString microsoft_base_ = "https://www.microsoft.com";
    QString record_dir_;
    int record_counter_ = 0;
};

}
