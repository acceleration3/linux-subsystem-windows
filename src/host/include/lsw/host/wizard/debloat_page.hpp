#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QButtonGroup>
#include <QLabel>
#include <QRadioButton>
#include <QWizardPage>

namespace lsw::host::wizard {

class Wizard;

// Step 4: how much of Windows to remove from the image before it is
// installed.
//
// Three mutually-exclusive choices in one QButtonGroup rather than a
// combo box: each one needs a sentence of its own to be chosen
// honestly, and the middle option is not the middle of a scale — it is
// the one that leaves the machine serviceable, which is the whole
// distinction the page exists to make.
class DebloatPage : public QWizardPage {
    Q_OBJECT
public:
    explicit DebloatPage(Wizard& wizard, QWidget* parent = nullptr);

    [[nodiscard]] int nextId() const override;
    [[nodiscard]] bool validatePage() override;

private:
    // Update the summary and the warning to match the selection.
    void on_selection_changed();

    Wizard& wizard_;
    QButtonGroup* group_ = nullptr;
    QRadioButton* none_ = nullptr;
    QRadioButton* sane_ = nullptr;
    QRadioButton* maximum_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* warning_ = nullptr;
};

}  // namespace lsw::host::wizard
