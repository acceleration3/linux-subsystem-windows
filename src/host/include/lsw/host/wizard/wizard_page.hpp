#pragma once

#include "lsw/host/wizard/page_chrome.hpp"

#include <QWizardPage>

namespace lsw::host::wizard {

// A page of the first-run wizard: a QWizardPage in the wizard's own
// frame, which the wizard reaches through ``chrome()`` to keep the rail
// and the footer in step with where the user is.
class WizardPage : public QWizardPage {
    Q_OBJECT
public:
    using QWizardPage::QWizardPage;

    [[nodiscard]] const PageChrome& chrome() const noexcept { return chrome_; }

protected:
    // Builds the frame; returns the body for the page's own content.
    // Called once, first thing in the page's constructor.
    QVBoxLayout* install_chrome(const QString& title, const QString& subtitle);

private:
    PageChrome chrome_;
};

}  // namespace lsw::host::wizard
