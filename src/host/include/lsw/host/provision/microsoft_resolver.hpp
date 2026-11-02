#pragma once

#include "lsw/host/provision/iso/windows_edition.hpp"
#include "lsw/host/provision/iso/microsoft_client.hpp"
#include "lsw/host/provision/iso/source_fetcher.hpp"

#include <QList>
#include <QObject>
#include <QString>

namespace lsw::host::provision {
class MicrosoftResolver : public QObject {
    Q_OBJECT
public:
    explicit MicrosoftResolver(QObject* parent = nullptr);

    void resolve(const QString& edition_label, const QString& language_label);

Q_SIGNALS:
    void resolved(const QString& url, const QString& matched_edition_display);
    void failed(const QString& message, bool banned);

private Q_SLOTS:
    void on_editions_ready(const QList<iso::WindowsEdition>& editions);
    void on_fetcher_failed(const QString& msg);
    void on_ms_initialized();
    void on_ms_initialization_failed(const QString& msg);
    void on_skus_ready(const QList<iso::MsSku>& skus);
    void on_download_url_ready(const QString& url);
    void on_ms_failed(const QString& msg);
    void on_ms_banned();

private:
    void match_and_fetch_skus();
    void match_and_fetch_url();

    iso::IsoSourceFetcher fetcher_;
    iso::MicrosoftClient ms_;

    QString want_edition_;
    QString want_language_;
    QList<iso::WindowsEdition> editions_;
    int matched_edition_id_ = 0;
    QString matched_edition_display_;
    QList<iso::MsSku> skus_;
    bool ms_initialized_ = false;
    bool waiting_on_init_ = false;
    bool waiting_on_skus_ = false;
};

}
