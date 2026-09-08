#pragma once

#include <QString>

class QVBoxLayout;
class QWizardPage;

namespace lsw::host::wizard {

// The frame every wizard page sits in: a title band on the surface
// colour with a hairline under it, then the body with the design's
// padding.
//
// QWizard would draw the title itself, on the page ground, with a rule
// it picks. That is one of two places the wizard chrome was visibly not
// the design, and it is fixed by not letting QWizard draw it: the page
// keeps its title empty and builds the band here instead.
class PageChrome {
public:
    PageChrome() = delete;

    // Installs the band and the body on ``page`` and returns the body
    // layout for the page's own content. Sets nothing QWizard would
    // draw.
    static QVBoxLayout* install(QWizardPage* page, const QString& title,
                                const QString& subtitle);

    // The title band's own layout, for the one page that puts its
    // progress up there with the title rather than in the body.
    [[nodiscard]] static QVBoxLayout* head_of(QWizardPage* page);
};

}  // namespace lsw::host::wizard
