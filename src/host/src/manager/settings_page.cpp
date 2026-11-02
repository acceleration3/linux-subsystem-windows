#include "lsw/host/manager/settings_page.hpp"

#include "lsw/host/config/display_refresh.hpp"
#include "lsw/host/config/log_verbosity.hpp"
#include "lsw/host/manager/adapter_roles.hpp"
#include "lsw/host/manager/resolution_editor.hpp"
#include "lsw/host/manager/usb_devices_panel.hpp"
#include "lsw/host/ui/machine_limits.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/config/vm_sizing.hpp"
#include "lsw/host/services/guest_update_status.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/slider_spin.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/toggle_switch.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <algorithm>
#include <functional>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;
using config::HostShare;
using K = config::ManagerSettings::Key;

using Choices = QVector<QPair<QString, QVariant>>;

QLabel* tag(bool restart) {
    auto* label = Widgets::meta(restart ? QStringLiteral("RESTART") : QStringLiteral("NOW"));
    label->setProperty(Theme::Tone::kProperty,
                       restart ? Theme::Tone::kRestart : Theme::Tone::kNow);
    return label;
}

QPushButton* small_button(const QString& text) {
    auto* button = new QPushButton(text);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QTableWidget* make_table(const QStringList& headers) {
    auto* table = new QTableWidget(0, static_cast<int>(headers.size()));
    QStringList upper;
    upper.reserve(headers.size());
    for (const auto& header : headers) {
        upper << header.toUpper();
    }
    table->setHorizontalHeaderLabels(upper);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->verticalHeader()->setVisible(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFocusPolicy(Qt::NoFocus);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < headers.size(); ++i) {
        table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    table->setMinimumHeight(96);
    return table;
}

}

SettingsPage::SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                           apps::AppConfigStore& apps, services::StatusHub& status,
                           services::UsbPassthrough& usb, config::LogVerbosity& verbosity,
                           QWidget* parent)
    : QWidget(parent), settings_(settings), pending_(pending), apps_(apps), status_(status),
      usb_(usb), verbosity_(verbosity) {
    build();
    debug_clock_.setInterval(15000);
    connect(&debug_clock_, &QTimer::timeout, this, &SettingsPage::refresh_debug_row);
    connect(&verbosity_, &config::LogVerbosity::changed, this, &SettingsPage::refresh_debug_row);
    refresh_debug_row();
    connect(&pending_, &config::PendingChanges::changed, this,
            &SettingsPage::refresh_apply_bar);
    connect(&pending_, &config::PendingChanges::changed, this,
            &SettingsPage::rebuild_shares_table, Qt::QueuedConnection);
    connect(&pending_, &config::PendingChanges::changed, this, [this] {
        for (auto it = shows_.constBegin(); it != shows_.constEnd(); ++it) {
            it.value()();
        }
    });
    connect(&settings_, &config::ManagerSettings::changed, this, &SettingsPage::show_current);
    refresh_apply_bar();
}

void SettingsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    status_.set_detail(services::StatusHub::Area::Usb, true);
    refresh_debug_row();
    debug_clock_.start();
}

void SettingsPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    status_.set_detail(services::StatusHub::Area::Usb, false);
    debug_clock_.stop();
}

void SettingsPage::follow(const QString& key, std::function<void()> show) {
    shows_.insert(key, std::move(show));
}

void SettingsPage::show_current(const QString& key) {
    if (const auto it = shows_.constFind(key); it != shows_.constEnd()) {
        it.value()();
    }
}

