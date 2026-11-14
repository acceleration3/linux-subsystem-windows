#include "lsw/host/manager/settings_page.hpp"

#include "lsw/host/config/log_verbosity.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/vm_sizing.hpp"
#include "lsw/host/config/port_forwards.hpp"
#include "lsw/host/manager/manager_exit.hpp"
#include "lsw/host/manager/port_forward_editor.hpp"
#include "lsw/host/manager/setting_controls.hpp"
#include "lsw/host/platform/host_memory.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/machine_limits.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/slider_spin.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/toggle_switch.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;

constexpr int kControlWidth = 292;
constexpr int kControlLeast = 190;
constexpr int kMibPerGib = 1024;

QString mono(const QString& text) {
    return QStringLiteral("<span style=\"font-family:'JetBrains Mono','DejaVu Sans Mono',monospace;"
                          "font-size:11px\">%1</span>")
        .arg(text.toHtmlEscaped());
}

QWidget* right_aligned(QWidget* widget) {
    auto* cell = new QWidget;
    auto* row = new QHBoxLayout(cell);
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch(1);
    row->addWidget(widget);
    return cell;
}

}

SettingsPage::SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                           services::StatusHub& status, config::LogVerbosity& verbosity,
                           QWidget* parent)
    : ui::ScrollPage(parent), pending_(pending), status_(status),
      verbosity_(verbosity), controls_(new SettingControls(settings, pending, this)) {
    build_machine();
    build_network(settings);
    build_startup();
    build_desktop();
    build_logging();
    column()->addStretch(1);

    debug_clock_.setInterval(15000);
    connect(&debug_clock_, &QTimer::timeout, this, &SettingsPage::refresh_debug_row);
    connect(&verbosity_, &config::LogVerbosity::changed, this, &SettingsPage::refresh_debug_row);
    refresh_debug_row();
}

void SettingsPage::showEvent(QShowEvent* event) {
    ui::ScrollPage::showEvent(event);
    refresh_network();
    refresh_debug_row();
    debug_clock_.start();
}

void SettingsPage::hideEvent(QHideEvent* event) {
    ui::ScrollPage::hideEvent(event);
    debug_clock_.stop();
}

QVBoxLayout* SettingsPage::new_group(const QString& title) {
    const auto group = ui::Panel::make(title);
    group.body->setContentsMargins(0, 0, 0, 0);
    group.body->setSpacing(0);
    column()->addWidget(group.frame);
    return group.body;
}

QWidget* SettingsPage::add_row(QVBoxLayout* group, const QString& key, const QString& label,
                               const QString& description, QWidget* control) {
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(18);

    auto* text = new QVBoxLayout;
    text->setSpacing(3);
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    head->addWidget(new QLabel(label));
    if (!key.isEmpty()) {
        head->addWidget(Widgets::tag(config::ManagerSettings::needs_restart(key)));
    }
    head->addStretch(1);
    text->addLayout(head);

    auto* prose = Widgets::prose(description);
    prose->setObjectName(QStringLiteral("rowDescription"));
    prose->setTextFormat(Qt::RichText);
    text->addWidget(prose);

    layout->addLayout(text, 1);
    control->setMinimumWidth(kControlLeast);
    control->setMaximumWidth(kControlWidth);
    control->setSizePolicy(QSizePolicy::Preferred, control->sizePolicy().verticalPolicy());
    layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);

    group->addWidget(row);
    group->addWidget(Widgets::separator());
    return row;
}

