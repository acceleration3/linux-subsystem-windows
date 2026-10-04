#pragma once

#include <QString>

class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class QWizardPage;

namespace lsw::host::wizard {

// The frame every wizard page sits in: the step rail down the left, a
// title band on the surface colour with a hairline under it, the body
// with the design's padding, and the page's own footer.
//
// QWizard would draw the title itself, on the page ground, with a rule
// it picks. That is one of two places the wizard chrome was visibly not
// the design, and it is fixed by not letting QWizard draw it: the page
// keeps its title empty and builds the band here instead.
//
// The parts the wizard fills in as pages change are handed back by
// pointer, so it reaches them through the page rather than by looking
// them up by name.
struct PageChrome {
    // The title band's own layout, for the one page that puts its
    // progress up there with the title rather than in the body.
    QVBoxLayout* head = nullptr;
    // Where the page's own content goes.
    QVBoxLayout* body = nullptr;

    // The rail: the steps, and the two lines about the host under them.
    QListWidget* steps = nullptr;
    QLabel* rail_host = nullptr;
    QLabel* rail_free = nullptr;

    // The footer: "step n of m" on the left, the buttons on the right.
    QLabel* counter = nullptr;
    QPushButton* cancel = nullptr;
    QPushButton* back = nullptr;
    QPushButton* next = nullptr;

    // Builds the frame on ``page`` and wires the footer's buttons to the
    // wizard. Sets nothing QWizard would draw.
    [[nodiscard]] static PageChrome install(QWizardPage* page, const QString& title,
                                            const QString& subtitle);
};

}  // namespace lsw::host::wizard
