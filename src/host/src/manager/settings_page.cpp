#include "lsw/host/manager/settings_page.hpp"

#include "lsw/host/config/log_verbosity.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/vm_sizing.hpp"
#include "lsw/host/manager/manager_exit.hpp"
#include "lsw/host/manager/setting_controls.hpp"
#include "lsw/host/platform/host_memory.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/info_tip.hpp"
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

SettingsPage::Row SettingsPage::add_row(QVBoxLayout* group, const QString& key,
                                        const QString& label, const QString& about,
                                        QWidget* control) {
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(18);

    auto* text = new QVBoxLayout;
    text->setSpacing(3);
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    head->addWidget(new QLabel(label));
    Row out;
    out.about = new ui::InfoTip(about);
    head->addWidget(out.about);
    if (!key.isEmpty()) {
        head->addWidget(Widgets::tag(config::ManagerSettings::needs_restart(key)));
    }
    head->addStretch(1);
    text->addLayout(head);

    out.note = Widgets::prose(QString{});
    out.note->setProperty(Theme::Tone::kProperty, Theme::Tone::kWarm);
    out.note->hide();
    text->addWidget(out.note);

    layout->addLayout(text, 1);
    control->setMinimumWidth(kControlLeast);
    control->setMaximumWidth(kControlWidth);
    control->setSizePolicy(QSizePolicy::Preferred, control->sizePolicy().verticalPolicy());
    layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);

    group->addWidget(row);
    group->addWidget(Widgets::separator());
    return out;
}