void SettingsPage::build_machine() {
    auto* machine = new_group(QStringLiteral("Machine"));
    auto* cores = controls_->slider(K::kCpuCores, ui::MachineLimits::kCores, QString{});
    cores->set_maximum(config::VmSizing::host_cores());
    add_row(machine, K::kCpuCores, QStringLiteral("Processor cores"),
            QStringLiteral("The host has %1 threads. The guest topology is one socket, cores only, "
                           "no SMT.")
                .arg(config::VmSizing::host_cores()),
            cores);

    auto* priority_row = add_row(
        machine, K::kPriority, QStringLiteral("Priority"), QString{},
        controls_->choice(K::kPriority, {
                                            {QStringLiteral("Normal"), QStringLiteral("normal")},
                                            {QStringLiteral("High"), QStringLiteral("high")},
                                            {QStringLiteral("Highest"), QStringLiteral("highest")},
                                        }));
    priority_note_ = priority_row->findChild<QLabel*>(QStringLiteral("rowDescription"));
    connect(&status_, &services::StatusHub::changed, this, [this](services::StatusHub::Area area) {
        if (area == services::StatusHub::Area::Host) {
            refresh_priority_note();
        }
    });
    refresh_priority_note();

    const auto host_mib = platform::HostMemory::total_mib();
    add_row(machine, K::kMemoryMib, QStringLiteral("Memory"),
            host_mib ? QStringLiteral("Fixed: the guest holds all of it while it runs. %1 GiB on "
                                      "the host.")
                           .arg(*host_mib / kMibPerGib)
                     : QStringLiteral("Fixed: the guest holds all of it while it runs."),
            controls_->slider(K::kMemoryMib, ui::MachineLimits::kMemoryMib, QStringLiteral(" MiB")));

    auto* disk = controls_->slider(K::kDiskGib, ui::MachineLimits::kDiskGib, QStringLiteral(" GiB"));
    add_row(machine, K::kDiskGib, QStringLiteral("Disk"),
            QStringLiteral("Grows only. The qcow2 is resized on the host and Windows extends the "
                           "volume on the next boot."),
            disk);
    const auto follow_disk = [this, disk] {
        const auto bytes = status_.vm().disk_bytes;
        if (!bytes) {
            return;
        }
        const int gib = static_cast<int>((*bytes + (1ULL << 30) - 1) >> 30);
        const QSignalBlocker quiet(disk);
        disk->set_minimum(std::max(ui::MachineLimits::kDiskGib.min, gib));
        disk->set_value(pending_.staged_or_current(K::kDiskGib).toInt());
    };
    connect(&status_, &services::StatusHub::changed, disk,
            [follow_disk](services::StatusHub::Area area) {
                if (area == services::StatusHub::Area::Vm) {
                    follow_disk();
                }
            });
    follow_disk();
}

QString SettingsPage::priority_note(const services::PriorityStatus& status) {
    using Level = services::PriorityStatus::Level;
    QStringList parts;
    switch (status.wanted) {
    case Level::Normal:
        parts << QStringLiteral("The guest shares the processors with other programs like any "
                                "program.");
        break;
    case Level::High:
        parts << QStringLiteral("When the processors are busy the guest and its windows get time "
                                "ahead of other programs.");
        break;
    case Level::Highest:
        parts << QStringLiteral("As High, and the threads that carry the picture run ahead of "
                                "ordinary threads.");
        break;
    }
    if (status.wanted == Level::Normal) {
        if (status.weight_raised) {
            parts << QStringLiteral("The larger share the guest had could not be taken back; it "
                                    "ends when the guest stops.");
        }
        return parts.join(QLatin1Char(' '));
    }
    if (!status.vm_running) {
        parts << QStringLiteral("In effect once the guest runs.");
        return parts.join(QLatin1Char(' '));
    }
    if (status.weight_refused) {
        parts << QStringLiteral("Not in effect: this session did not let the manager give the "
                                "guest a larger share of the processors.");
    }
    if (status.mirrors_refused) {
        parts << QStringLiteral("The window mirrors run with the normal share: this session did "
                                "not let the manager give them a larger one.");
    }
    if (status.wanted != Level::Highest) {
        return parts.join(QLatin1Char(' '));
    }
    if (status.threads_unnamed) {
        parts << QStringLiteral("The picture threads stay at normal priority: QEMU's threads "
                                "carry no names to tell them apart.");
    } else if (status.threads_refused) {
        parts << (status.threads_raised
                      ? QStringLiteral("The system refused the priority for some of the picture "
                                       "threads.")
                      : QStringLiteral("The picture threads stay at normal priority: this "
                                       "account's limits do not allow a higher one."));
    }
    if (status.clamped()) {
        parts << QStringLiteral("This account's limits allow nice %1 rather than %2.")
                     .arg(status.nice)
                     .arg(status.wanted_nice);
    }
    return parts.join(QLatin1Char(' '));
}

