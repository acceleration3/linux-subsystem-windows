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
    // The two tiles read from the status hub: the guest's display adapter
    // and whether its agent answers, as they are now.
    void show_guest();

    Wizard& wizard_;
    QLabel* edition_value_ = nullptr;
    QLabel* machine_value_ = nullptr;
    QLabel* graphics_value_ = nullptr;
    QLabel* agent_value_ = nullptr;
};

}  // namespace lsw::host::wizard
