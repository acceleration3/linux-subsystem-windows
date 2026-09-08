#include "lsw/host/wizard/page_chrome.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

namespace lsw::host::wizard {

namespace {

using ui::Theme;

// The step rail, built into the page rather than handed to
// QWizard::setSideWidget.
//
// A side widget is placed in a column of QWizard's own layout, and that
// layout insets everything it holds by the style's margin. There is no
// way to reach that margin: it is read from the style with the page as
// the widget, and the stylesheet's style answers first. With no side
// widget the page fills the window exactly, so the rail lives here and
// the page is the whole of what QWizard lays out.
QWidget* build_rail() {
    auto* rail = new QWidget;
    Theme::surface(rail, Theme::Role::kWizardSide);
    rail->setFixedWidth(212);

    auto* layout = new QVBoxLayout(rail);
    layout->setContentsMargins(0, 18, 0, 14);
    layout->setSpacing(0);

    auto* caption = new QLabel(QStringLiteral("SETUP"));
    caption->setObjectName(QString::fromLatin1(Theme::Role::kCaption));
    caption->setContentsMargins(18, 0, 18, 12);
    layout->addWidget(caption);

    auto* steps = new QListWidget;
    steps->setObjectName(QString::fromLatin1(Theme::Role::kWizardSteps));
    steps->setSelectionMode(QAbstractItemView::SingleSelection);
    steps->setFocusPolicy(Qt::NoFocus);
    steps->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    steps->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    steps->setFrameShape(QFrame::NoFrame);
    steps->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(steps);
    layout->addStretch(1);

    auto* foot = new QFrame;
    Theme::surface(foot, Theme::Role::kWizardSideFoot);
    auto* foot_layout = new QVBoxLayout(foot);
    foot_layout->setContentsMargins(18, 12, 18, 0);
    foot_layout->setSpacing(3);
    for (const char* name : {"RailHost", "RailFree"}) {
        auto* line = new QLabel;
        line->setObjectName(QString::fromLatin1(name));
        line->setProperty("lswRole", QString::fromLatin1(Theme::Role::kMeta));
        foot_layout->addWidget(line);
    }
    layout->addWidget(foot);
    return rail;
}

}  // namespace

QVBoxLayout* PageChrome::head_of(QWizardPage* page) {
    auto* head =
        page->findChild<QFrame*>(QString::fromLatin1(ui::Theme::Role::kWizardHead));
    return head == nullptr ? nullptr
                           : qobject_cast<QVBoxLayout*>(head->layout());
}

QVBoxLayout* PageChrome::install(QWizardPage* page, const QString& title,
                                 const QString& subtitle) {
    auto* columns = new QHBoxLayout(page);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);
    columns->addWidget(build_rail());

    auto* right = new QWidget;
    auto* root = new QVBoxLayout(right);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    columns->addWidget(right, 1);

    // ---- title band -----------------------------------------------
    auto* head = new QFrame;
    Theme::surface(head, Theme::Role::kWizardHead);
    head->setProperty("lswHead", true);
    auto* head_layout = new QVBoxLayout(head);
    head_layout->setContentsMargins(26, 22, 26, 18);
    head_layout->setSpacing(6);

    auto* heading = new QLabel(title);
    heading->setObjectName(QString::fromLatin1(Theme::Role::kWizardTitle));
    heading->setWordWrap(true);
    head_layout->addWidget(heading);

    auto* sub = new QLabel(subtitle);
    sub->setObjectName(QString::fromLatin1(Theme::Role::kDim));
    sub->setWordWrap(true);
    sub->setVisible(!subtitle.isEmpty());
    head_layout->addWidget(sub);
    root->addWidget(head);

    // ---- body -----------------------------------------------------
    auto* body_widget = new QWidget;
    auto* body = new QVBoxLayout(body_widget);
    body->setContentsMargins(26, 22, 26, 22);
    body->setSpacing(16);
    root->addWidget(body_widget, 1);

    // ---- footer ---------------------------------------------------
    // Beside the rail, not under it: the design's footer belongs to the
    // page. QWizard's own button row spans the window and cannot be
    // told otherwise, so it is emptied and this stands in.
    auto* foot = new QFrame;
    Theme::surface(foot, Theme::Role::kWizardFoot);
    auto* foot_layout = new QHBoxLayout(foot);
    foot_layout->setContentsMargins(20, 14, 20, 14);
    foot_layout->setSpacing(10);

    auto* counter = new QLabel;
    counter->setObjectName(QStringLiteral("StepCounter"));
    foot_layout->addWidget(counter);
    foot_layout->addStretch(1);

    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    cancel->setObjectName(QStringLiteral("WizCancel"));
    cancel->setVisible(false);
    foot_layout->addWidget(cancel);

    auto* back = new QPushButton(QStringLiteral("Back"));
    back->setObjectName(QStringLiteral("WizBack"));
    foot_layout->addWidget(back);

    auto* next = new QPushButton(QStringLiteral("Next"));
    next->setObjectName(QStringLiteral("WizNext"));
    foot_layout->addWidget(next);
    root->addWidget(foot);

    // QWizardPage::wizard() is protected; the wizard is the page's
    // top-level window, which is not.
    const auto wizard_of = [page]() { return qobject_cast<QWizard*>(page->window()); };
    QObject::connect(back, &QPushButton::clicked, page, [wizard_of] {
        if (auto* wizard = wizard_of()) {
            wizard->back();
        }
    });
    QObject::connect(next, &QPushButton::clicked, page, [wizard_of] {
        if (auto* wizard = wizard_of()) {
            if (wizard->nextId() == -1) {
                wizard->accept();
            } else {
                wizard->next();
            }
        }
    });
    QObject::connect(cancel, &QPushButton::clicked, page, [wizard_of] {
        if (auto* wizard = wizard_of()) {
            wizard->reject();
        }
    });
    // Not asked now: this runs at the top of the page's constructor,
    // before the widgets isComplete() reads exist. The wizard asks when
    // the page is shown.
    QObject::connect(page, &QWizardPage::completeChanged, next,
                     [page, next] { next->setEnabled(page->isComplete()); });
    return body;
}

}  // namespace lsw::host::wizard