void SettingsPage::build() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget;
    content_ = new QVBoxLayout(holder);
    content_->setContentsMargins(14, 14, 14, 14);
    content_->setSpacing(14);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);

    auto* machine = new_group(QStringLiteral("Machine"));
    add_row(machine, K::kCpuCores, QStringLiteral("Processor cores"),
            QStringLiteral("One socket, cores only, no SMT."),
            [this] {
                auto* cores = static_cast<ui::SliderSpin*>(
                    make_slider(K::kCpuCores, ui::MachineLimits::kCores, QString{}));
                cores->set_maximum(config::VmSizing::host_cores());
                return cores;
            }());
    add_row(machine, K::kMemoryMib, QStringLiteral("Memory"),
            QStringLiteral("Fixed: the guest holds all of it while it runs."),
            make_slider(K::kMemoryMib, ui::MachineLimits::kMemoryMib, QStringLiteral(" MiB")));
    auto* disk = static_cast<ui::SliderSpin*>(
        make_slider(K::kDiskGib, ui::MachineLimits::kDiskGib, QStringLiteral(" GiB")));
    add_row(machine, K::kDiskGib, QStringLiteral("Disk"),
            QStringLiteral("Grows only: the image is grown when the guest restarts, and Windows "
                           "extends C: into the new space as it starts."),
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
    connect(&status_, &services::StatusHub::changed, this,
            [follow_disk](services::StatusHub::Area area) {
                if (area == services::StatusHub::Area::Vm) {
                    follow_disk();
                }
            });
    follow_disk();

    auto* display = new_group(QStringLiteral("Display"));
    auto* resolutions = new ResolutionEditor(settings_);
    add_wide_row(display, false, QStringLiteral("Resolutions"),
                 QStringLiteral("The sizes the guest's monitor offers, as Windows lists them in "
                                "Settings → Display. Windows uses the preferred one. Any size "
                                "works, from 100 × 100 up to 4095 on an edge; a change reaches a "
                                "running guest at once, as if a monitor were replugged. "
                                "Application windows are mirrored individually, so the preferred "
                                "size is the ceiling a maximised window can reach."),
                 resolutions);
    {
        auto* host_modes = new ui::ToggleSwitch;
        host_modes->setChecked(settings_.string_list_value(K::kDisplayModes).isEmpty());
        connect(host_modes, &ui::ToggleSwitch::toggled, resolutions,
                &ResolutionEditor::use_host_modes);
        connect(&settings_, &config::ManagerSettings::changed, host_modes,
                [this, host_modes](const QString& key) {
                    if (key == K::kDisplayModes) {
                        const QSignalBlocker quiet(host_modes);
                        host_modes->setChecked(
                            settings_.string_list_value(K::kDisplayModes).isEmpty());
                    }
                });
        auto* toggle_box = new QWidget;
        auto* row = new QHBoxLayout(toggle_box);
        row->setContentsMargins(0, 0, 0, 0);
        row->addStretch(1);
        row->addWidget(host_modes);
        add_row(display, K::kDisplayModes,
                QStringLiteral("Offer the host monitors' own modes instead"),
                QStringLiteral("Leaves the list empty, which is accel-virt's default: the guest's "
                               "monitor offers the modes of the monitors connected to this "
                               "machine, the largest one's best mode preferred."),
                toggle_box);
    }
    add_row(display, K::kMatchHostOutput,
            QStringLiteral("Match the monitor the manager is on"),
            QStringLiteral("Puts the size of the monitor this window is on first, preferred, "
                           "ahead of the list above, and follows when the window moves to "
                           "another one."),
            make_toggle(K::kMatchHostOutput));
    add_refresh_row(display);
    add_row(display, K::kScalingPercent, QStringLiteral("Scaling"),
            QStringLiteral("Windows' display scale, changed while Windows runs. Matching this "
                           "desktop takes the scale of the monitor the manager is on, as the "
                           "nearest step Windows has (1.2 is 125 %); mirrored windows stay one "
                           "guest pixel to one screen pixel."),
            make_choice(K::kScalingPercent,
                        Choices{{QStringLiteral("Match this desktop"), 0},
                                {QStringLiteral("100 % (96 DPI)"), 100},
                                {QStringLiteral("125 % (120 DPI)"), 125},
                                {QStringLiteral("150 % (144 DPI)"), 150},
                                {QStringLiteral("200 % (192 DPI)"), 200}}));

    auto* graphics = new_group(QStringLiteral("Graphics"));
    add_wide_row(graphics, true, QStringLiteral("Adapters"),
                 QStringLiteral("Each host GPU can be the guest's display adapter, a render-only "
                                "adapter with no outputs, or left out. Windows names every "
                                "adapter after its GPU and reports its VRAM. Exactly one is the "
                                "display; until you pick one, it is the first Vulkan device."),
                 new AdapterRoles(pending_, status_));
    add_row(graphics, K::kHostWindowMib, QStringLiteral("Host window"),
            QStringLiteral("Host memory the guest maps write-back, for uploads, readbacks and "
                           "host-visible Vulkan memory. Used on demand; 8 GiB runs real "
                           "applications."),
            make_choice(K::kHostWindowMib,
                        Choices{{QStringLiteral("2 GiB"), 2048},
                                {QStringLiteral("4 GiB"), 4096},
                                {QStringLiteral("8 GiB"), 8192},
                                {QStringLiteral("16 GiB"), 16384}}));

    auto* files = new_group(QStringLiteral("Files"));
    add_row(files, K::kSmbEnabled,
            QStringLiteral("Browse the guest from your file manager"),
            QStringLiteral("Serves the guest over SMB on 127.0.0.1:5445, handled in "
                           "userspace, so nothing is mounted and Windows junctions never "
                           "turn into extra mounts."),
            make_toggle(K::kSmbEnabled));
    add_row(files, K::kShareScope, QStringLiteral("Shared folder"),
            QStringLiteral("What the bookmark opens."),
            make_choice(K::kShareScope,
                        Choices{{QStringLiteral("Whole disk (C$)"), QStringLiteral("C$")},
                                {QStringLiteral("The user's profile"),
                                 QStringLiteral("profile")}}));
    add_row(files, K::kBookmarkDolphin,
            QStringLiteral("Bookmark in Dolphin"),
            QStringLiteral("Writes a Places entry titled Windows into user-places.xbel."),
            make_toggle(K::kBookmarkDolphin));
    add_row(files, K::kBookmarkGtk,
            QStringLiteral("Bookmark in GNOME Files and Thunar"),
            QStringLiteral("The same entry, in gtk-3.0/bookmarks."),
            make_toggle(K::kBookmarkGtk));

    auto* shares_row = new QWidget;
    auto* shares_layout = new QVBoxLayout(shares_row);
    shares_layout->setContentsMargins(12, 10, 12, 12);
    shares_layout->setSpacing(8);
    auto* shares_head = new QHBoxLayout;
    shares_head->addWidget(Widgets::caption(QStringLiteral("Host folders the guest can see")));
    shares_head->addWidget(tag(true));
    shares_head->addStretch(1);
    auto* add_share = small_button(QStringLiteral("Add a folder"));
    shares_head->addWidget(add_share);
    shares_layout->addLayout(shares_head);
    shares_layout->addWidget(Widgets::meta(
        QStringLiteral("Each one becomes a virtiofs device and a drive letter in Windows, named "
                       "after the folder and mounted "
                       "with WinFsp - Windows File System Proxy, Copyright (C) Bill "
                       "Zissimopoulos (github.com/winfsp/winfsp)."),
        true));
    shares_table_ = make_table({QStringLiteral("Folder on this machine"),
                                QStringLiteral("Drive"), QStringLiteral("Access"),
                                QString{}});
    shares_layout->addWidget(shares_table_);
    files->addWidget(shares_row);
    rebuild_shares_table();

    connect(add_share, &QPushButton::clicked, this, [this] {
        const QString dir = QDir::cleanPath(QFileDialog::getExistingDirectory(
            this, QStringLiteral("Share a folder with the guest"), QDir::homePath()));
        if (dir.isEmpty()) {
            return;
        }
        auto shares = staged_shares();
        const auto letters = config::ManagerSettings::free_drive_letters(shares);
        if (letters.isEmpty()) {
            return;
        }
        shares.push_back(HostShare{dir, letters.first(), false});
        stage_shares(shares);
    });

    add_action_row(files, QStringLiteral("Open the share"),
                   QStringLiteral("Launches your file manager at the guest's C: drive."),
                   QStringLiteral("Open Windows files"), &SettingsPage::open_share_requested);

    auto* entries = new_group(QStringLiteral("Desktop entries"));
    add_row(entries, K::kPublishEntries,
            QStringLiteral("Put guest applications in your menu"),
            QStringLiteral("One .desktop file per application, with the icon pulled out "
                           "of the executable."),
            make_toggle(K::kPublishEntries));
    add_row(entries, K::kMenuCategory, QStringLiteral("Menu category"),
            QStringLiteral("The submenu they appear under."),
            make_text(K::kMenuCategory));
    add_row(entries, K::kFileAssociations,
            QStringLiteral("Open Windows file types with these applications"),
            QStringLiteral("Windows applications open the file types nothing on this machine "
                           "handles. For the rest, use Make it the default in an application's "
                           "settings."),
            make_toggle(K::kFileAssociations));
    add_action_row(entries, QStringLiteral("Rewrite the entries"),
                   QStringLiteral("Regenerates every .desktop file and icon."),
                   QStringLiteral("Rewrite now"), &SettingsPage::rewrite_entries_requested);

    auto* usb = new_group(QStringLiteral("USB devices"));
    add_wide_row(usb, false, QStringLiteral("Pass USB devices to the guest"),
                 QStringLiteral("A device passed to the guest disappears from this machine until "
                                "you return it."),
                 new UsbDevicesPanel(status_, usb_));

    auto* guest = new_group(QStringLiteral("Guest"));
    add_row(guest, K::kAutostart,
            QStringLiteral("Start the guest when the manager opens"),
            QStringLiteral("Otherwise it starts the first time you launch an application."),
            make_toggle(K::kAutostart));

    auto* host = new_group(QStringLiteral("Host"));
    add_row(host, K::kCloseToTray,
            QStringLiteral("Keep running in the tray when closed"),
            QStringLiteral("Closing the window leaves the guest running."),
            make_toggle(K::kCloseToTray));

    build_logs_group();

    auto* images = new_group(QStringLiteral("Images"));
    add_action_row(images, QStringLiteral("Download a Windows image"),
                   QStringLiteral("Fetch an installation ISO from microsoft.com into any folder."),
                   QStringLiteral("Open the download tool"),
                   &SettingsPage::download_tool_requested);
    add_action_row(images, QStringLiteral("Trim a Windows image"),
                   QStringLiteral("Build a smaller install ISO from one you have, into any "
                                  "folder. Does not touch the running guest."),
                   QStringLiteral("Open the image tool"), &SettingsPage::trim_tool_requested);
    auto* update_button = small_button(QStringLiteral("Update"));
    connect(update_button, &QPushButton::clicked, this, &SettingsPage::update_requested);
    auto* update_row = add_row(images, QString{}, QStringLiteral("Update LSW in Windows"),
                               QString{}, update_button);
    update_button_ = update_button;
    update_note_ = update_row->findChild<QLabel*>(QStringLiteral("rowDescription"));
    update_note_->setToolTip(QStringLiteral(
        "Replaces every LSW file in Windows: the agent, the window mirror, the GPU driver, the "
        "graphics loaders and the shared folders' half. Restarts Windows when the driver "
        "changes."));
    connect(&status_, &services::StatusHub::changed, this, [this](services::StatusHub::Area area) {
        if (area == services::StatusHub::Area::Update || area == services::StatusHub::Area::Vm) {
            refresh_update_row();
        }
    });
    refresh_update_row();
    add_action_row(images, QStringLiteral("Reinstall the guest"),
                   QStringLiteral("Runs the first-run wizard again. Its install replaces the "
                                  "current disk, and applications inside the guest are lost; "
                                  "closing the wizard before then changes nothing."),
                   QStringLiteral("Reinstall"), &SettingsPage::reinstall_requested,
                   Theme::Role::kStop);

    content_->addStretch(1);

    apply_bar_ = new QWidget;
    Theme::surface(apply_bar_, Theme::Role::kApplyBar);
    auto* bar = new QHBoxLayout(apply_bar_);
    bar->setContentsMargins(14, 11, 14, 11);
    bar->setSpacing(14);

    apply_count_ = Widgets::caption(QStringLiteral("0 changes need a restart"));
    apply_count_->setProperty(Theme::Tone::kProperty, Theme::Tone::kAmber);
    apply_diffs_ = Widgets::meta(QString{}, true);

    auto* revert = small_button(QStringLiteral("Revert"));
    revert->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
    auto* apply = small_button(QStringLiteral("Apply and restart the guest"));
    apply->setObjectName(QString::fromLatin1(Theme::Role::kPrimary));

    bar->addWidget(apply_count_);
    bar->addWidget(apply_diffs_, 1);
    bar->addWidget(revert);
    bar->addWidget(apply);
    root->addWidget(apply_bar_);

    connect(revert, &QPushButton::clicked, &pending_, &config::PendingChanges::revert);
    connect(apply, &QPushButton::clicked, this, [this] {
        pending_.commit();
        Q_EMIT restart_requested();
    });
}

