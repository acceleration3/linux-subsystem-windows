#include "lsw/host/provision/iso/windows_edition.hpp"
#include "lsw/host/wizard/iso_choice_page.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

IsoChoicePage::IsoChoicePage(Wizard& wizard, QWidget* parent)
    : QWizardPage(parent), wizard_(wizard) {
    setTitle("Windows ISO");
    setSubTitle("Pick an existing Windows ISO or fetch one from microsoft.com.");

    auto* lay = new QVBoxLayout(this);

    // Single button group across both group boxes — that's what makes
    // the two radios mutually exclusive (Qt's auto-exclusion only
    // applies to siblings of the same parent widget, and the radios
    // here live inside separate QGroupBoxes).
    mode_group_ = new QButtonGroup(this);
    mode_group_->setExclusive(true);

    // ---- Pick an existing ISO (default, on top) ----
    browse_radio_ = new QRadioButton("Pick an existing Windows ISO");
    mode_group_->addButton(browse_radio_);
    browse_radio_->setChecked(true);
    auto* br_box = new QGroupBox;
    auto* br_lay = new QVBoxLayout(br_box);
    br_lay->addWidget(browse_radio_);
    auto* path_row = new QHBoxLayout;
    path_edit_ = new QLineEdit;
    path_edit_->setPlaceholderText("Path to Windows ISO...");
    auto* browse_btn = new QPushButton("Browse...");
    path_row->addWidget(path_edit_, 1);
    path_row->addWidget(browse_btn);
    br_lay->addLayout(path_row);
    lay->addWidget(br_box);

    // ---- Download from Microsoft ----
    download_radio_ = new QRadioButton("Download from Microsoft");
    mode_group_->addButton(download_radio_);

    // Combos expand to fill the section's full width — long edition/SKU
    // labels (60+ chars) wouldn't otherwise fit.
    edition_combo_ = new QComboBox;
    edition_combo_->setEnabled(false);
    edition_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    edition_combo_->setPlaceholderText("Select an edition...");

    language_combo_ = new QComboBox;
    language_combo_->setEnabled(false);
    language_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    fetch_status_ = new QLabel("Select \"Download from Microsoft\" to query "
                               "the live edition list.");
    fetch_status_->setStyleSheet("color: gray;");
    fetch_status_->setWordWrap(true);

    dl_box_ = new QGroupBox;
    auto* dl_lay = new QVBoxLayout(dl_box_);
    dl_lay->addWidget(download_radio_);
    dl_lay->addWidget(fetch_status_);
    dl_lay->addStretch(1);  // pushes the combos to the bottom of the box
    auto* dl_form = new QFormLayout;
    // Make the field column eat all horizontal slack so the combos
    // stretch to the right edge of the group box.
    dl_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    dl_form->setFormAlignment(Qt::AlignBottom | Qt::AlignRight);
    dl_form->setLabelAlignment(Qt::AlignRight);
    dl_form->addRow("Edition", edition_combo_);
    dl_form->addRow("Language", language_combo_);
    dl_lay->addLayout(dl_form);
    lay->addWidget(dl_box_);
    lay->addStretch(1);

    connect(mode_group_, &QButtonGroup::buttonToggled, this, &IsoChoicePage::on_mode_changed);
    connect(browse_btn, &QPushButton::clicked, this, &IsoChoicePage::on_browse_clicked);
    connect(path_edit_, &QLineEdit::textChanged, this, &IsoChoicePage::completeChanged);
    connect(edition_combo_, &QComboBox::currentIndexChanged, this,
            &IsoChoicePage::on_edition_changed);
    connect(language_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
        // Picking a different language invalidates any previously
        // resolved URL — force a fresh fetch_download_url on Next.
        url_resolved_ = false;
        resolved_url_.clear();
        Q_EMIT completeChanged();
    });

    connect(&fetcher_, &provision::iso::IsoSourceFetcher::ready, this, &IsoChoicePage::on_editions_ready);
    connect(&fetcher_, &provision::iso::IsoSourceFetcher::failed, this, &IsoChoicePage::on_fetch_failed);

    connect(&ms_, &provision::iso::MicrosoftClient::initialized, this, &IsoChoicePage::on_ms_initialized);
    connect(&ms_, &provision::iso::MicrosoftClient::initialization_failed, this,
            &IsoChoicePage::on_ms_initialization_failed);
    connect(&ms_, &provision::iso::MicrosoftClient::skus_ready, this, &IsoChoicePage::on_skus_ready);
    connect(&ms_, &provision::iso::MicrosoftClient::download_url_ready, this,
            &IsoChoicePage::on_download_url_ready);
    connect(&ms_, &provision::iso::MicrosoftClient::failed, this, &IsoChoicePage::on_ms_failed);
    connect(&ms_, &provision::iso::MicrosoftClient::banned_detected, this, &IsoChoicePage::on_ms_banned);

    on_mode_changed();
}

