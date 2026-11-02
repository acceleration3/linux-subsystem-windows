#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

namespace lsw::host::wizard {
class WelcomePage : public WizardPage {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);
};

}
