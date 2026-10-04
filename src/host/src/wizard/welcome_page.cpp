#include "lsw/host/wizard/welcome_page.hpp"

#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::wizard {

namespace {

using ui::Theme;

// One stage of the install. Four short cards rather than a numbered
// paragraph: the same information, scannable, and it does not turn into
// a run-on sentence in a language with longer words.
QWidget* stage_card(const QString& number, const QString& title, const QString& body) {
    auto* frame = new QFrame;
    Theme::surface(frame, Theme::Role::kPanel);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(13, 12, 13, 12);
    layout->setSpacing(5);

    auto* heading = ui::Widgets::caption(QStringLiteral("%1 · %2").arg(number, title));
    heading->setProperty(Theme::Tone::kProperty, Theme::Tone::kAmber);

    auto* text = new QLabel(body);
    text->setObjectName(QString::fromLatin1(Theme::Role::kDim));
    text->setWordWrap(true);

    layout->addWidget(heading);
    layout->addWidget(text, 1);
    return frame;
}

}  // namespace

WelcomePage::WelcomePage(QWidget* parent) : WizardPage(parent) {

    auto* root =
        install_chrome(QStringLiteral("Run Windows applications on Linux"),
                       QStringLiteral("LSW installs a Windows guest and renders its windows onto "
                                      "your desktop. Four stages, roughly 25 minutes, mostly "
                                      "unattended."));

    auto* grid = new QGridLayout;
    grid->setSpacing(10);
    grid->addWidget(stage_card(QStringLiteral("01"), QStringLiteral("Image"),
                               QStringLiteral("Point at a Windows ISO you have. Tools to "
                                              "download one or shrink one sit next to "
                                              "the field.")),
                    0, 0);
    grid->addWidget(stage_card(QStringLiteral("02"), QStringLiteral("Machine"),
                               QStringLiteral("Size cores, memory and disk, and name the "
                                              "local Windows account.")),
                    0, 1);
    grid->addWidget(stage_card(QStringLiteral("03"), QStringLiteral("Payload"),
                               QStringLiteral("The answer file, the guest binaries, and "
                                              "the drivers that must exist on the very "
                                              "first boot.")),
                    1, 0);
    grid->addWidget(stage_card(QStringLiteral("04"), QStringLiteral("Install"),
                               QStringLiteral("Unattended Windows setup, then the guest "
                                              "agent, DXVK and VKD3D-Proton.")),
                    1, 1);
    root->addLayout(grid);

    auto* footnote = new QLabel(
        QStringLiteral("Nothing on this machine is modified outside <span style=\"color:%1;"
                       "font-family:'JetBrains Mono','DejaVu Sans Mono',monospace\">"
                       "~/.local/share/lsw</span>. The guest is a single qcow2 file you "
                       "can delete.")
            .arg(QString::fromLatin1(Theme::Color::kAmber)));
    footnote->setObjectName(QString::fromLatin1(Theme::Role::kDim));
    footnote->setTextFormat(Qt::RichText);
    footnote->setWordWrap(true);
    root->addWidget(footnote);
    root->addStretch(1);
}

}  // namespace lsw::host::wizard
