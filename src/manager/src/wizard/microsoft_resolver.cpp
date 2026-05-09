#include "lsw/manager/wizard/microsoft_resolver.hpp"

namespace lsw::manager::wizard {

MicrosoftResolver::MicrosoftResolver(QObject* parent) : QObject(parent) {
    connect(&fetcher_, &iso::IsoSourceFetcher::ready, this, &MicrosoftResolver::on_editions_ready);
    connect(&fetcher_, &iso::IsoSourceFetcher::failed, this, &MicrosoftResolver::on_fetcher_failed);

    connect(&ms_, &iso::MicrosoftClient::initialized, this, &MicrosoftResolver::on_ms_initialized);
    connect(&ms_, &iso::MicrosoftClient::initialization_failed, this,
            &MicrosoftResolver::on_ms_initialization_failed);
    connect(&ms_, &iso::MicrosoftClient::skus_ready, this, &MicrosoftResolver::on_skus_ready);
    connect(&ms_, &iso::MicrosoftClient::download_url_ready, this,
            &MicrosoftResolver::on_download_url_ready);
    connect(&ms_, &iso::MicrosoftClient::failed, this, &MicrosoftResolver::on_ms_failed);
    connect(&ms_, &iso::MicrosoftClient::banned_detected, this, &MicrosoftResolver::on_ms_banned);
}

void MicrosoftResolver::resolve(const QString& edition_label, const QString& language_label) {
    want_edition_ = edition_label;
    want_language_ = language_label;
    waiting_on_init_ = true;
    ms_.initialize();
    fetcher_.fetch();
}

void MicrosoftResolver::on_editions_ready(const QList<iso::WindowsEdition>& editions) {
    editions_ = editions;
    match_and_fetch_skus();
}

void MicrosoftResolver::on_fetcher_failed(const QString& msg) {
    Q_EMIT failed(QStringLiteral("edition list scrape failed: %1").arg(msg), /*banned=*/false);
}

void MicrosoftResolver::on_ms_initialized() {
    ms_initialized_ = true;
    waiting_on_init_ = false;
    if (!editions_.isEmpty()) {
        match_and_fetch_skus();
    }
}

void MicrosoftResolver::on_ms_initialization_failed(const QString& msg) {
    waiting_on_init_ = false;
    Q_EMIT failed(QStringLiteral("microsoft session init failed: %1").arg(msg), /*banned=*/false);
}

void MicrosoftResolver::match_and_fetch_skus() {
    if (!ms_initialized_ || editions_.isEmpty() || waiting_on_skus_) {
        return;
    }
    // Find the first edition whose ``display`` contains the requested
    // label as a case-insensitive substring. If the user passed an
    // empty edition label, fall back to the first edition that has
    // any product_edition_ids — this is rare in practice but keeps the
    // resolver from hanging when called with defaults.
    int matched_idx = -1;
    for (int i = 0; i < editions_.size(); ++i) {
        const auto& e = editions_[i];
        if (e.product_edition_ids.isEmpty()) continue;
        if (want_edition_.isEmpty()
            || e.display.contains(want_edition_, Qt::CaseInsensitive)
            || (!e.version.isEmpty()
                && QStringLiteral("%1 — %2").arg(e.version, e.display)
                       .contains(want_edition_, Qt::CaseInsensitive))) {
            matched_idx = i;
            break;
        }
    }
    if (matched_idx < 0) {
        Q_EMIT failed(QStringLiteral("no edition matched '%1' — try one of the labels "
                                       "shown in the wizard's Edition dropdown")
                          .arg(want_edition_),
                      /*banned=*/false);
        return;
    }
    matched_edition_id_ = editions_[matched_idx].product_edition_ids.first();
    matched_edition_display_ = editions_[matched_idx].display;
    waiting_on_skus_ = true;
    ms_.fetch_skus(matched_edition_id_);
}

void MicrosoftResolver::on_skus_ready(const QList<iso::MsSku>& skus) {
    waiting_on_skus_ = false;
    skus_ = skus;
    match_and_fetch_url();
}

void MicrosoftResolver::match_and_fetch_url() {
    if (skus_.isEmpty()) {
        Q_EMIT failed(QStringLiteral("microsoft.com returned no SKUs for edition '%1'")
                          .arg(matched_edition_display_),
                      /*banned=*/false);
        return;
    }
    int matched_sku = -1;
    if (want_language_.isEmpty()) {
        for (int i = 0; i < skus_.size(); ++i) {
            if (skus_[i].language.contains(QStringLiteral("English"), Qt::CaseInsensitive)) {
                matched_sku = i;
                break;
            }
        }
        if (matched_sku < 0) matched_sku = 0;
    } else {
        for (int i = 0; i < skus_.size(); ++i) {
            if (skus_[i].language.contains(want_language_, Qt::CaseInsensitive)) {
                matched_sku = i;
                break;
            }
        }
    }
    if (matched_sku < 0) {
        Q_EMIT failed(QStringLiteral("no language matched '%1' for edition '%2'")
                          .arg(want_language_, matched_edition_display_),
                      /*banned=*/false);
        return;
    }
    ms_.fetch_download_url(skus_[matched_sku]);
}

void MicrosoftResolver::on_download_url_ready(const QString& url) {
    Q_EMIT resolved(url, matched_edition_display_);
}

void MicrosoftResolver::on_ms_failed(const QString& msg) {
    Q_EMIT failed(msg, /*banned=*/false);
}

void MicrosoftResolver::on_ms_banned() {
    Q_EMIT failed(
        QStringLiteral("Temporarily banned from the Microsoft downloads page "
                       "(error 715-123130) — supply a local --iso instead."),
        /*banned=*/true);
}

}  // namespace lsw::manager::wizard
