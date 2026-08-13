#pragma once

#include <QWizardPage>

namespace lsw::host::wizard {

// Step 1: introduces what the wizard will do. No user input.
class WelcomePage : public QWizardPage {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);
};

}  // namespace lsw::host::wizard