void IsoChoicePage::initializePage() {
    // If we previously caught a Type=9 ban, that fact lives on the
    // wizard state — re-apply the blocked UI every time the user
    // navigates back so they can't loop into another doomed attempt.
    if (wizard_.state().microsoft_banned) {
        apply_blocked_state(QStringLiteral("Temporarily banned from the Microsoft downloads page "
                                           "from too many automated download tries. Use the local "
                                           "ISO option below for now."));
        return;
    }
    // Network requests are deferred until the user actually selects
    // the Download radio — see on_mode_changed.
}

void IsoChoicePage::on_mode_changed() {
    const bool dl = download_radio_->isChecked();
    edition_combo_->setEnabled(dl && !editions_.isEmpty() && !blocked_);
    language_combo_->setEnabled(dl && !skus_.isEmpty() && !blocked_);
    path_edit_->setEnabled(!dl);
    if (dl && !blocked_) {
        // First time the user opts into the download flow, kick off
        // the IsoSourceFetcher scrape and prime the Microsoft session.
        // Both are no-ops if already done / in flight.
        if (editions_.isEmpty()) {
            fetch_status_->setText("Fetching the current edition list from microsoft.com...");
            fetch_status_->setStyleSheet("color: gray;");
            fetcher_.fetch();
        }
        if (!ms_initialized_ && !ms_init_in_flight_) {
            ms_init_in_flight_ = true;
            ms_.initialize();
        }
    }
    Q_EMIT completeChanged();
}

void IsoChoicePage::on_browse_clicked() {
    const auto path =
        QFileDialog::getOpenFileName(this, "Select Windows ISO", QString{}, "ISO files (*.iso)");
    if (!path.isEmpty()) {
        path_edit_->setText(path);
    }
}

void IsoChoicePage::on_edition_changed() {
    // Edition swap invalidates the SKU list and any resolved URL.
    skus_.clear();
    language_combo_->clear();
    language_combo_->setEnabled(false);
    url_resolved_ = false;
    resolved_url_.clear();
    Q_EMIT completeChanged();
    maybe_fetch_skus();
}

void IsoChoicePage::maybe_fetch_skus() {
    if (blocked_ || !ms_initialized_ || fetching_skus_) {
        return;
    }
    if (!download_radio_->isChecked()) {
        return;
    }
    const int idx = edition_combo_->currentIndex();
    if (idx < 0 || idx >= editions_.size() || editions_[idx].product_edition_ids.isEmpty()) {
        return;
    }
    const int eid = editions_[idx].product_edition_ids.first();
    current_skus_edition_id_ = eid;
    fetching_skus_ = true;
    fetch_status_->setStyleSheet("color: gray;");
    fetch_status_->setText("Asking microsoft.com for available languages...");
    ms_.fetch_skus(eid);
}

void IsoChoicePage::on_editions_ready(const QList<provision::iso::WindowsEdition>& editions) {
    editions_ = editions;
    // Preserve a previously-applied ban; a successful editions scrape
    // doesn't undo the fact that Microsoft refused our download.
    if (wizard_.state().microsoft_banned) {
        return;
    }
    blocked_ = false;
    dl_box_->setStyleSheet({});  // clear any prior red outline
    download_radio_->setEnabled(true);
    edition_combo_->clear();
    for (const auto& e : editions_) {
        const QString label =
            e.version.isEmpty() ? e.display : QStringLiteral("%1 — %2").arg(e.version, e.display);
        edition_combo_->addItem(label);
    }
    // Leave the combo unselected so the placeholder "Select an
    // edition..." shows instead of auto-picking the first row.
    // QComboBox::clear() leaves currentIndex at -1, but populating
    // via addItem on an empty combo flips it to 0 — undo that.
    edition_combo_->setCurrentIndex(-1);
    fetch_status_->setText(
        QStringLiteral("Loaded %1 editions from microsoft.com.").arg(editions_.size()));
    fetch_status_->setStyleSheet("color: gray;");
    edition_combo_->setEnabled(download_radio_->isChecked());
    Q_EMIT completeChanged();
}

void IsoChoicePage::on_fetch_failed(const QString& message) {
    apply_blocked_state(QStringLiteral("microsoft.com refused our request (%1). They periodically "
                                       "block IPs / regions from the download portal — please "
                                       "supply a local Windows ISO instead.")
                            .arg(message));
}

void IsoChoicePage::apply_blocked_state(const QString& reason) {
    blocked_ = true;
    dl_box_->setStyleSheet(
        "QGroupBox { border: 1px solid #b00; border-radius: 4px; margin-top: 6px; }");
    download_radio_->setEnabled(false);
    edition_combo_->setEnabled(false);
    language_combo_->setEnabled(false);
    if (download_radio_->isChecked() && browse_radio_ != nullptr) {
        browse_radio_->setChecked(true);
    }
    fetch_status_->setText(reason);
    fetch_status_->setStyleSheet("color: #b00;");
    Q_EMIT completeChanged();
}

void IsoChoicePage::on_ms_initialized() {
    ms_init_in_flight_ = false;
    ms_initialized_ = true;
    LSW_INFO("iso_choice: microsoft session ready");
    maybe_fetch_skus();
}

void IsoChoicePage::on_ms_initialization_failed(const QString& message) {
    ms_init_in_flight_ = false;
    apply_blocked_state(
        QStringLiteral("Could not initialize microsoft.com session: %1").arg(message));
}