void SettingsPage::refresh_priority_note() {
    if (priority_note_ != nullptr) {
        priority_note_->setText(priority_note(status_.priority()));
    }
}

void SettingsPage::build_network(config::ManagerSettings& settings) {
    using Mode = config::ManagerSettings::NetworkMode;
    auto* network = new_group(QStringLiteral("Network"));
    add_row(network, K::kNetworkMode, QStringLiteral("Network"),
            QStringLiteral("Shared: Windows reaches the network through this computer and can be "
                           "reached only through the forwarded ports. Bridged: Windows also joins "
                           "a bridge of this computer and gets its own address on that network."),
            controls_->choice(K::kNetworkMode,
                              {
                                  {QStringLiteral("Shared with this computer (NAT)"), QString{Mode::kShared}},
                                  {QStringLiteral("Bridged to a host bridge"), QString{Mode::kBridged}},
                              }));

    bridge_ = new QComboBox;
    connect(bridge_, &QComboBox::activated, this,
            [this](int index) { controls_->put(K::kNetworkBridge, bridge_->itemData(index)); });
    auto* bridge_row = add_row(network, K::kNetworkBridge, QStringLiteral("Bridge"), QString{}, bridge_);
    bridge_note_ = bridge_row->findChild<QLabel*>(QStringLiteral("rowDescription"));
    controls_->follow(K::kNetworkMode, this, [this] { refresh_network(); });
    controls_->follow(K::kNetworkBridge, this, [this] { refresh_network(); });

    QStringList own;
    for (const config::PortForward& forward : config::PortForwards::own()) {
        own << QString::number(forward.host_port);
    }
    auto* sunshine = Widgets::button(QStringLiteral("Add Sunshine ports"));
    add_row(network, K::kPortForwards, QStringLiteral("Forwarded ports"),
            QStringLiteral("A connection to a port of this computer goes to a port in Windows. "
                           "LSW itself uses TCP %1. Add Sunshine ports forwards what Moonlight "
                           "needs to reach Sunshine in Windows from the network: TCP 47984, 47989 "
                           "and 48010, UDP 47998 to 48000.")
                .arg(own.join(QStringLiteral(", "))),
            right_aligned(sunshine));
    forwards_ = new PortForwardEditor(settings, pending_);
    network->addWidget(forwards_);
    connect(sunshine, &QPushButton::clicked, forwards_, [this] { (void)forwards_->add_sunshine(); });
    refresh_network();
}

QString SettingsPage::bridge_note(bool bridged, const QString& bridge,
                                  const platform::HostBridges::Facts& facts) {
    if (facts.bridges.isEmpty() && bridge.isEmpty()) {
        return QStringLiteral("This computer has no bridge. One must exist here before Windows "
                              "can join it; LSW does not create bridges.");
    }
    const QString missing = platform::HostBridges::missing(facts, bridge);
    if (!bridged) {
        if (bridge.isEmpty()) {
            return QStringLiteral("Used only when the network is bridged.");
        }
        return missing.isEmpty()
                   ? QStringLiteral("Used only when the network is bridged. Windows can join %1.")
                         .arg(bridge)
                   : QStringLiteral("Used only when the network is bridged. Windows could not "
                                    "join it now: %1")
                         .arg(missing);
    }
    if (!missing.isEmpty()) {
        return QStringLiteral("Not in effect, Windows starts with the shared network only: %1")
            .arg(missing);
    }
    return QStringLiteral("Windows gets a second network adapter on %1. LSW keeps its own "
                          "connections to Windows on the shared adapter.")
        .arg(bridge);
}

