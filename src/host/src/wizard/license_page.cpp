#include "lsw/host/wizard/license_page.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::wizard {

LicensePage::LicensePage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Microsoft Windows licensing");
    setSubTitle("LSW is not affiliated with Microsoft. You are responsible for licensing.");

    auto* lay = new QVBoxLayout(this);
    auto* blurb = new QLabel(
        "LSW does not bundle Windows. Installing or running Microsoft Windows requires "
        "you to obtain a license from Microsoft and to comply with the Microsoft Software "
        "License Terms. The full terms are presented during the Windows setup process.\n\n"
        "If you continue, you confirm you have a valid license to install and use Windows "
        "in this virtual machine.");
    blurb->setWordWrap(true);
    lay->addWidget(blurb);

    accept_box_ = new QCheckBox("I have a valid Windows license and accept the terms");
    lay->addWidget(accept_box_);
    connect(accept_box_, &QCheckBox::toggled, this, &LicensePage::completeChanged);
    lay->addStretch(1);
}

bool LicensePage::isComplete() const {
    return accept_box_ != nullptr && accept_box_->isChecked();
}

}  // namespace lsw::host::wizard
