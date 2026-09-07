#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QButtonGroup>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
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
//
// The work happens HERE, not later. That is why this page sits after
// the download rather than beside the level question it asks: trimming
// needs the image on disk and the edition already chosen, and doing it
// under a progress bar the user is watching beats doing it invisibly
// inside a pipeline phase. Next stays disabled until it finishes, and
// the pipeline afterwards is handed an ISO that is already trimmed.
class DebloatPage : public QWizardPage {
    Q_OBJECT
public:
    explicit DebloatPage(Wizard& wizard, QWidget* parent = nullptr);

    void initializePage() override;
    [[nodiscard]] int nextId() const override;
    [[nodiscard]] bool isComplete() const override;
    [[nodiscard]] bool validatePage() override;

private:
    // Update the summary and the warning to match the selection.
    void on_selection_changed();
    // Run the trim for the selected level.
    void start_trim();

    Wizard& wizard_;
    QButtonGroup* group_ = nullptr;
    QRadioButton* none_ = nullptr;
    QRadioButton* sane_ = nullptr;
    QRadioButton* maximum_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* warning_ = nullptr;
    QPushButton* start_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QLabel* status_ = nullptr;
    bool done_ = false;
};

}  // namespace lsw::host::wizard
