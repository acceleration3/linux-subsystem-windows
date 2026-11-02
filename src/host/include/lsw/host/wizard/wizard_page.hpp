#pragma once

#include "lsw/host/wizard/page_chrome.hpp"

#include <QWizardPage>

namespace lsw::host::wizard {
class WizardPage : public QWizardPage {
    Q_OBJECT
public:
    using QWizardPage::QWizardPage;

    [[nodiscard]] const PageChrome& chrome() const noexcept { return chrome_; }

protected:
    QVBoxLayout* install_chrome(const QString& title, const QString& subtitle);

private:
    PageChrome chrome_;
};

}
