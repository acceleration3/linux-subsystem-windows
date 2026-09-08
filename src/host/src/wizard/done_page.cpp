#include "lsw/host/wizard/done_page.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

DonePage::DonePage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Installation finished");
    setSubTitle("The guest agent is online. The VM is ready.");

    auto* lay = new QVBoxLayout(this);
    auto* body =
        new QLabel("Press \"Open manager\" to launch the LSW manager. It lists your VM and the "
                   "apps installed inside it. Double-click one to launch it, and its window "
                   "appear on your host, rendered through the LSW render-server.");
    body->setWordWrap(true);
    lay->addWidget(body);
    lay->addStretch(1);
}

void DonePage::initializePage() {
    if (auto* w = wizard()) {
        w->setButtonText(QWizard::FinishButton, "Open manager");
    }
}

}  // namespace lsw::host::wizard
