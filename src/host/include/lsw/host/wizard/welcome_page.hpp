#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

namespace lsw::host::wizard {

// Step 1: introduces what the wizard will do. No user input.
class WelcomePage : public WizardPage {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);
};

}  // namespace lsw::host::wizard
