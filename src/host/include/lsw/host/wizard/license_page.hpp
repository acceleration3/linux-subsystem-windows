#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

#include <QCheckBox>

namespace lsw::host::wizard {
class LicensePage : public WizardPage {
    Q_OBJECT
public:
    explicit LicensePage(QWidget* parent = nullptr);
    [[nodiscard]] bool isComplete() const override;

private:
    QCheckBox* accept_box_ = nullptr;
};

}
