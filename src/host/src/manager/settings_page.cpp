#include "lsw/host/manager/settings_page.hpp"

#include "lsw/host/vm/render_server.hpp"

#include "lsw/host/ui/machine_limits.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/slider_spin.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/toggle_switch.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
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
using config::ChannelBinding;
using config::ChannelBusyPolicy;
using config::HostShare;
using K = config::ManagerSettings::Key;

using Choices = QVector<QPair<QString, QVariant>>;

// The now-or-restart tag. Amber for restart because it is the one that
// costs something; green for now because it is already done.
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

// Drive letters no share in ``shares`` has taken, backwards from Z,
// which is where people put a mapped drive, and stopping before the
// letters Windows has already spoken for. The same rule as
// ManagerSettings::free_drive_letters, applied to the staged list:
// that one reads what is stored, and two folders added before an
// Apply would otherwise both be offered Z:.
QStringList free_drive_letters(const QVector<HostShare>& shares) {
    QStringList taken;
    for (const auto& share : shares) {
        taken << share.drive.toUpper();
    }
    QStringList out;
    for (char letter = 'Z'; letter >= 'E'; --letter) {
        const QString candidate = QString(QLatin1Char(letter)) + QLatin1Char(':');
        if (!taken.contains(candidate)) {
            out << candidate;
        }
    }
    return out;
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

}  // namespace

SettingsPage::SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                           apps::AppConfigStore& apps, QWidget* parent)
    : QWidget(parent), settings_(settings), pending_(pending), apps_(apps) {
    build();
    connect(&pending_, &config::PendingChanges::changed, this,
            &SettingsPage::refresh_apply_bar);
    connect(&apps_, &apps::AppConfigStore::changed, this,
            &SettingsPage::rebuild_bindings_table);
    // The shares table shows the staged list, so whatever changes what
    // is staged (an add, a remove, Revert, Apply) redraws it. Queued,
    // because a remove is clicked from inside the table it rebuilds.
    connect(&pending_, &config::PendingChanges::changed, this,
            &SettingsPage::rebuild_shares_table, Qt::QueuedConnection);
    refresh_apply_bar();
}

