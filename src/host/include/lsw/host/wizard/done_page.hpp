#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

class QLabel;

namespace lsw::host::wizard {

class Wizard;

class DonePage : public WizardPage {
    Q_OBJECT
public:
    explicit DonePage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;

private:
    void show_guest();

    Wizard& wizard_;
    QLabel* edition_value_ = nullptr;
    QLabel* machine_value_ = nullptr;
    QLabel* graphics_value_ = nullptr;
    QLabel* agent_value_ = nullptr;
};

}
