#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

namespace lsw::host::wizard {

// A page the install pipeline drives rather than the user. It starts
// out incomplete every time it is shown, and moves the wizard on by
// itself once the stage it shows is over: nobody has to press Next on
// a progress screen.
class PipelinePage : public WizardPage {
    Q_OBJECT
public:
    using WizardPage::WizardPage;

    [[nodiscard]] bool isComplete() const override { return complete_; }

protected:
    // From initializePage: the stage has not finished yet.
    void reset_complete();
    // The stage is over. Marks the page complete and advances once
    // control is back in the event loop, out of the pipeline signal
    // that said so.
    void finish_and_advance();

private:
    bool complete_ = false;
};

}  // namespace lsw::host::wizard