void SettingsPage::build_logs_group() {
    using Part = config::LogVerbosity::Part;
    const Choices levels = {
        {QStringLiteral("Trace"), QStringLiteral("trace")},
        {QStringLiteral("Debug"), QStringLiteral("debug")},
        {QStringLiteral("Info"), QStringLiteral("info")},
        {QStringLiteral("Warn"), QStringLiteral("warn")},
        {QStringLiteral("Error"), QStringLiteral("error")},
    };
    auto* logs = new_group(QStringLiteral("Logs"));
    add_row(logs, K::kLogManager, QStringLiteral("Manager"),
            QStringLiteral("How much the manager says."), make_choice(K::kLogManager, levels));
    add_row(logs, QString{}, QStringLiteral("Window mirrors"),
            QStringLiteral("Applies to mirrors started from now on."),
            make_choice(K::kLogMirrors, levels));
    add_row(logs, K::kLogAgent, QStringLiteral("Windows agent"),
            QStringLiteral("How much the agent in Windows says."),
            make_choice(K::kLogAgent, levels));
    add_row(logs, QString{}, QStringLiteral("Window mirrors in Windows"),
            QStringLiteral("Applies to mirrors started from now on."),
            make_choice(K::kLogGuestMirrors, levels));
    add_row(logs, QString{}, QStringLiteral("GPU"),
            QStringLiteral("The GPU device, DXVK and vkd3d-proton. Applies the next time the "
                           "guest starts."),
            make_choice(K::kLogGpu, levels));

    auto* debug = new QWidget;
    auto* debug_layout = new QHBoxLayout(debug);
    debug_layout->setContentsMargins(0, 0, 0, 0);
    debug_layout->setSpacing(8);
    debug_button_ = small_button(QString{});
    debug_stop_ = small_button(QStringLiteral("Stop"));
    debug_layout->addWidget(debug_button_, 1);
    debug_layout->addWidget(debug_stop_);
    connect(debug_button_, &QPushButton::clicked, this, [this] { verbosity_.start_debug(); });
    connect(debug_stop_, &QPushButton::clicked, this, [this] { verbosity_.stop_debug(); });
    add_row(logs, QString{}, QStringLiteral("Debug logging"),
            QStringLiteral("Every part at debug for %1 minutes, then back to the levels above.")
                .arg(config::LogVerbosity::kDebugMinutes),
            debug);

    add_action_row(logs, QStringLiteral("Log folder"), QStringLiteral("Where every log is kept."),
                   QStringLiteral("Open log folder"), &SettingsPage::open_log_folder_requested);
    add_action_row(logs, QStringLiteral("Collect logs for a bug report"),
                   QStringLiteral("Packs the logs, Windows' logs and system details into one "
                                  "archive. Window titles and file names appear in logs; read it "
                                  "before you share it."),
                   QStringLiteral("Collect logs"), &SettingsPage::collect_logs_requested);
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

void SettingsPage::refresh_update_row() {
    using State = services::GuestUpdateStatus::State;
    const auto& update = status_.update();
    const auto brief = [](const QString& release) {
        return release.isEmpty() ? QStringLiteral("-") : release.left(12);
    };
    QString note;
    switch (update.state) {
    case State::Running:
        note = QStringLiteral("%1...").arg(update.step.isEmpty() ? QStringLiteral("Updating")
                                                                   : update.step);
        break;
    case State::Failed:
        note = QStringLiteral("Could not update: %1").arg(update.message);
        break;
    case State::Idle:
    case State::Succeeded:
        note = !update.guest_release.isEmpty() && update.guest_release == update.shipped_release
                   ? QStringLiteral("Windows has this release (%1)").arg(brief(update.guest_release))
                   : QStringLiteral("Windows has %1, this LSW ships %2")
                         .arg(brief(update.guest_release), brief(update.shipped_release));
        break;
    }
    update_note_->setText(note);
    const auto& vm = status_.vm();
    update_button_->setEnabled(!update.running() && vm.running() && vm.agent_online());
}

QVBoxLayout* SettingsPage::new_group(const QString& title) {
    const auto group = ui::Panel::make(title);
    group.body->setContentsMargins(0, 0, 0, 0);
    group.body->setSpacing(0);
    content_->addWidget(group.frame);
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
    auto* name = new QLabel(label);
    head->addWidget(name);
    if (!key.isEmpty()) {
        head->addWidget(tag(config::ManagerSettings::needs_restart(key)));
    }
    head->addStretch(1);
    text->addLayout(head);

    auto* prose = Widgets::prose(description);
    prose->setObjectName(QStringLiteral("rowDescription"));
    text->addWidget(prose);

    layout->addLayout(text, 1);
    control->setFixedWidth(300);
    layout->addWidget(control, 0, Qt::AlignRight);

    group->addWidget(row);

    group->addWidget(Widgets::separator());
    return row;
}

void SettingsPage::add_wide_row(QVBoxLayout* group, bool restart, const QString& label,
                                const QString& description, QWidget* control) {
    auto* row = new QWidget;
    auto* layout = new QVBoxLayout(row);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(3);
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    head->addWidget(new QLabel(label));
    head->addWidget(tag(restart));
    head->addStretch(1);
    layout->addLayout(head);
    layout->addWidget(Widgets::prose(description));
    layout->addSpacing(7);
    layout->addWidget(control);
    group->addWidget(row);
    group->addWidget(Widgets::separator());
}

void SettingsPage::add_action_row(QVBoxLayout* group, const QString& label,
                                  const QString& description, const QString& button_text,
                                  void (SettingsPage::*clicked)(), const char* role) {
    auto* button = small_button(button_text);
    if (role != nullptr) {
        button->setObjectName(QString::fromLatin1(role));
    }
    connect(button, &QPushButton::clicked, this, clicked);
    add_row(group, QString{}, label, description, button);
}

QWidget* SettingsPage::make_slider(const QString& key, const ui::MachineLimits::Range& range,
                                   const QString& suffix) {
    auto* slider = new ui::SliderSpin(range.min, range.max, range.step, suffix);
    slider->row()->setSpacing(12);
    slider->spin()->setFixedWidth(96);
    slider->spin()->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    slider->spin()->setObjectName(QStringLiteral("SliderValue"));
    slider->set_value(pending_.staged_or_current(key).toInt());
    connect(slider, &ui::SliderSpin::value_changed, this,
            [this, key](int value) { put(key, value); });
    follow(key, [this, key, slider] {
        const QSignalBlocker quiet(slider);
        slider->set_value(pending_.staged_or_current(key).toInt());
    });
    return slider;
}


QString SettingsPage::refresh_description(const QVector<platform::RateChoice>& rates) {
    QString text = QStringLiteral(
        "Windows' refresh rate, changed while Windows runs: it switches to the new rate at once, "
        "without a restart. Same as monitor follows the screen the manager is on. A higher rate "
        "means more GPU work on the host.");
    QStringList all;
    for (const auto& rate : rates) {
        for (const QString& monitor : rate.monitors) {
            if (!all.contains(monitor)) {
                all << monitor;
            }
        }
    }
    bool differ = false;
    for (const auto& rate : rates) {
        differ = differ || rate.monitors.size() != all.size();
    }
    if (differ) {
        QStringList parts;
        for (const auto& rate : rates) {
            parts << QStringLiteral("%1 on %2")
                         .arg(config::DisplayRefresh::label(static_cast<quint32>(rate.millihz)),
                              rate.monitors.join(QStringLiteral(", ")));
        }
        text += QStringLiteral(" Supported by: %1.").arg(parts.join(QStringLiteral("; ")));
    }
    return text;
}

void SettingsPage::add_refresh_row(QVBoxLayout* group) {
    using config::DisplayRefresh;
    auto* combo = new QComboBox;
    auto* row = add_row(group, K::kDisplayRefresh, QStringLiteral("Refresh rate"), QString{}, combo);
    auto* description = row->findChild<QLabel*>(QStringLiteral("rowDescription"));

    const auto show = [this, combo, description] {
        const auto& graphics = status_.graphics();
        const std::optional<quint32> stored = DisplayRefresh::fixed(settings_.string_value(K::kDisplayRefresh));
        QVector<QPair<QString, QString>> wanted;
        wanted.push_back({QStringLiteral("Same as monitor (%1)")
                              .arg(DisplayRefresh::label(graphics.monitor_refresh_millihz)),
                          QString::fromLatin1(DisplayRefresh::kMonitor)});
        QVector<quint32> rates;
        for (const auto& choice : graphics.monitor_rates) {
            rates.push_back(static_cast<quint32>(choice.millihz));
        }
        if (stored && !rates.contains(*stored)) {
            rates.push_back(*stored);
            std::sort(rates.begin(), rates.end(), std::greater<>());
        }
        for (const quint32 millihz : rates) {
            wanted.push_back({DisplayRefresh::label(millihz), DisplayRefresh::text(millihz)});
        }
        bool same = combo->count() == wanted.size();
        for (qsizetype i = 0; same && i < wanted.size(); ++i) {
            same = combo->itemText(static_cast<int>(i)) == wanted.at(i).first &&
                   combo->itemData(static_cast<int>(i)).toString() == wanted.at(i).second;
        }
        const QSignalBlocker quiet(combo);
        if (!same) {
            combo->clear();
            for (const auto& item : wanted) {
                combo->addItem(item.first, item.second);
            }
        }
        const int at = stored ? combo->findData(DisplayRefresh::text(*stored)) : 0;
        combo->setCurrentIndex(at >= 0 ? at : 0);
        combo->setToolTip(combo->currentText());
        combo->view()->setMinimumWidth(combo->sizeHint().width());
        if (description != nullptr) {
            description->setText(refresh_description(graphics.monitor_rates));
        }
    };
    connect(combo, &QComboBox::activated, this, [this, combo](int index) {
        put(K::kDisplayRefresh, combo->itemData(index));
    });
    connect(&status_, &services::StatusHub::changed, combo,
            [show](services::StatusHub::Area area) {
                if (area == services::StatusHub::Area::Graphics) {
                    show();
                }
            });
    follow(K::kDisplayRefresh, show);
    show();
}

QWidget* SettingsPage::make_choice(const QString& key, const Choices& options) {
    auto* combo = new QComboBox;
    for (const auto& option : options) {
        combo->addItem(option.first, option.second);
    }
    const QVariant current = pending_.staged_or_current(key);
    const int index = combo->findData(current);
    combo->setCurrentIndex(index >= 0 ? index : 0);
    combo->view()->setMinimumWidth(combo->sizeHint().width());
    combo->setToolTip(combo->currentText());
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key](int i) {
        combo->setToolTip(combo->itemText(i));
        put(key, combo->itemData(i));
    });
    follow(key, [this, key, combo] {
        const QSignalBlocker quiet(combo);
        const int at = combo->findData(pending_.staged_or_current(key));
        combo->setCurrentIndex(at >= 0 ? at : 0);
        combo->setToolTip(combo->currentText());
    });
    return combo;
}

