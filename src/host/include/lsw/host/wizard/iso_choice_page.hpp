#pragma once

#include "lsw/host/wizard/wizard_page.hpp"

class QLabel;
class QLineEdit;
class QPushButton;

namespace lsw::host::wizard {

class Wizard;

// Which image to install.
//
// One field. Getting an image and shrinking one are both tools, opened
// from the buttons below it, and neither is on the path to Next: they
// write an ISO somewhere and hand back its path, and a failure in
// either leaves this field exactly as it was.
//
// It used to be two radio cards, one of which was a whole Microsoft
// download flow with its own edition list, session, SKU query and ban
// state. Downloading is not an alternative way to answer the question
// this page asks, it is a thing that produces an answer.
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
    // Set when the path came out of the trim tool, which has already
    // run the preparation pass the installer needs.
    bool prepared_ = false;
};

}  // namespace lsw::host::wizard
