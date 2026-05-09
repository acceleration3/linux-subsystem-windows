#include "lsw/manager/ui/wizard/welcome_page.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace lsw::manager::ui::wizard {

WelcomePage::WelcomePage(QWidget* parent) : QWizardPage(parent) {
    setTitle("Welcome to LSW");
    setSubTitle("Run Windows applications on Linux with native-feeling integration.");

    auto* lay = new QVBoxLayout(this);
    auto* body =
        new QLabel("This wizard will:\n"
                   "  1. Acquire a Windows installation ISO (download or pick a local file).\n"
                   "  2. Create a virtual machine sized to your liking.\n"
                   "  3. Run an unattended Windows install.\n"
                   "  4. Install the LSW guest payload (ICD, DXVK, VKD3D-Proton, guest agent).\n"
                   "  5. Hand you off to the manager so you can launch your apps.\n");
    body->setWordWrap(true);
    lay->addWidget(body);
    lay->addStretch(1);
}

}  // namespace lsw::manager::ui::wizard