void SettingsPage::build() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // The list scrolls; the apply bar stays pinned under it.
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget;
    content_ = new QVBoxLayout(holder);
    content_->setContentsMargins(14, 14, 14, 14);
    content_->setSpacing(14);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);

    // ---- machine --------------------------------------------------
    auto* machine = new_group(QStringLiteral("Machine"));
    add_row(machine, K::kCpuCores, QStringLiteral("Processor cores"),
            QStringLiteral("One socket, cores only, no SMT."),
            make_slider(K::kCpuCores, ui::MachineLimits::kCores, QString{}));
    add_row(machine, K::kMemoryMib, QStringLiteral("Memory"),
            QStringLiteral("Ballooned, so the guest gives memory back when it is idle."),
            make_slider(K::kMemoryMib, ui::MachineLimits::kMemoryMib, QStringLiteral(" MiB")));
    add_row(machine, K::kDiskGib, QStringLiteral("Disk"),
            QStringLiteral("Grows only. Windows extends the volume on the next boot."),
            make_slider(K::kDiskGib, ui::MachineLimits::kDiskGib, QStringLiteral(" GiB")));

    // ---- display --------------------------------------------------
    auto* display = new_group(QStringLiteral("Display"));
    add_row(display, K::kGuestWidth, QStringLiteral("Guest resolution"),
            QStringLiteral("The size of the virtual monitor Windows sees, and the ceiling "
                           "a maximised window can reach."),
            make_choice(K::kGuestWidth,
                        Choices{{QStringLiteral("1280 x 720"), 1280},
                                {QStringLiteral("1920 x 1080"), 1920},
                                {QStringLiteral("2560 x 1440"), 2560},
                                {QStringLiteral("3840 x 2160"), 3840}}));
    add_row(display, K::kMatchHostOutput,
            QStringLiteral("Match the monitor the manager is on"),
            QStringLiteral("Takes the resolution from your current output instead."),
            make_toggle(K::kMatchHostOutput));
    add_row(display, K::kScalingPercent, QStringLiteral("Scaling"),
            QStringLiteral("Windows DPI, applied at the guest logon."),
            make_choice(K::kScalingPercent,
                        Choices{{QStringLiteral("100 % (96 DPI)"), 100},
                                {QStringLiteral("125 % (120 DPI)"), 125},
                                {QStringLiteral("150 % (144 DPI)"), 150},
                                {QStringLiteral("200 % (192 DPI)"), 200}}));
    add_row(display, K::kRefreshHz, QStringLiteral("Refresh rate"),
            QStringLiteral("How often a mirrored window repaints. Lower it to spend less "
                           "host CPU on an idle guest."),
            make_choice(K::kRefreshHz,
                        Choices{{QStringLiteral("30 Hz"), 30},
                                {QStringLiteral("60 Hz"), 60},
                                {QStringLiteral("120 Hz"), 120}}));
    add_row(display, K::kVramMib,
            QStringLiteral("Video memory the adapter reports"),
            QStringLiteral("Applications gate their GPU path on this number before they "
                           "ever open a driver."),
            make_choice(K::kVramMib,
                        Choices{{QStringLiteral("512 MiB"), 512},
                                {QStringLiteral("1024 MiB"), 1024},
                                {QStringLiteral("2048 MiB"), 2048},
                                {QStringLiteral("3072 MiB"), 3072},
                                {QStringLiteral("4096 MiB"), 4096},
                                {QStringLiteral("6144 MiB"), 6144},
                                {QStringLiteral("8192 MiB"), 8192}}));
    add_row(display, K::kLivePreview, QStringLiteral("Live preview"),
            QStringLiteral("Streams the whole guest desktop into the overview."),
            make_toggle(K::kLivePreview));

    // ---- render channels ------------------------------------------
    auto* render = new_group(QStringLiteral("Render channels"));
    add_row(render, K::kChannels, QStringLiteral("Channels"),
            QStringLiteral("Each drawing process claims one. A channel carries a single "
                           "command stream, so two processes can never share one."),
            make_choice(K::kChannels,
                        Choices{{QStringLiteral("1 channel"), 1},
                                {QStringLiteral("2 channels"), 2},
                                {QStringLiteral("4 channels"), 4},
                                {QStringLiteral("8 channels"), 8}}));
    add_row(render, K::kGlDevice, QStringLiteral("OpenGL GPU"),
            QStringLiteral("Which card guest applications draw on. Left to itself, "
                           "the driver picks whichever GPU drives this screen — on a "
                           "laptop, the integrated one."),
            make_choice(K::kGlDevice, gl_device_choices()));
    add_row(render, K::kRingKib, QStringLiteral("Command ring"),
            QStringLiteral("A command bigger than this is sent in pieces, each waiting "
                           "for the one before it to drain."),
            make_choice(K::kRingKib,
                        Choices{{QStringLiteral("256 KiB"), 256},
                                {QStringLiteral("512 KiB"), 512},
                                {QStringLiteral("1024 KiB"), 1024},
                                {QStringLiteral("4096 KiB"), 4096},
                                {QStringLiteral("16384 KiB"), 16384}}));
    add_row(render, K::kReplyKib, QStringLiteral("Reply slot"),
            QStringLiteral("An answer that does not fit is collected in extra round "
                           "trips instead of arriving whole."),
            make_choice(K::kReplyKib,
                        Choices{{QStringLiteral("2 MiB"), 2048},
                                {QStringLiteral("8 MiB"), 8192},
                                {QStringLiteral("16 MiB"), 16384},
                                {QStringLiteral("64 MiB"), 65536}}));
    add_row(render, K::kBlobMib, QStringLiteral("Blob arena"),
            QStringLiteral("Large command streams, host visible device memory, and the "
                           "frames on their way to a window."),
            make_choice(K::kBlobMib,
                        Choices{{QStringLiteral("256 MiB"), 256},
                                {QStringLiteral("512 MiB"), 512},
                                {QStringLiteral("1024 MiB"), 1024},
                                {QStringLiteral("2048 MiB"), 2048}}));
    add_row(render, K::kWarnCongestion,
            QStringLiteral("Warn when an area is congested"),
            QStringLiteral("Reads the counters the render server publishes, and falls "
                           "back to reading its log."),
            make_toggle(K::kWarnCongestion));

    // ---- assignments ----------------------------------------------
    auto* bindings = new_group(QStringLiteral("Which application uses which channel"));
    bindings_table_ = make_table({QStringLiteral("Application"), QStringLiteral("Channel"),
                                  QStringLiteral("If it is already in use")});
    auto* bindings_row = new QWidget;
    auto* bindings_layout = new QVBoxLayout(bindings_row);
    bindings_layout->setContentsMargins(12, 10, 12, 12);
    bindings_layout->setSpacing(8);
    bindings_layout->addWidget(Widgets::caption(QStringLiteral("Assignments")));
    bindings_layout->addWidget(
        Widgets::meta(QStringLiteral("Anything left on Any takes the first free channel.")));
    bindings_layout->addWidget(bindings_table_);
    bindings->addWidget(bindings_row);
    rebuild_bindings_table();

    add_row(bindings, K::kNothingFreePolicy,
            QStringLiteral("When nothing is free"),
            QStringLiteral("Every channel claimed and another process wants one."),
            make_choice(K::kNothingFreePolicy,
                        Choices{{QStringLiteral("Wait, then refuse"),
                                 QStringLiteral("wait")},
                                {QStringLiteral("Refuse immediately"),
                                 QStringLiteral("refuse")}}));
    add_row(bindings, K::kChannelWaitSeconds,
            QStringLiteral("How long to wait"),
            QStringLiteral("Before refusing. A process that gives up says so in the log."),
            make_number(K::kChannelWaitSeconds, 1, 120));

    // ---- files ----------------------------------------------------
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

    // The shares are a list because there is no reason for there to be
    // only one of them.
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
    shares_layout->addWidget(
        Widgets::meta(QStringLiteral("Each one becomes a virtiofs device and a drive letter.")));
    shares_table_ = make_table({QStringLiteral("Folder on this machine"),
                                QStringLiteral("Drive"), QStringLiteral("Access"),
                                QString{}});
    shares_layout->addWidget(shares_table_);
    files->addWidget(shares_row);
    rebuild_shares_table();

    connect(add_share, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Share a folder with the guest"), QDir::homePath());
        if (dir.isEmpty()) {
            return;
        }
        auto shares = staged_shares();
        const auto letters = free_drive_letters(shares);
        if (letters.isEmpty()) {
            return;
        }
        shares.push_back(HostShare{dir, letters.first(), false});
        stage_shares(shares);
    });

    add_action_row(files, QStringLiteral("Open the share"),
                   QStringLiteral("Launches your file manager at the guest's C: drive."),
                   QStringLiteral("Open Windows files"), &SettingsPage::open_share_requested);

    // ---- desktop entries ------------------------------------------
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
            QStringLiteral("Registers MIME handlers. An existing default is never "
                           "replaced without asking."),
            make_toggle(K::kFileAssociations));
    add_action_row(entries, QStringLiteral("Rewrite the entries"),
                   QStringLiteral("Regenerates every .desktop file and icon."),
                   QStringLiteral("Rewrite now"), &SettingsPage::rewrite_entries_requested);

    // ---- guest ----------------------------------------------------
    auto* guest = new_group(QStringLiteral("Guest"));
    add_row(guest, K::kAgentPort, QStringLiteral("Agent port"),
            QStringLiteral("Forwarded from the host loopback into the guest."),
            make_number(K::kAgentPort, 1024, 65535));
    add_row(guest, K::kAutostart,
            QStringLiteral("Start the guest when the manager opens"),
            QStringLiteral("Otherwise it starts the first time you launch an application."),
            make_toggle(K::kAutostart));

    // ---- host -----------------------------------------------------
    auto* host = new_group(QStringLiteral("Host"));
    add_row(host, K::kCloseToTray,
            QStringLiteral("Keep running in the tray when closed"),
            QStringLiteral("Closing the window leaves the guest running."),
            make_toggle(K::kCloseToTray));

    // ---- images ---------------------------------------------------
    auto* images = new_group(QStringLiteral("Images"));
    add_action_row(images, QStringLiteral("Download a Windows image"),
                   QStringLiteral("Fetch an installation ISO from microsoft.com into any folder."),
                   QStringLiteral("Open the download tool"),
                   &SettingsPage::download_tool_requested);
    add_action_row(images, QStringLiteral("Trim a Windows image"),
                   QStringLiteral("Build a smaller install ISO from one you have, into any "
                                  "folder. Does not touch the running guest."),
                   QStringLiteral("Open the image tool"), &SettingsPage::trim_tool_requested);
    add_action_row(images, QStringLiteral("Reinstall the guest"),
                   QStringLiteral("Deletes the current disk and runs the first-run wizard "
                                  "again. Applications inside the guest are lost."),
                   QStringLiteral("Reinstall"), &SettingsPage::reinstall_requested,
                   Theme::Role::kStop);

    content_->addStretch(1);

    // ---- apply bar ------------------------------------------------
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

