#include "lsw/host/wizard/welcome_page.hpp"

#include "lsw/host/ui/info_tip.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::wizard {

namespace {

using ui::Theme;

QWidget* stage_card(const QString& number, const QString& title, const QString& line,
                    const QString& about) {
    auto* frame = new QFrame;
    Theme::surface(frame, Theme::Role::kPanel);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(13, 12, 13, 12);
    layout->setSpacing(5);

    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    auto* heading = ui::Widgets::caption(QStringLiteral("%1 · %2").arg(number, title));
    heading->setProperty(Theme::Tone::kProperty, Theme::Tone::kAmber);
    head->addWidget(heading);
    head->addWidget(new ui::InfoTip(about));
    head->addStretch(1);

    auto* text = new QLabel(line);
    text->setObjectName(QString::fromLatin1(Theme::Role::kDim));

    layout->addLayout(head);
    layout->addWidget(text, 1);
    return frame;
}

}

WelcomePage::WelcomePage(QWidget* parent) : WizardPage(parent) {

    auto* root = install_chrome(
        QStringLiteral("Run Windows applications on Linux"),
        QStringLiteral("LSW installs a Windows guest and renders its windows onto your desktop. "
                       "Nothing on this machine is modified outside ~/.local/share/lsw; the guest "
                       "is a single qcow2 file you can delete."),
        QStringLiteral("Four stages, about 25 minutes, mostly unattended."));

    auto* grid = new QGridLayout;
    grid->setSpacing(10);
    grid->addWidget(stage_card(QStringLiteral("01"), QStringLiteral("Image"),
                               QStringLiteral("A Windows ISO"),
                               QStringLiteral("Point at a Windows ISO you have. Tools to download one "
                                              "or shrink one sit next to the field.")),
                    0, 0);
    grid->addWidget(stage_card(QStringLiteral("02"), QStringLiteral("Machine"),
                               QStringLiteral("Cores, memory, disk and account"),
                               QStringLiteral("Size cores, memory and disk, and name the local "
                                              "Windows account.")),
                    0, 1);
    grid->addWidget(stage_card(QStringLiteral("03"), QStringLiteral("Payload"),
                               QStringLiteral("Answer file and drivers"),
                               QStringLiteral("The answers Windows Setup reads, and the drivers "
                                              "Windows needs from its first start, LSW's graphics "
                                              "driver among them.")),
                    1, 0);
    grid->addWidget(stage_card(QStringLiteral("04"), QStringLiteral("Install"),
                               QStringLiteral("Unattended setup"),
                               QStringLiteral("Windows installs itself without questions, then LSW's "
                                              "software and graphics driver.")),
                    1, 1);
    root->addLayout(grid);
    root->addStretch(1);
}

}
