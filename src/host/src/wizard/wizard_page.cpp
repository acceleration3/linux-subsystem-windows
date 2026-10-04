#include "lsw/host/wizard/wizard_page.hpp"

namespace lsw::host::wizard {

QVBoxLayout* WizardPage::install_chrome(const QString& title, const QString& subtitle) {
    chrome_ = PageChrome::install(this, title, subtitle);
    return chrome_.body;
}

}  // namespace lsw::host::wizard
