#include "lsw/host/manager/files_page.hpp"

#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/manager/setting_controls.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/item_list.hpp"
#include "lsw/host/ui/item_row.hpp"
#include "lsw/host/ui/key_values.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/segmented_control.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/toggle_switch.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lsw::host::manager {

namespace {

using config::HostShare;
using ui::ItemRow;
using ui::LineIcon;
using ui::StatusPill;
using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;
using Area = services::StatusHub::Area;

constexpr int kAccessWidth = 108;
constexpr int kPillWidth = 116;

QWidget* toggle_line(ui::ToggleSwitch* toggle, const QString& text) {
    auto* line = new QWidget;
    auto* row = new QHBoxLayout(line);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(9);
    row->addWidget(toggle);
    auto* label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(Theme::Role::kDim));
    row->addWidget(label);
    return line;
}

QString folder_name(const QString& path) {
    const QString name = QDir(path).dirName();
    return name.isEmpty() ? path : name;
}

}

FilesPage::FilesPage(services::StatusHub& status, config::ManagerSettings& settings,
                     config::PendingChanges& pending, QWidget* parent)
    : ui::ScrollPage(parent), status_(status), settings_(settings), pending_(pending),
      controls_(new SettingControls(settings, pending, this)) {
    build_folders();
    build_smb();
    column()->addStretch(1);

    connect(&pending_, &config::PendingChanges::changed, this, &FilesPage::refresh_folders,
            Qt::QueuedConnection);
    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area == Area::Vm) {
            refresh_folders();
            refresh_smb();
        }
    });
    connect(&settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kHostShares) {
            refresh_folders();
        } else if (key == K::kSmbEnabled || key == K::kShareScope) {
            refresh_smb();
        }
    });
    refresh_folders();
    refresh_smb();
}

QString FilesPage::summary() const {
    const vm::DomainDescription* definition = status_.vm().definition();
    QString folders = QStringLiteral("-");
    if (definition != nullptr) {
        const auto count = definition->shares.size();
        const QString noun = count == 1 ? QStringLiteral("folder") : QStringLiteral("folders");
        folders = QStringLiteral("%1 %2 %3")
                      .arg(count)
                      .arg(noun, status_.vm().running() ? QStringLiteral("in Windows")
                                                        : QStringLiteral("shared"));
    }
    const QString smb = settings_.bool_value(K::kSmbEnabled)
                            ? QStringLiteral("Windows on smb://%1")
                                  .arg(platform::GuestSmbShare::endpoint())
                            : QStringLiteral("file manager entry off");
    return QStringLiteral("%1 · %2").arg(folders, smb);
}

QVector<FilesPage::Entry> FilesPage::entries() const {
    QVector<Entry> out;
    const auto shares = staged_shares();
    const bool running = status_.vm().running();
    for (const HostShare& share : shares) {
        out.push_back({share, false, in_windows(share)});
    }
    if (running) {
        if (const vm::DomainDescription* definition = status_.vm().definition()) {
            const auto stored = settings_.host_shares();
            for (const auto& live : definition->shares) {
                const bool kept = std::any_of(shares.begin(), shares.end(), [&live](const HostShare& s) {
                    return s.host_path == live.host_path;
                });
                if (kept) {
                    continue;
                }
                HostShare share{live.host_path, QStringLiteral("-"), live.read_only};
                for (const HostShare& known : stored) {
                    if (known.host_path == live.host_path) {
                        share.drive = known.drive;
                    }
                }
                out.push_back({share, true, true});
            }
        }
    }
    return out;
}

bool FilesPage::in_windows(const HostShare& share) const {
    const vm::DomainDescription* definition = status_.vm().definition();
    return definition != nullptr &&
           std::any_of(definition->shares.begin(), definition->shares.end(),
                       [&share](const vm::DomainDescription::Share& known) {
                           return known.host_path == share.host_path &&
                                  known.read_only == share.read_only;
                       });
}

QVector<HostShare> FilesPage::staged_shares() const {
    return config::ManagerSettings::decode_host_shares(
        pending_.staged_or_current(K::kHostShares).toString());
}

void FilesPage::stage_shares(const QVector<HostShare>& shares) {
    pending_.stage(K::kHostShares, config::ManagerSettings::encode_host_shares(shares));
}

