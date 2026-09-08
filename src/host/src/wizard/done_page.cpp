#include "lsw/host/wizard/done_page.hpp"
#include "lsw/host/wizard/page_chrome.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

DonePage::DonePage(Wizard& wizard, QWidget* parent)
    : QWizardPage(parent), wizard_(wizard) {

    auto* lay = PageChrome::install(this, QStringLiteral("Installation finished"), QStringLiteral("The guest agent is online. The VM is ready."));
    // What was actually built, as four tiles, then where to go.
    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    const auto tile = [](const QString& key, const QString& value, QLabel** value_out) {
        auto* frame = new QFrame;
        frame->setObjectName(QString::fromLatin1(ui::Theme::Role::kPanel));
        auto* row = new QHBoxLayout(frame);
        row->setContentsMargins(13, 11, 13, 11);
        auto* k = new QLabel(key.toUpper());
        k->setObjectName(QString::fromLatin1(ui::Theme::Role::kCaption));
        auto* v = new QLabel(value);
        v->setObjectName(QString::fromLatin1(ui::Theme::Role::kMono));
        v->setStyleSheet(QStringLiteral("font-size: 12px;"));
        row->addWidget(k);
        row->addStretch(1);
        row->addWidget(v);
        if (value_out != nullptr) {
            *value_out = v;
        }
        return frame;
    };
    grid->addWidget(tile(QStringLiteral("Edition"), QString{}, &edition_value_), 0, 0);
    grid->addWidget(tile(QStringLiteral("Machine"), QString{}, &machine_value_), 0, 1);
    grid->addWidget(tile(QStringLiteral("Runtimes"), QStringLiteral("DXVK, VKD3D-Proton"),
                         nullptr),
                    1, 0);
    grid->addWidget(tile(QStringLiteral("Transport"), QStringLiteral("shared memory"),
                         nullptr),
                    1, 1);
    lay->addLayout(grid);

    auto* body = new QLabel(
        QStringLiteral("The manager lists the applications found inside the guest. "
                       "Double-click one and its window opens on your desktop, rendered "
                       "through the LSW render server. Installing more Windows software "
                       "is done from inside the guest."));
    body->setObjectName(QString::fromLatin1(ui::Theme::Role::kDim));
    body->setWordWrap(true);
    lay->addWidget(body);
    lay->addStretch(1);
}

void DonePage::initializePage() {
    const auto& state = wizard_.state();
    edition_value_->setText(state.fido_edition_label.isEmpty()
                                ? QStringLiteral("image %1").arg(state.image_index)
                                : state.fido_edition_label);
    machine_value_->setText(QStringLiteral("%1 cores, %2 GiB, %3 GiB")
                                .arg(state.draft.sizing.cpu_cores)
                                .arg(state.draft.sizing.memory_mib / 1024)
                                .arg(state.draft.sizing.disk_gib));
}

}  // namespace lsw::host::wizard
