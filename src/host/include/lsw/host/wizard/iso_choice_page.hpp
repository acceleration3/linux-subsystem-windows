#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

class QLabel;
class QLineEdit;
class QPushButton;

namespace lsw::host::wizard {

class Wizard;

class IsoChoicePage : public WizardPage {
    Q_OBJECT
public:
    explicit IsoChoicePage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] bool isComplete() const override;
    bool validatePage() override;
    [[nodiscard]] int nextId() const override;

private:
    void describe_image();
    void open_download_tool();
    void open_trim_tool();

    Wizard& wizard_;
    QLineEdit* path_edit_ = nullptr;
    QLabel* detail_ = nullptr;
    QPushButton* trim_button_ = nullptr;
    bool prepared_ = false;
};

}
