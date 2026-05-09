#include "lsw/manager/ui/wizard/done_page.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::manager::ui::wizard {

DonePage::DonePage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Installation finished");
    setSubTitle("The guest agent is online — the VM is ready.");

    auto* lay = new QVBoxLayout(this);
    auto* body =
        new QLabel("Press \"Open manager\" to launch the LSW manager. It lists your VM and the "
                   "apps installed inside it; double-click an app to launch it — its window will "
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

}  // namespace lsw::manager::ui::wizard