void SettingsPage::refresh_network() {
    if (bridge_ == nullptr || bridge_note_ == nullptr) {
        return;
    }
    const platform::HostBridges::Facts facts = platform::HostBridges::probe();
    const QString chosen = controls_->current(K::kNetworkBridge).toString();
    {
        const QSignalBlocker quiet(bridge_);
        bridge_->clear();
        if (chosen.isEmpty()) {
            bridge_->addItem(facts.bridges.isEmpty() ? QStringLiteral("No bridge on this computer")
                                                     : QStringLiteral("Choose a bridge"),
                             QString{});
        }
        for (const QString& name : facts.bridges) {
            bridge_->addItem(name, name);
        }
        if (!chosen.isEmpty() && !facts.bridges.contains(chosen)) {
            bridge_->addItem(QStringLiteral("%1 (not on this computer)").arg(chosen), chosen);
        }
        bridge_->setCurrentIndex(bridge_->findData(chosen));
        bridge_->setEnabled(bridge_->count() > 1);
    }
    const bool bridged = controls_->current(K::kNetworkMode).toString()
                         == config::ManagerSettings::NetworkMode::kBridged;
    bridge_note_->setText(bridge_note(bridged, chosen, facts).toHtmlEscaped());
}

void SettingsPage::build_startup() {
    auto* startup = new_group(QStringLiteral("Startup"));
    add_row(startup, K::kAutostart, QStringLiteral("Start the guest when the manager opens"),
            QStringLiteral("Otherwise it starts the first time you launch an application."),
            right_aligned(controls_->toggle(K::kAutostart)));
    add_row(startup, K::kCloseToTray, QStringLiteral("Keep running in the tray when closed"),
            QStringLiteral("On: closing the window hides it; the manager, its tray icon and the "
                           "guest keep running. Off: there is no tray icon and closing the window "
                           "quits the manager."),
            right_aligned(controls_->toggle(K::kCloseToTray)));
    add_row(startup, K::kShutGuestOnQuit, QStringLiteral("Shut the guest down when the manager quits"),
            QStringLiteral("On: quitting asks Windows to shut down and waits up to %1 seconds for "
                           "it before the manager exits. Off: the guest keeps running without the "
                           "manager.")
                .arg(std::chrono::duration_cast<std::chrono::seconds>(ManagerExit::kGuestWait).count()),
            right_aligned(controls_->toggle(K::kShutGuestOnQuit)));
}

void SettingsPage::build_desktop() {
    auto* desktop = new_group(QStringLiteral("Desktop integration"));
    add_row(desktop, K::kPublishEntries, QStringLiteral("Put guest applications in your menu"),
            QStringLiteral("Writes one %1 file per application into %2, with the icon pulled out "
                           "of the executable.")
                .arg(mono(QStringLiteral(".desktop")), mono(QStringLiteral("~/.local/share/applications"))),
            right_aligned(controls_->toggle(K::kPublishEntries)));
    add_row(desktop, K::kMenuCategory, QStringLiteral("Menu category"),
            QStringLiteral("The submenu they appear under. Written as a merged menu fragment, which "
                           "KDE, GNOME and LXQt all honour."),
            controls_->text(K::kMenuCategory));
    add_row(desktop, K::kFileAssociations,
            QStringLiteral("Open Windows file types with these applications"),
            QStringLiteral("Windows applications open the file types nothing on this machine "
                           "handles. For the rest, use Make it the default in an application's "
                           "settings."),
            right_aligned(controls_->toggle(K::kFileAssociations)));
    auto* clipboard_row =
        add_row(desktop, K::kShareClipboard, QStringLiteral("Share the clipboard"), QString{},
                right_aligned(controls_->toggle(K::kShareClipboard)));
    clipboard_note_ = clipboard_row->findChild<QLabel*>(QStringLiteral("rowDescription"));
    connect(&status_, &services::StatusHub::changed, this, [this](services::StatusHub::Area area) {
        if (area == services::StatusHub::Area::Host) {
            refresh_clipboard_note();
        }
    });
    refresh_clipboard_note();
    auto* rewrite = Widgets::button(QStringLiteral("Rewrite now"));
    connect(rewrite, &QPushButton::clicked, this, &SettingsPage::rewrite_entries_requested);
    add_row(desktop, QString{}, QStringLiteral("Rewrite the entries"),
            QStringLiteral("Regenerates every .desktop file and icon. Use it if a launcher is "
                           "showing a stale name or a missing icon."),
            right_aligned(rewrite));
}

