#pragma once

#include <QWizardPage>

namespace lsw::manager::ui::wizard {

// Step 8: confirmation. ``finishButton()`` -> ``Wizard::accept()``
// commits the VmRecord and switches to the manager UI.
class DonePage : public QWizardPage {
    Q_OBJECT
public:
    explicit DonePage(QWidget* parent = nullptr);

    void initializePage() override;
};

}  // namespace lsw::manager::ui::wizard
