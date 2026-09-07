#include "lsw/host/wizard/debloat_page.hpp"

#include "lsw/host/config/settings.hpp"
#include "lsw/host/provision/debloat_stager.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QVBoxLayout>

namespace lsw::host::wizard {

using provision::debloat::DebloatLevel;
using provision::debloat::DebloatPlan;

namespace {

// The button group's ids are the enum values, so the selection reads
// back without a lookup table.
[[nodiscard]] int id_of(DebloatLevel level) { return static_cast<int>(level); }

}  // namespace

DebloatPage::DebloatPage(Wizard& wizard, QWidget* parent)
    : QWizardPage(parent), wizard_(wizard) {
    setTitle("Trim Windows");
    setSubTitle("Removed from the image on this machine, before the guest ever boots.");

    auto* lay = new QVBoxLayout(this);

    auto* blurb = new QLabel(
        "Windows ships with applications and services a guest running one program does "
        "not need. LSW can take them out of the installation image here, which is "
        "cheaper and more complete than removing them afterwards — nothing is installed, "
        "so nothing has to be uninstalled and the bytes never reach the VM's disk.");
    blurb->setWordWrap(true);
    lay->addWidget(blurb);
    lay->addSpacing(8);

    none_ = new QRadioButton("Keep everything");
    sane_ = new QRadioButton("Remove the bundled apps and the telemetry (recommended)");
    maximum_ = new QRadioButton("Strip it to the minimum");

    group_ = new QButtonGroup(this);
    group_->addButton(none_, id_of(DebloatLevel::None));
    group_->addButton(sane_, id_of(DebloatLevel::Sane));
    group_->addButton(maximum_, id_of(DebloatLevel::Maximum));
    for (auto* button : {none_, sane_, maximum_}) {
        lay->addWidget(button);
    }
    sane_->setChecked(true);

    lay->addSpacing(8);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    lay->addWidget(summary_);

    warning_ = new QLabel;
    warning_->setWordWrap(true);
    // Distinguished by colour rather than by an icon: it is a
    // consequence of a choice already made, not an error to correct.
    warning_->setStyleSheet("color: #b5651d;");
    lay->addWidget(warning_);

    lay->addSpacing(8);
    start_ = new QPushButton("Prepare the image");
    start_->setDefault(true);
    lay->addWidget(start_);

    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    bar_->setVisible(false);
    lay->addWidget(bar_);

    status_ = new QLabel;
    status_->setWordWrap(true);
    lay->addWidget(status_);

    lay->addStretch(1);

    connect(group_, &QButtonGroup::idToggled, this, &DebloatPage::on_selection_changed);
    connect(start_, &QPushButton::clicked, this, &DebloatPage::start_trim);
    on_selection_changed();
}

void DebloatPage::initializePage() {
    // Reachable twice if the user goes Back from a later page. The
    // image was already rewritten by then, so offer the work again
    // rather than pretending it is still to do.
    done_ = false;
    bar_->setVisible(false);
    bar_->setValue(0);
    status_->clear();
    start_->setEnabled(true);
    Q_EMIT completeChanged();
}

void DebloatPage::start_trim() {
    const auto level = static_cast<DebloatLevel>(group_->checkedId());
    wizard_.state().debloat = level;

    // The choice is made; from here the page is doing, not asking.
    for (auto* button : {none_, sane_, maximum_}) {
        button->setEnabled(false);
    }
    start_->setEnabled(false);
    bar_->setVisible(true);
    status_->setText(QStringLiteral("Preparing..."));

    auto& state = wizard_.state();
    auto* stager = new provision::DebloatStager(this);
    connect(stager, &provision::DebloatStager::progress, this,
            [this](const QString& detail, int percent) {
        bar_->setValue(percent);
        status_->setText(detail);
    });
    connect(stager, &provision::DebloatStager::failed, this, [this](const QString& message) {
        // Recoverable: the source image is untouched, so the user can
        // pick a different level or try again.
        status_->setText(QStringLiteral("Could not prepare the image: %1").arg(message));
        bar_->setVisible(false);
        for (auto* button : {none_, sane_, maximum_}) {
            button->setEnabled(true);
        }
        start_->setEnabled(true);
    });
    connect(stager, &provision::DebloatStager::finished, this, [this](const QString& iso_path) {
        // Everything downstream boots what this names, and the pipeline
        // skips its own trim because the work is already done.
        wizard_.state().chosen_iso_path = iso_path;
        bar_->setValue(100);
        status_->setText(QStringLiteral("Ready: %1").arg(iso_path));
        done_ = true;
        Q_EMIT completeChanged();
    });
    stager->trim(state.chosen_iso_path, wizard_.services().settings().isos_dir(),
                 state.image_index, level);
}

bool DebloatPage::isComplete() const { return done_; }

void DebloatPage::on_selection_changed() {
    const auto level = static_cast<DebloatLevel>(group_->checkedId());
    const DebloatPlan plan{level};
    summary_->setText(plan.summary());
    const QString warning = plan.warning();
    warning_->setText(warning);
    warning_->setVisible(!warning.isEmpty());
}

bool DebloatPage::validatePage() {
    // Everything the pipeline needs is collected, and the image it will
    // install from is already on disk. This is the last page that
    // gathers anything; the rest report.
    wizard_.start_pipeline();
    return true;
}

int DebloatPage::nextId() const { return Wizard::Payload; }

}  // namespace lsw::host::wizard