QVBoxLayout* SettingsPage::new_group(const QString& title) {
    // Rows run edge to edge and carry their own padding, so the body
    // has none.
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

    text->addWidget(Widgets::prose(description));

    layout->addLayout(text, 1);
    // The control column is a fixed share of the row, so a slider has
    // somewhere to travel and a combo does not float in the middle.
    control->setFixedWidth(300);
    layout->addWidget(control, 0, Qt::AlignRight);

    group->addWidget(row);

    // A hairline between rows, and none after the last one; the frame's
    // own border closes the group.
    group->addWidget(Widgets::separator());
    return row;
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
    // The number is quiet and right aligned: the slider is the control.
    slider->spin()->setFixedWidth(96);
    slider->spin()->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    slider->spin()->setObjectName(QStringLiteral("SliderValue"));
    slider->set_value(pending_.staged_or_current(key).toInt());
    connect(slider, &ui::SliderSpin::value_changed, this,
            [this, key](int value) { put(key, value); });
    return slider;
}


// The GPUs to offer, as the render server enumerates them.
//
// Asking costs a process, so it is asked once and the answer stands for
// the life of the page. A host that answers with nothing still gets the
// automatic entry: a chooser with no choices in it would say the
// machine has no GPU, which is not what "we could not enumerate" means.
QVector<QPair<QString, QVariant>> SettingsPage::gl_device_choices() {
    // What a driver calls itself is written for a log, not for a
    // control: the trailing bus and instruction-set notes, and the
    // registered-trademark marks, cost the width the card's actual name
    // needs. Only decoration is dropped — no name is invented.
    const auto shorten = [](QString name) {
        name.remove(QStringLiteral("(R)"));
        name.remove(QStringLiteral("(TM)"));
        if (const qsizetype bus = name.indexOf(QStringLiteral("/PCIe")); bus > 0) {
            name.truncate(bus);
        }
        // "Mesa Intel Iris Xe Graphics (ADL GT2)" names the stack, the
        // vendor, the card and the silicon revision. The card is the
        // part being chosen between.
        if (name.startsWith(QStringLiteral("Mesa "), Qt::CaseInsensitive)) {
            name.remove(0, 5);
        }
        if (name.contains(QStringLiteral("llvmpipe"), Qt::CaseInsensitive)) {
            return QStringLiteral("llvmpipe (software)");
        }
        if (const qsizetype detail = name.lastIndexOf(QLatin1Char('('));
            detail > 0 && name.endsWith(QLatin1Char(')'))) {
            name.truncate(detail);
        }
        return name.simplified();
    };

    QVector<QPair<QString, QVariant>> choices{{QStringLiteral("Automatic"), -1}};
    for (const auto& device : vm::RenderServer::gl_devices()) {
        // A device with no context is still listed: leaving it out
        // would renumber the ones after it, and the number is the
        // setting.
        const QString label =
            device.renderer.isEmpty()
                ? QStringLiteral("%1 — no OpenGL").arg(device.index)
                : QStringLiteral("%1 — %2").arg(device.index).arg(shorten(device.renderer));
        choices.push_back({label, device.index});
    }
    return choices;
}

