#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

namespace lsw::host::wizard {
class PipelinePage : public WizardPage {
    Q_OBJECT
public:
    using WizardPage::WizardPage;

    [[nodiscard]] bool isComplete() const override { return complete_; }

protected:
    void reset_complete();
    void finish_and_advance();

private:
    bool complete_ = false;
};

}