void SettingsPage::refresh_clipboard_note() {
    if (clipboard_note_ == nullptr) {
        return;
    }
    const auto& state = status_.clipboard();
    if (state.limited) {
        clipboard_note_->setText(
            QStringLiteral("This desktop does not let applications follow the clipboard in the "
                           "background, so only copies made while the manager window is "
                           "focused reach Windows."));
    } else if (state.enabled && !state.working) {
        clipboard_note_->setText(QStringLiteral(
            "The clipboard connection to this desktop is down; it reconnects on its own."));
    } else {
        clipboard_note_->setText(
            QStringLiteral("Text and images copied on this machine paste into Windows "
                           "applications and the desktop view, and the other way round."));
    }
}

void SettingsPage::build_logging() {
    using Choices = SettingControls::Choices;
    const Choices levels = {
        {QStringLiteral("Trace"), QStringLiteral("trace")},
        {QStringLiteral("Debug"), QStringLiteral("debug")},
        {QStringLiteral("Info"), QStringLiteral("info")},
        {QStringLiteral("Warn"), QStringLiteral("warn")},
        {QStringLiteral("Error"), QStringLiteral("error")},
    };
    auto* logs = new_group(QStringLiteral("Logging"));
    add_row(logs, K::kLogManager, QStringLiteral("Manager"), QStringLiteral("How much the manager says."),
            controls_->choice(K::kLogManager, levels));
    add_row(logs, QString{}, QStringLiteral("Window mirrors"),
            QStringLiteral("Applies to mirrors started from now on."),
            controls_->choice(K::kLogMirrors, levels));
    add_row(logs, K::kLogAgent, QStringLiteral("Windows agent"),
            QStringLiteral("How much the agent in Windows says."),
            controls_->choice(K::kLogAgent, levels));
    add_row(logs, QString{}, QStringLiteral("Window mirrors in Windows"),
            QStringLiteral("Applies to mirrors started from now on."),
            controls_->choice(K::kLogGuestMirrors, levels));
    add_row(logs, QString{}, QStringLiteral("GPU"),
            QStringLiteral("The GPU device, DXVK and vkd3d-proton. Applies the next time the "
                           "guest starts."),
            controls_->choice(K::kLogGpu, levels));

    auto* debug = new QWidget;
    auto* debug_layout = new QHBoxLayout(debug);
    debug_layout->setContentsMargins(0, 0, 0, 0);
    debug_layout->setSpacing(8);
    debug_button_ = new QPushButton;
    debug_stop_ = new QPushButton(QStringLiteral("Stop"));
    debug_layout->addWidget(debug_button_, 1);
    debug_layout->addWidget(debug_stop_);
    connect(debug_button_, &QPushButton::clicked, this, [this] { verbosity_.start_debug(); });
    connect(debug_stop_, &QPushButton::clicked, this, [this] { verbosity_.stop_debug(); });
    add_row(logs, QString{}, QStringLiteral("Debug logging"),
            QStringLiteral("Every part at debug for %1 minutes, then back to the levels above.")
                .arg(config::LogVerbosity::kDebugMinutes),
            debug);
}

void SettingsPage::refresh_debug_row() {
    const qint64 left_ms = verbosity_.debug_until_ms() - QDateTime::currentMSecsSinceEpoch();
    const bool active = left_ms > 0;
    if (active) {
        const qint64 minutes = (left_ms + 59999) / 60000;
        debug_button_->setText(QStringLiteral("Debug on, %1 min left").arg(minutes));
    } else {
        debug_button_->setText(QStringLiteral("Debug logging for %1 minutes")
                                   .arg(config::LogVerbosity::kDebugMinutes));
    }
    debug_button_->setEnabled(!active);
    debug_stop_->setVisible(active);
}

}