QWidget* SettingsPage::make_choice(const QString& key, const Choices& options) {
    auto* combo = new QComboBox;
    for (const auto& option : options) {
        combo->addItem(option.first, option.second);
    }
    const QVariant current = pending_.staged_or_current(key);
    const int index = combo->findData(current);
    combo->setCurrentIndex(index >= 0 ? index : 0);
    // The control column is a fixed width and some of these names are
    // longer than it. The list that drops down is not bound by it, so
    // the choice is made against the whole name even when the closed
    // box shows the beginning of one.
    combo->view()->setMinimumWidth(combo->sizeHint().width());
    combo->setToolTip(combo->currentText());
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key](int i) {
        combo->setToolTip(combo->itemText(i));
        put(key, combo->itemData(i));
    });
    return combo;
}

QWidget* SettingsPage::make_toggle(const QString& key) {
    // A switch rather than a check box: every boolean here is a thing
    // that is on or off, not one of a list of things being ticked.
    auto* box = new ui::ToggleSwitch;
    box->setChecked(pending_.staged_or_current(key).toBool());
    connect(box, &ui::ToggleSwitch::toggled, this, [this, key](bool on) { put(key, on); });

    // The control column is right aligned, and a bare checkbox floating
    // there reads as unfinished; the wrapper gives it the same right
    // edge as every other control.
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(box);
    return row;
}