void FilesPage::build_folders() {
    auto panel = ui::Panel::area(QStringLiteral("Folders Windows can see"),
                                 QStringLiteral("a drive letter each"));
    panel.header->addWidget(Widgets::tag(true));
    letters_ = Widgets::meta(QString{});
    panel.header->addWidget(letters_);
    auto* add = Widgets::button(QStringLiteral("Add a folder"));
    add_button_ = add;
    connect(add, &QPushButton::clicked, this, &FilesPage::add_folder);
    panel.header->addWidget(add);

    panel.body->setContentsMargins(0, 0, 0, 0);
    panel.body->setSpacing(0);
    list_ = new ui::ItemList;
    panel.body->addWidget(list_);
    empty_ = Widgets::meta(QStringLiteral("No folder is shared with Windows yet."), true);
    empty_->setContentsMargins(12, 10, 12, 10);
    panel.body->addWidget(empty_);
    panel.add_foot(QStringLiteral(
        "Each folder is a virtiofs device on the machine, mounted in Windows with WinFsp - "
        "Windows File System Proxy, Copyright (C) Bill Zissimopoulos "
        "(github.com/winfsp/winfsp). Changes to this list wait for the next restart."));
    column()->addWidget(panel.frame);
}

void FilesPage::add_folder() {
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
}

void FilesPage::refresh_folders() {
    const auto shares = staged_shares();
    const auto total = config::ManagerSettings::free_drive_letters({}).size();
    letters_->setText(QStringLiteral("%1 of %2 letters").arg(shares.size()).arg(total));
    add_button_->setEnabled(shares.size() < total);
    Q_EMIT summary_changed();

    const bool running = status_.vm().running();
    const auto rows = entries();
    QStringList signature{QString::number(running)};
    for (const Entry& entry : rows) {
        signature << entry.share.host_path << entry.share.drive
                  << QString::number(entry.share.read_only) << QString::number(entry.leaving)
                  << QString::number(entry.defined);
    }
    if (signature == rows_signature_) {
        return;
    }
    rows_signature_ = signature;

    list_->clear();
    for (const Entry& entry : rows) {
        const HostShare& share = entry.share;
        auto* row = new ItemRow(LineIcon::Shape::Files);
        row->set_name(folder_name(share.host_path));
        row->set_sub(share.host_path);

        auto* arrow = new QLabel(QStringLiteral("→"));
        arrow->setProperty(Theme::Tone::kProperty, Theme::Tone::kFaint);
        row->trailing()->addWidget(arrow);

        auto* drive = new QLabel(share.drive);
        drive->setObjectName(QString::fromLatin1(Theme::Role::kDriveBadge));
        row->trailing()->addWidget(drive);

        auto* access = new QComboBox;
        access->addItem(QStringLiteral("Read write"), false);
        access->addItem(QStringLiteral("Read only"), true);
        access->setCurrentIndex(share.read_only ? 1 : 0);
        access->setFixedWidth(kAccessWidth);
        access->setEnabled(!entry.leaving);
        Widgets::compact(access);
        connect(access, &QComboBox::activated, this, [this, path = share.host_path](int index) {
            const bool read_only = index == 1;
            QMetaObject::invokeMethod(
                this,
                [this, path, read_only] {
                    auto now = staged_shares();
                    for (auto& item : now) {
                        if (item.host_path == path) {
                            item.read_only = read_only;
                        }
                    }
                    stage_shares(now);
                },
                Qt::QueuedConnection);
        });
        row->trailing()->addWidget(access);

        QLabel* pill = nullptr;
        if (entry.leaving) {
            pill = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Until restart"));
        } else if (!entry.defined) {
            pill = StatusPill::make(StatusPill::State::Pending,
                                    running ? QStringLiteral("After restart")
                                            : QStringLiteral("Next start"));
        } else if (running) {
            pill = StatusPill::make(StatusPill::State::Done, QStringLiteral("In Windows"));
        } else {
            pill = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Guest off"));
        }
        auto* pill_cell = new QWidget;
        pill_cell->setMinimumWidth(kPillWidth);
        auto* pill_row = new QHBoxLayout(pill_cell);
        pill_row->setContentsMargins(0, 0, 0, 0);
        pill_row->addStretch(1);
        pill_row->addWidget(pill);
        row->trailing()->addWidget(pill_cell);

        if (entry.leaving) {
            auto* undo = Widgets::button(QStringLiteral("↶"), Theme::Role::kIconButton);
            undo->setToolTip(QStringLiteral("Undo: keep sharing this folder"));
            undo->setCursor(Qt::PointingHandCursor);
            connect(undo, &QPushButton::clicked, this, [this, share] {
                auto now = staged_shares();
                now.push_back(share);
                stage_shares(now);
            });
            row->trailing()->addWidget(undo);
        } else {
            auto* remove = Widgets::button(QStringLiteral("×"), Theme::Role::kIconButton);
            remove->setToolTip(QStringLiteral("Stop sharing"));
            remove->setCursor(Qt::PointingHandCursor);
            connect(remove, &QPushButton::clicked, this, [this, path = share.host_path] {
                auto remaining = staged_shares();
                remaining.removeIf([&path](const HostShare& s) { return s.host_path == path; });
                stage_shares(remaining);
            });
            row->trailing()->addWidget(remove);
        }
        list_->add(row);
    }
    list_->setVisible(!rows.isEmpty());
    empty_->setVisible(rows.isEmpty());
}