void SettingsPage::build_machine() {
    auto* machine = new_group(QStringLiteral("Machine"));
    auto* cores = controls_->slider(K::kCpuCores, ui::MachineLimits::kCores, QString{});
    cores->set_maximum(config::VmSizing::host_cores());
    add_row(machine, K::kCpuCores, QStringLiteral("Processor cores"),
            QStringLiteral("How many processor cores Windows gets. This computer has %1 threads.")
                .arg(config::VmSizing::host_cores()),
            cores);

    priority_row_ = add_row(
        machine, K::kPriority, QStringLiteral("Priority"), QString{},
        controls_->choice(K::kPriority, {
                                            {QStringLiteral("Normal"), QStringLiteral("normal")},
                                            {QStringLiteral("High"), QStringLiteral("high")},
                                            {QStringLiteral("Highest"), QStringLiteral("highest")},
                                        }));
    connect(&status_, &services::StatusHub::changed, this, [this](services::StatusHub::Area area) {
        if (area == services::StatusHub::Area::Host) {
            refresh_priority_note();
        }
    });
    refresh_priority_note();

    const auto host_mib = platform::HostMemory::total_mib();
    add_row(
        machine, K::kMemoryMib, QStringLiteral("Memory"),
        QStringLiteral("The most memory Windows has. While it runs, Windows gives back what it "
                       "does not use.%1")
            .arg(host_mib ? QStringLiteral(" This computer has %1 GiB.").arg(*host_mib / kMibPerGib)
                          : QString{}),
        controls_->slider(K::kMemoryMib, ui::MachineLimits::kMemoryMib, QStringLiteral(" MiB")));

    auto* disk = controls_->slider(K::kDiskGib, ui::MachineLimits::kDiskGib, QStringLiteral(" GiB"));
    add_row(
        machine, K::kDiskGib, QStringLiteral("Disk"),
        QStringLiteral("The size of Windows's disk. It can only grow; Windows extends its drive "
                       "the next time it starts."),
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

QString SettingsPage::priority_about(const services::PriorityStatus& status) {
    using Level = services::PriorityStatus::Level;
    switch (status.wanted) {
    case Level::High:
        return QStringLiteral("When the processors are busy the guest and its windows get time "
                              "ahead of other programs.");
    case Level::Highest:
        return QStringLiteral("As High, and the threads that carry the picture run ahead of "
                              "ordinary threads.");
    case Level::Normal:
        break;
    }
    return QStringLiteral("The guest shares the processors with other programs like any program.");
}

QString SettingsPage::priority_note(const services::PriorityStatus& status) {
    using Level = services::PriorityStatus::Level;
    QStringList parts;
    if (status.wanted == Level::Normal) {
        if (status.weight_raised) {
            parts << QStringLiteral("The larger share the guest had could not be taken back; it "
                                    "ends when the guest stops.");
        }
        return parts.join(QLatin1Char(' '));
    }
    if (!status.vm_running) {
        return QStringLiteral("In effect once the guest runs.");
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
    if (priority_row_.note == nullptr) {
        return;
    }
    priority_row_.about->set_text(priority_about(status_.priority()));
    const QString note = priority_note(status_.priority());
    priority_row_.note->setText(note);
    priority_row_.note->setVisible(!note.isEmpty());
}

void SettingsPage::build_startup() {
    auto* startup = new_group(QStringLiteral("Startup"));
    add_row(startup, K::kAutostart, QStringLiteral("Start the guest when the manager opens"),
            QStringLiteral("Starts Windows as soon as the manager opens. Otherwise it starts the "
                           "first time you open a Windows application."),
            right_aligned(controls_->toggle(K::kAutostart)));
    add_row(startup, K::kCloseToTray, QStringLiteral("Keep running in the tray when closed"),
            QStringLiteral("Closing the window keeps the manager and Windows running, with an icon "
                           "in the tray. When off, closing the window quits the manager."),
            right_aligned(controls_->toggle(K::kCloseToTray)));
    add_row(
        startup, K::kShutGuestOnQuit, QStringLiteral("Shut the guest down when the manager quits"),
        QStringLiteral("Quitting the manager shuts Windows down too, waiting up to %1 seconds. "
                       "When off, Windows keeps running after the manager quits.")
            .arg(std::chrono::duration_cast<std::chrono::seconds>(ManagerExit::kGuestWait).count()),
        right_aligned(controls_->toggle(K::kShutGuestOnQuit)));
    add_row(startup, K::kCheckUpdates, QStringLiteral("Check for updates on startup"),
            QStringLiteral("Each time the manager opens, it asks github.com for LSW's releases and "
                           "tells you when a newer LSW is out. When off, it only checks when you "
                           "click Check for updates on the Maintenance page."),
            right_aligned(controls_->toggle(K::kCheckUpdates)));
}

void SettingsPage::build_desktop() {
    auto* desktop = new_group(QStringLiteral("Desktop integration"));
    add_row(desktop, K::kPublishEntries, QStringLiteral("Put guest applications in your menu"),
            QStringLiteral("Adds each Windows application to your application menu, with its own "
                           "icon."),
            right_aligned(controls_->toggle(K::kPublishEntries)));
    add_row(desktop, K::kMenuCategory, QStringLiteral("Menu category"),
            QStringLiteral("The submenu Windows applications appear under in your application "
                           "menu."),
            controls_->text(K::kMenuCategory));
    add_row(desktop, K::kFileAssociations,
            QStringLiteral("Open Windows file types with these applications"),
            QStringLiteral("Files no application on this computer opens are opened by the Windows "
                           "application that handles them. For other file types, use Make it the "
                           "default in that application's settings."),
            right_aligned(controls_->toggle(K::kFileAssociations)));
    clipboard_row_ =
        add_row(desktop, K::kShareClipboard, QStringLiteral("Share the clipboard"),
                QStringLiteral("Text and images copied on this computer paste into Windows "
                               "applications, and the other way round."),
                right_aligned(controls_->toggle(K::kShareClipboard)));
    connect(&status_, &services::StatusHub::changed, this, [this](services::StatusHub::Area area) {
        if (area == services::StatusHub::Area::Host) {
            refresh_clipboard_note();
        }
    });
    refresh_clipboard_note();
    auto* rewrite = Widgets::button(QStringLiteral("Rewrite now"));
    connect(rewrite, &QPushButton::clicked, this, &SettingsPage::rewrite_entries_requested);
    add_row(desktop, QString{}, QStringLiteral("Rewrite the entries"),
            QStringLiteral("Recreates every menu entry and icon. Use it when a menu entry shows an "
                           "old name or a missing icon."),
            right_aligned(rewrite));
}

void SettingsPage::refresh_clipboard_note() {
    if (clipboard_row_.note == nullptr) {
        return;
    }
    const auto& state = status_.clipboard();
    QString note;
    if (state.limited) {
        note = QStringLiteral("This desktop does not let applications follow the clipboard in the "
                              "background, so only copies made while the manager window is "
                              "focused reach Windows.");
    } else if (state.enabled && !state.working) {
        note = QStringLiteral("The clipboard connection to this desktop is down; it reconnects on "
                              "its own.");
    }
    clipboard_row_.note->setText(note);
    clipboard_row_.note->setVisible(!note.isEmpty());
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
    add_row(logs, K::kLogManager, QStringLiteral("Manager"),
            QStringLiteral("How much detail the manager writes to its log."),
            controls_->choice(K::kLogManager, levels));
    add_row(logs, QString{}, QStringLiteral("Window mirrors"),
            QStringLiteral("How much detail the Windows application windows on this desktop log. "
                           "Applies to windows opened from now on."),
            controls_->choice(K::kLogMirrors, levels));
    add_row(logs, K::kLogAgent, QStringLiteral("Windows agent"),
            QStringLiteral("How much detail LSW's service inside Windows logs."),
            controls_->choice(K::kLogAgent, levels));
    add_row(logs, QString{}, QStringLiteral("Window mirrors in Windows"),
            QStringLiteral("How much detail the Windows side of each application window logs. "
                           "Applies to windows opened from now on."),
            controls_->choice(K::kLogGuestMirrors, levels));
    add_row(logs, QString{}, QStringLiteral("GPU"),
            QStringLiteral("How much detail the virtual GPU and the Direct3D translation (DXVK, "
                           "vkd3d-proton) log. Applies the next time Windows starts."),
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
    add_row(
        logs, QString{}, QStringLiteral("Debug logging"),
        QStringLiteral("Logs everything in full detail for %1 minutes, then returns to the levels "
                       "above. Use it before reporting a problem.")
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