QWidget* SettingsPage::make_toggle(const QString& key) {
    auto* box = new ui::ToggleSwitch;
    box->setChecked(pending_.staged_or_current(key).toBool());
    connect(box, &ui::ToggleSwitch::toggled, this, [this, key](bool on) { put(key, on); });
    follow(key, [this, key, box] {
        const QSignalBlocker quiet(box);
        box->setChecked(pending_.staged_or_current(key).toBool());
    });

    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(box);
    return row;
}

QWidget* SettingsPage::make_text(const QString& key) {
    auto* edit = new QLineEdit(pending_.staged_or_current(key).toString());
    connect(edit, &QLineEdit::editingFinished, this,
            [this, key, edit] { put(key, edit->text()); });
    follow(key, [this, key, edit] {
        if (!edit->hasFocus()) {
            edit->setText(pending_.staged_or_current(key).toString());
        }
    });
    return edit;
}

void SettingsPage::put(const QString& key, const QVariant& value) {
    if (config::ManagerSettings::needs_restart(key)) {
        pending_.stage(key, value);
        return;
    }
    settings_.set_value(key, value);
}

QVector<HostShare> SettingsPage::staged_shares() const {
    return config::ManagerSettings::decode_host_shares(
        pending_.staged_or_current(K::kHostShares).toString());
}

