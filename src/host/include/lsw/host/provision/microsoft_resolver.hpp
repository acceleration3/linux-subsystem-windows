#pragma once

#include "lsw/host/provision/iso/catalog.hpp"
#include "lsw/host/provision/iso/windows_edition.hpp"
#include "lsw/host/provision/iso/microsoft_client.hpp"
#include "lsw/host/provision/iso/source_fetcher.hpp"

#include <QList>
#include <QObject>
#include <QString>

namespace lsw::host::provision {

// Resolves a Microsoft "Download Windows" edition + language label to
// a concrete .iso download URL by driving ``IsoSourceFetcher`` and
// ``MicrosoftClient`` end-to-end:
//
//   1. ``fetch`` scrapes the live edition list from microsoft.com.
//   2. Matches the caller's edition label (case-insensitive substring
//      against ``WindowsEdition::display``) and picks the first
//      matching ``productEditionId``.
//   3. ``MicrosoftClient::initialize`` opens a session, then
//      ``fetch_skus`` for that edition, then matches the caller's
//      language label (case-insensitive substring against
//      ``MsSku::language``).
//   4. ``fetch_download_url`` resolves the chosen SKU to an ESD URL.
//
// Used by both the wizard's ``IsoChoicePage`` (the UI flow is the same
// dance, just driven by user clicks instead of static labels) and the
// unattended CLI runner (where ``--edition`` and ``--language`` are
// fed in as strings on the command line).
//
// Fires ``resolved(url)`` on success. Fires ``failed(reason, banned)``
// on any error; ``banned == true`` is the 715-123130 (Type=9) ban
// case, which is sticky and should propagate non-zero out of the CLI
// runner rather than retry-spin.
class MicrosoftResolver : public QObject {
    Q_OBJECT
public:
    explicit MicrosoftResolver(QObject* parent = nullptr);

    // Begin resolving. Both labels are matched as case-insensitive
    // substrings against the live Microsoft catalog. Empty
    // ``language_label`` means "first SKU labelled English".
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

}  // namespace lsw::host::provision
