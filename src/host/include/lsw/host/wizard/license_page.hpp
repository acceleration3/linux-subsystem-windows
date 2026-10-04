#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

#include <QCheckBox>

namespace lsw::host::wizard {

// Step 2: shows a brief Windows license blurb pointing to Microsoft's
// EULA URL; user must check ``I accept`` to proceed (page completes).
class LicensePage : public WizardPage {
    Q_OBJECT
public:
    explicit LicensePage(QWidget* parent = nullptr);
    [[nodiscard]] bool isComplete() const override;

private:
    QCheckBox* accept_box_ = nullptr;
};

}  // namespace lsw::host::wizard