void SettingsPage::stage_shares(const QVector<HostShare>& shares) {
    put(K::kHostShares, config::ManagerSettings::encode_host_shares(shares));
}

void SettingsPage::rebuild_shares_table() {
    const auto shares = staged_shares();
    shares_table_->setRowCount(static_cast<int>(shares.size()));
    for (int row = 0; row < shares.size(); ++row) {
        const auto& share = shares.at(row);
        shares_table_->setItem(row, 0, new QTableWidgetItem(share.host_path));
        shares_table_->setItem(row, 1, new QTableWidgetItem(share.drive));
        shares_table_->setItem(
            row, 2,
            new QTableWidgetItem(share.read_only ? QStringLiteral("Read only")
                                                 : QStringLiteral("Read write")));
        auto* remove = small_button(QStringLiteral("✕"));
        remove->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
        connect(remove, &QPushButton::clicked, this, [this, path = share.host_path] {
            auto remaining = staged_shares();
            remaining.removeIf([&path](const HostShare& s) { return s.host_path == path; });
            stage_shares(remaining);
        });
        shares_table_->setCellWidget(row, 3, remove);
    }
}

void SettingsPage::refresh_apply_bar() {
    const int count = pending_.count();
    apply_bar_->setVisible(count > 0);
    apply_count_->setText(count == 1 ? QStringLiteral("1 change needs a restart")
                                     : QStringLiteral("%1 changes need a restart").arg(count));
    apply_diffs_->setText(pending_.summary().join(QStringLiteral("   ")));
}

}
