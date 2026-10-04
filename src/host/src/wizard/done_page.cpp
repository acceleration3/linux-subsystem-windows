#include "lsw/host/wizard/done_page.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include "lsw/host/services/host_services.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWizard>

namespace lsw::host::wizard {

DonePage::DonePage(Wizard& wizard, QWidget* parent) : WizardPage(parent), wizard_(wizard) {

    auto* lay = install_chrome(QStringLiteral("Installation finished"),
                               QStringLiteral("The guest agent is online. The VM is ready."));
    // What was actually built, as four tiles, then where to go.
    auto* grid = new QGridLayout;
    grid->setSpacing(8);
    const auto tile = [](const QString& key, const QString& value, QLabel** value_out) {
        auto* frame = new QFrame;
        ui::Theme::surface(frame, ui::Theme::Role::kPanel);
        auto* row = new QHBoxLayout(frame);
        row->setContentsMargins(13, 11, 13, 11);
        auto* k = ui::Widgets::caption(key);
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
    // The guest as it is now, from the status hub: the display adapter
    // and whether Windows started avgpu.sys on it, and the agent.
    grid->addWidget(tile(QStringLiteral("Graphics"), QStringLiteral("—"), &graphics_value_), 1, 0);
    grid->addWidget(tile(QStringLiteral("Agent"), QStringLiteral("—"), &agent_value_), 1, 1);
    connect(&wizard_.services().status(), &services::StatusHub::changed, this,
            &DonePage::show_guest);
    lay->addLayout(grid);

    auto* body = new QLabel(
        QStringLiteral("The manager lists the applications found inside the guest. "
                       "Double click one and its window opens on your desktop, drawn on "
                       "this machine's GPU through the guest's accel-virt adapter. "
                       "Installing more Windows software is done from inside the guest."));
    body->setObjectName(QString::fromLatin1(ui::Theme::Role::kDim));
    body->setWordWrap(true);
    lay->addWidget(body);
    lay->addStretch(1);
}

void DonePage::initializePage() {
    const auto& state = wizard_.state();
    // The setup page hands over here when the run fails too, so the
    // title says how it ended rather than assuming.
    if (!state.failure.isEmpty()) {
        chrome().title->setText(QStringLiteral("Installation did not finish"));
        chrome().subtitle->setText(state.failure);
    } else {
        chrome().title->setText(QStringLiteral("Installation finished"));
        chrome().subtitle->setText(QStringLiteral("The guest agent is online. The VM is ready."));
    }
    chrome().subtitle->setVisible(true);
    // Watch the machine that was built, so the last two tiles are what
    // it is doing rather than what it was meant to.
    auto& services = wizard_.services();
    if (const config::VmRecord* record = services.vms().primary()) {
        services.status().set_target(record->uuid);
        services.status().refresh(services::StatusHub::Area::Graphics);
    }
    show_guest();
    edition_value_->setText(state.fido_edition_label.isEmpty()
                                ? QStringLiteral("image %1").arg(state.image_index)
                                : state.fido_edition_label);
    machine_value_->setText(QStringLiteral("%1 cores, %2 GiB, %3 GiB")
                                .arg(state.draft.sizing.cpu_cores)
                                .arg(state.draft.sizing.memory_mib / 1024)
                                .arg(state.draft.sizing.disk_gib));
}

void DonePage::show_guest() {
    const auto& status = wizard_.services().status();
    const auto& vm = status.vm();
    const auto& g = status.graphics();
    if (vm.uuid.isNull()) {
        graphics_value_->setText(QStringLiteral("no machine"));
        agent_value_->setText(QStringLiteral("no machine"));
        return;
    }
    agent_value_->setText(!vm.running()        ? QStringLiteral("guest off")
                          : vm.agent_online() ? QStringLiteral("answering")
                                              : QStringLiteral("not answering yet"));
    if (g.adapters.isEmpty()) {
        graphics_value_->setText(g.probed ? QStringLiteral("no host GPU found")
                                          : QStringLiteral("asking the host"));
        return;
    }
    const auto& display = g.adapters.first();
    const QString name = platform::HostGpus::adapter_name(display.gpu);
    QString state;
    if (!vm.running()) {
        state = QStringLiteral("guest off");
    } else if (!g.guest.valid) {
        state = QStringLiteral("asking Windows");
    } else if (!display.guest) {
        state = QStringLiteral("not in Windows");
    } else if (display.guest->running()) {
        state = QStringLiteral("avgpu.sys started");
    } else {
        state = QStringLiteral("problem code %1").arg(display.guest->problem_code);
    }
    graphics_value_->setText(QStringLiteral("%1, %2").arg(name, state));
}

}  // namespace lsw::host::wizard
