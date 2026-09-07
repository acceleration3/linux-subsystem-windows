#include "lsw/host/wizard/debloat_page.hpp"

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

    lay->addStretch(1);

    connect(group_, &QButtonGroup::idToggled, this, &DebloatPage::on_selection_changed);
    on_selection_changed();
}

void DebloatPage::on_selection_changed() {
    const auto level = static_cast<DebloatLevel>(group_->checkedId());
    const DebloatPlan plan{level};
    summary_->setText(plan.summary());
    const QString warning = plan.warning();
    warning_->setText(warning);
    warning_->setVisible(!warning.isEmpty());
}

bool DebloatPage::validatePage() {
    wizard_.state().debloat = static_cast<DebloatLevel>(group_->checkedId());
    return true;
}

int DebloatPage::nextId() const { return Wizard::VmSize; }

}  // namespace lsw::host::wizard
