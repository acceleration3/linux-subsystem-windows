#pragma once

#include "lsw/host/provision/iso/catalog.hpp"
#include "lsw/host/provision/iso/windows_edition.hpp"
#include "lsw/host/provision/iso/microsoft_client.hpp"
#include "lsw/host/provision/iso/source_fetcher.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QWizardPage>

namespace lsw::host::wizard {

class Wizard;

// Step 3: pick how to get the Windows ISO. Two mutually-exclusive modes
// (enforced by a shared QButtonGroup — ``QRadioButton``s nested in
// separate ``QGroupBox``es do NOT auto-exclude each other, only their
// siblings within the same parent):
//   * Download: edition + language (SKU) chosen here. ``MicrosoftClient``
//     lives on this page so we can ``fetch_skus`` eagerly when the
//     edition changes and ``fetch_download_url`` synchronously when the
//     user clicks Next. On a 715-123130 (Type=9) ban during URL resolve
//     we red-outline the box, disable the radio, and stay on the page —
//     we never advance to a doomed download.
//   * Browse: user provides a local file.
//
// The download list is populated dynamically by ``IsoSourceFetcher``;
// no static catalog ships with the binary.
class IsoChoicePage : public QWizardPage {
    Q_OBJECT
public:
    explicit IsoChoicePage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] bool isComplete() const override;
    [[nodiscard]] int nextId() const override;
    bool validatePage() override;

private Q_SLOTS:
    void on_browse_clicked();
    void on_mode_changed();
    void on_edition_changed();
    void on_editions_ready(const QList<provision::iso::WindowsEdition>& editions);
    void on_fetch_failed(const QString& message);

    void on_ms_initialized();
    void on_ms_initialization_failed(const QString& message);
    void on_skus_ready(const QList<provision::iso::MsSku>& skus);
    void on_download_url_ready(const QString& url);
    void on_ms_failed(const QString& message);
    void on_ms_banned();

private:
    // Apply the red-outlined "download disabled" treatment with the
    // given user-facing reason. Idempotent. Used by both the live
    // poll-failure path and the persistent ban flag in WizardState.
    void apply_blocked_state(const QString& reason);

    // Kick off ms_.fetch_skus for the currently selected edition iff
    // the MS session is initialized and we don't already have SKUs for
    // it. Cancels staleness via ``current_skus_edition_id_``.
    void maybe_fetch_skus();

    Wizard& wizard_;
    provision::iso::IsoSourceFetcher fetcher_;
    provision::iso::MicrosoftClient ms_;
    QList<provision::iso::WindowsEdition> editions_;
    QList<provision::iso::MsSku> skus_;

    QButtonGroup* mode_group_ = nullptr;
    QGroupBox* dl_box_ = nullptr;
    QRadioButton* download_radio_ = nullptr;
    QRadioButton* browse_radio_ = nullptr;
    QComboBox* edition_combo_ = nullptr;
    QComboBox* language_combo_ = nullptr;
    QLabel* fetch_status_ = nullptr;
    QLineEdit* path_edit_ = nullptr;

    // ---- Async pipeline state ----
    bool ms_init_in_flight_ = false;
    bool ms_initialized_ = false;
    bool fetching_skus_ = false;
    int current_skus_edition_id_ = 0;  // ignore stale fetch_skus replies
    bool resolving_url_ = false;
    bool url_resolved_ = false;  // makes the post-resolve validatePage() return true
    QString resolved_url_;
    bool blocked_ = false;  // microsoft.com poll failed OR Microsoft returned a Type=9 ban
};

}  // namespace lsw::host::wizard