void FilesPage::build_smb() {
    auto panel = ui::Panel::area(QStringLiteral("Windows in your file manager"),
                                 QStringLiteral("SMB, the file manager entry"));
    serving_ = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Off"));
    panel.header->addWidget(serving_);
    auto* entry_toggle = controls_->toggle(K::kSmbEnabled);
    entry_toggle->setToolTip(QStringLiteral("Add or remove the Windows entry in your file manager"));
    panel.header->addWidget(entry_toggle);

    auto* rows_widget = new QWidget;
    auto* rows_column = new QVBoxLayout(rows_widget);
    rows_column->setContentsMargins(0, 0, 0, 0);
    ui::KeyValues rows(rows_column);

    auto* address_cell = new QWidget;
    auto* address_row = new QHBoxLayout(address_cell);
    address_row->setContentsMargins(0, 0, 0, 0);
    address_row->setSpacing(10);
    address_ = new QLabel;
    address_->setObjectName(QString::fromLatin1(Theme::Role::kMono));
    address_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* copy = Widgets::button(QStringLiteral("Copy"), Theme::Role::kQuiet);
    Widgets::compact(copy);
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(address_->text()); });
    address_row->addWidget(address_);
    address_row->addWidget(copy);
    rows.add_widget(QStringLiteral("Address"), address_cell);

    rows.add_widget(QStringLiteral("Opens"),
                    controls_->segmented(K::kShareScope,
                                         {{QStringLiteral("The whole disk, C:"), {}, QStringLiteral("C$")},
                                          {QStringLiteral("Your Windows profile"), {},
                                           QStringLiteral("profile")}}));

    auto* bookmarks = new QWidget;
    auto* bookmark_row = new QHBoxLayout(bookmarks);
    bookmark_row->setContentsMargins(0, 0, 0, 0);
    bookmark_row->setSpacing(22);
    bookmark_row->addWidget(toggle_line(controls_->toggle(K::kBookmarkDolphin), QStringLiteral("Dolphin")));
    bookmark_row->addWidget(toggle_line(controls_->toggle(K::kBookmarkGtk),
                                        QStringLiteral("GNOME Files and Thunar")));
    rows.add_widget(QStringLiteral("Bookmarks"), bookmarks);

    panel.body->addWidget(rows_widget);
    panel.body->addWidget(Widgets::meta(
        QStringLiteral("A Places entry titled Windows. Nothing is mounted, so Windows junctions "
                       "never turn into extra mounts."),
        true));
    column()->addWidget(panel.frame);
}

void FilesPage::refresh_smb() {
    const bool on = settings_.bool_value(K::kSmbEnabled);
    const auto& vm = status_.vm();
    if (!on) {
        StatusPill::set(serving_, StatusPill::State::Pending, QStringLiteral("Off"));
    } else if (!vm.running()) {
        StatusPill::set(serving_, StatusPill::State::Pending, QStringLiteral("Guest off"));
    } else if (vm.agent_online()) {
        StatusPill::set(serving_, StatusPill::State::Done, QStringLiteral("Serving"));
    } else {
        StatusPill::set(serving_, StatusPill::State::Running, QStringLiteral("Starting"));
    }
    address_->setText(platform::GuestSmbShare::url(
        QString{}, QString{},
        platform::GuestSmbShare::share_for_scope(settings_.string_value(K::kShareScope))));
    Q_EMIT summary_changed();
}

}
