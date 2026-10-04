#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

class QLabel;

namespace lsw::host::wizard {

class Wizard;

// Step 8: what was built, and the one button that goes where the user
// wants to be. ``finishButton()`` -> ``Wizard::accept()`` commits the
// VmRecord and switches to the manager UI.
class DonePage : public WizardPage {
    Q_OBJECT
public:
    explicit DonePage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;

private:
    Wizard& wizard_;
    QLabel* edition_value_ = nullptr;
    QLabel* machine_value_ = nullptr;
};

}  // namespace lsw::host::wizard