QWidget* SettingsPage::make_number(const QString& key, int min, int max) {
    auto* spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setObjectName(QString::fromLatin1(Theme::Role::kMono));
    spin->setValue(pending_.staged_or_current(key).toInt());
    connect(spin, &QSpinBox::valueChanged, this,
            [this, key](int value) { put(key, value); });
    return spin;
}

QWidget* SettingsPage::make_text(const QString& key) {
    auto* edit = new QLineEdit(pending_.staged_or_current(key).toString());
    connect(edit, &QLineEdit::editingFinished, this,
            [this, key, edit] { put(key, edit->text()); });
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
    // A share is a device on the machine, so it stages rather than
    // applying under a running guest.
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

void SettingsPage::rebuild_bindings_table() {
    const auto& configs = apps_.configs();
    auto bindings = settings_.channel_bindings();
    const int channels = pending_.staged_or_current(K::kChannels).toInt();

    bindings_table_->setRowCount(static_cast<int>(configs.size()));
    int row = 0;
    for (const auto& cfg : configs) {
        const auto existing =
            std::find_if(bindings.begin(), bindings.end(),
                         [&cfg](const ChannelBinding& b) { return b.app_id == cfg.id; });
        const bool pinned = existing != bindings.end();

        bindings_table_->setItem(row, 0, new QTableWidgetItem(cfg.name));

        auto* channel = new QComboBox;
        channel->addItem(QStringLiteral("Any"), -1);
        for (int i = 0; i < channels; ++i) {
            channel->addItem(QStringLiteral("Channel %1").arg(i), i);
        }
        channel->setCurrentIndex(
            channel->findData(pinned ? existing->channel : -1));

        auto* policy = new QComboBox;
        for (const auto p : {ChannelBusyPolicy::TakeAnyFree, ChannelBusyPolicy::Wait,
                             ChannelBusyPolicy::Refuse}) {
            policy->addItem(config::ManagerSettings::policy_label(p),
                            config::ManagerSettings::policy_to_string(p));
        }
        policy->setCurrentIndex(policy->findData(config::ManagerSettings::policy_to_string(
            pinned ? existing->busy : ChannelBusyPolicy::TakeAnyFree)));
        // A policy only means anything for a pinned application: there
        // is nothing to wait for when any channel will do.
        policy->setEnabled(pinned && existing->channel >= 0);

        const QString app_id = cfg.id;
        auto write = [this, app_id, channel, policy] {
            auto all = settings_.channel_bindings();
            all.removeIf([&app_id](const ChannelBinding& b) { return b.app_id == app_id; });
            const int index = channel->currentData().toInt();
            if (index >= 0) {
                ChannelBinding binding;
                binding.app_id = app_id;
                binding.channel = index;
                binding.busy =
                    config::ManagerSettings::policy_from_string(policy->currentData().toString());
                all.push_back(binding);
            }
            settings_.set_channel_bindings(all);
            policy->setEnabled(index >= 0);
        };
        connect(channel, &QComboBox::currentIndexChanged, this, [write](int) { write(); });
        connect(policy, &QComboBox::currentIndexChanged, this, [write](int) { write(); });

        bindings_table_->setCellWidget(row, 1, channel);
        bindings_table_->setCellWidget(row, 2, policy);
        ++row;
    }
}

void SettingsPage::refresh_apply_bar() {
    const int count = pending_.count();
    apply_bar_->setVisible(count > 0);
    apply_count_->setText(count == 1 ? QStringLiteral("1 change needs a restart")
                                     : QStringLiteral("%1 changes need a restart").arg(count));
    apply_diffs_->setText(pending_.summary().join(QStringLiteral("   ")));
}

}  // namespace lsw::host::manager
