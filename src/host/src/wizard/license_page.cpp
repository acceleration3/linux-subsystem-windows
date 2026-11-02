#include "lsw/host/wizard/license_page.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::wizard {

namespace {
constexpr int kProseWidth = 620;

}

LicensePage::LicensePage(QWidget* parent) : WizardPage(parent) {

    auto* lay = install_chrome(
        QStringLiteral("Microsoft Windows licensing"),
        QStringLiteral("LSW is not affiliated with Microsoft, and does not bundle Windows."));
    auto* blurb = new QLabel(
        "LSW does not bundle Windows. Installing or running Microsoft Windows requires "
        "you to obtain a license from Microsoft and to comply with the Microsoft Software "
        "License Terms. The full terms are presented during the Windows setup process.");
    blurb->setWordWrap(true);
    blurb->setMaximumWidth(kProseWidth);
    lay->addWidget(blurb);

    auto* callout = new QLabel(
        QStringLiteral("If you continue, you confirm you hold a valid license to install "
                       "and use Windows in this virtual machine."));
    callout->setObjectName(QString::fromLatin1(ui::Theme::Role::kCallout));
    callout->setWordWrap(true);
    callout->setMaximumWidth(kProseWidth);
    lay->addWidget(callout);

    accept_box_ = new QCheckBox("I have a valid Windows license and accept the terms");
    lay->addWidget(accept_box_);
    connect(accept_box_, &QCheckBox::toggled, this, &LicensePage::completeChanged);
    lay->addStretch(1);
}

bool LicensePage::isComplete() const {
    return accept_box_ != nullptr && accept_box_->isChecked();
}

}
