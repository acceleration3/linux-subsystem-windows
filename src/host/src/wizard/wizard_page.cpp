#include "lsw/host/wizard/wizard_page.hpp"

namespace lsw::host::wizard {

QVBoxLayout* WizardPage::install_chrome(const QString& title, const QString& about,
                                        const QString& subtitle) {
    chrome_ = PageChrome::install(this, title, about, subtitle);
    return chrome_.body;
}

}