void IsoChoicePage::on_skus_ready(const QList<provision::iso::MsSku>& skus) {
    fetching_skus_ = false;
    skus_ = skus;
    language_combo_->clear();
    int default_idx = 0;
    int i = 0;
    for (const auto& s : skus_) {
        const QString label = s.product_name.isEmpty()
                                  ? s.language
                                  : QStringLiteral("%1 (%2)").arg(s.product_name, s.language);
        language_combo_->addItem(label);
        // Microsoft's Language field is a display name ("English",
        // "English (United States)"), not a BCP-47 code. Any string
        // containing "English" is good enough for a default.
        if (s.language.contains("English", Qt::CaseInsensitive) && default_idx == 0) {
            default_idx = i;
        }
        ++i;
    }
    language_combo_->setCurrentIndex(default_idx);
    language_combo_->setEnabled(true);
    fetch_status_->setText("Pick a language and press Next.");
    fetch_status_->setStyleSheet("color: gray;");
    Q_EMIT completeChanged();
}

void IsoChoicePage::on_download_url_ready(const QString& url) {
    LSW_INFO("iso_choice: download url resolved");
    resolving_url_ = false;
    url_resolved_ = true;
    resolved_url_ = url;
    wizard_.state().fido_download_url = url;
    fetch_status_->setText("URL ready — advancing.");
    fetch_status_->setStyleSheet("color: gray;");
    // Re-trigger the Next button now that we've resolved. The second
    // pass through validatePage() returns true thanks to url_resolved_.
    QTimer::singleShot(0, this, [this] {
        if (auto* w = wizard()) {
            w->next();
        }
    });
}

void IsoChoicePage::on_ms_failed(const QString& message) {
    resolving_url_ = false;
    fetching_skus_ = false;
    fetch_status_->setText(QStringLiteral("microsoft.com error: %1").arg(message));
    fetch_status_->setStyleSheet("color: #b00;");
    // Re-enable the controls so the user can try a different edition
    // / language.
    edition_combo_->setEnabled(true);
    language_combo_->setEnabled(!skus_.isEmpty());
    Q_EMIT completeChanged();
}

void IsoChoicePage::on_ms_banned() {
    resolving_url_ = false;
    wizard_.state().microsoft_banned = true;
    apply_blocked_state(QStringLiteral("Temporarily banned from the Microsoft downloads page "
                                       "from too many automated download tries. Use the local "
                                       "ISO option for now."));
}

bool IsoChoicePage::isComplete() const {
    if (download_radio_->isChecked()) {
        if (blocked_) {
            return false;
        }
        // Edition AND language picked — Next click will resolve the URL.
        const bool edition_ok = edition_combo_->currentIndex() >= 0 &&
                                edition_combo_->currentIndex() < editions_.size();
        const bool language_ok = !skus_.isEmpty() && language_combo_->currentIndex() >= 0 &&
                                 language_combo_->currentIndex() < skus_.size();
        return edition_ok && language_ok && !resolving_url_;
    }
    return !path_edit_->text().isEmpty();
}

bool IsoChoicePage::validatePage() {
    auto& s = wizard_.state();
    if (browse_radio_->isChecked()) {
        s.chosen_iso_path = path_edit_->text();
        s.fido_edition_ids.clear();
        s.fido_edition_label.clear();
        s.fido_download_url.clear();
        return true;
    }
    // Download mode. Persist edition/label so DownloadPage has context
    // for the status text.
    const int eidx = edition_combo_->currentIndex();
    if (eidx < 0 || eidx >= editions_.size()) {
        return false;
    }
    const int lidx = language_combo_->currentIndex();
    if (lidx < 0 || lidx >= skus_.size()) {
        return false;
    }
    s.chosen_iso_path.clear();
    s.fido_edition_ids = editions_[eidx].product_edition_ids;
    s.fido_edition_label = editions_[eidx].display;

    if (url_resolved_) {
        // Second pass — kicked by on_download_url_ready re-triggering
        // Next. Let the wizard advance.
        s.fido_download_url = resolved_url_;
        return true;
    }
    if (resolving_url_) {
        return false;  // user clicked Next while we're already working
    }
    // First pass — fire fetch_download_url and refuse advance until
    // the response arrives. The slots flip url_resolved_ / blocked_ /
    // an error and re-call wizard()->next() on success.
    resolving_url_ = true;
    fetch_status_->setText("Resolving download URL with Microsoft...");
    fetch_status_->setStyleSheet("color: gray;");
    edition_combo_->setEnabled(false);
    language_combo_->setEnabled(false);
    Q_EMIT completeChanged();
    ms_.fetch_download_url(skus_[lidx]);
    return false;
}

int IsoChoicePage::nextId() const {
    // VmSize is always next — whether we'll later need to download the
    // ISO (download radio chosen) or skip straight to Install (browse
    // radio with a local path) is decided by VmSizePage::nextId, which
    // checks ``state.chosen_iso_path``.
    return Wizard::VmSize;
}

}  // namespace lsw::host::wizard
