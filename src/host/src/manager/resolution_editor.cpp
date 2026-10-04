#include "lsw/host/manager/resolution_editor.hpp"

#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using config::DisplayMode;
using config::DisplayModes;
using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;

constexpr int kColPreferred = 0;
constexpr int kColSize = 1;
constexpr int kColAspect = 2;
constexpr int kColActions = 3;

// The sizes most monitors and most people ask for.
constexpr DisplayMode kCommon[] = {
    {1280, 720},  {1366, 768},  {1600, 900},  {1920, 1080}, {1920, 1200},
    {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 2160},
};

QPushButton* small(const QString& glyph, const QString& tip) {
    auto* button = Widgets::button(glyph, Theme::Role::kIconButton);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

}  // namespace

ResolutionEditor::ResolutionEditor(config::ManagerSettings& settings, QWidget* parent)
    : QWidget(parent), settings_(settings) {
    auto* frame = new QFrame;
    Theme::surface(frame, Theme::Role::kPanel);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(frame);
    auto* column = new QVBoxLayout(frame);
    column->setContentsMargins(1, 1, 1, 1);
    column->setSpacing(0);

    table_ = new QTableWidget(0, 4);
    Theme::rows_table(table_, {QStringLiteral("PREFERRED"), QStringLiteral("RESOLUTION"),
                               QStringLiteral("ASPECT"), QString{}});
    table_->verticalHeader()->setDefaultSectionSize(38);
    table_->setProperty("controls", true);
    auto* head = table_->horizontalHeader();
    head->setSectionResizeMode(kColPreferred, QHeaderView::Fixed);
    head->setSectionResizeMode(kColSize, QHeaderView::Stretch);
    head->setSectionResizeMode(kColAspect, QHeaderView::Fixed);
    head->setSectionResizeMode(kColActions, QHeaderView::Fixed);
    table_->setColumnWidth(kColPreferred, 96);
    table_->setColumnWidth(kColAspect, 90);
    table_->setColumnWidth(kColActions, 104);
    column->addWidget(table_);

    // The add row: a preset, or any size typed in, checked as it is typed.
    auto* foot = new QWidget;
    Theme::surface(foot, Theme::Role::kListFoot);
    auto* foot_column = new QVBoxLayout(foot);
    foot_column->setContentsMargins(10, 8, 10, 8);
    foot_column->setSpacing(6);
    auto* add_row = new QHBoxLayout;
    add_row->setSpacing(8);
    presets_ = new QComboBox;
    presets_->setMinimumWidth(200);
    width_ = new QSpinBox;
    height_ = new QSpinBox;
    for (auto* spin : {width_, height_}) {
        spin->setRange(1, 9999);
        spin->setFixedWidth(72);
        spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        spin->setObjectName(QString::fromLatin1(Theme::Role::kMono));
    }
    // Filled in by rebuild with a size the list does not have yet.
    add_ = Widgets::button(QStringLiteral("Add a resolution"));
    add_row->addWidget(presets_);
    add_row->addWidget(width_);
    add_row->addWidget(Widgets::meta(QStringLiteral("×")));
    add_row->addWidget(height_);
    add_row->addWidget(add_);
    add_row->addStretch(1);
    reason_ = Widgets::meta(QString{}, true);
    foot_column->addLayout(add_row);
    foot_column->addWidget(reason_);
    column->addWidget(foot);

    fill_presets();
    connect(presets_, &QComboBox::activated, this, [this](int index) {
        if (const auto mode = DisplayModes::parse(presets_->itemData(index).toString())) {
            width_->setValue(mode->width);
            height_->setValue(mode->height);
        }
    });
    for (auto* spin : {width_, height_}) {
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            edited_ = true;
            validate();
        });
    }
    connect(add_, &QPushButton::clicked, this, [this] {
        const DisplayMode mode{width_->value(), height_->value()};
        QStringList list = DisplayModes::normalized(settings_.string_list_value(K::kDisplayModes));
        if (!DisplayModes::refusal_to_add(list, mode).isEmpty()) {
            return;
        }
        list << mode.text();
        // A fresh suggestion for the next one: the boxes are no longer
        // the user's edit once it has been added.
        edited_ = false;
        write(list);
    });
    // Another page can change the list too: the Graphics page's switch.
    connect(&settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kDisplayModes) {
            QMetaObject::invokeMethod(this, &ResolutionEditor::rebuild, Qt::QueuedConnection);
        }
    });
    rebuild();
}

void ResolutionEditor::fill_presets() {
    presets_->clear();
    presets_->addItem(QStringLiteral("Common sizes and monitors"), QString{});
    // The monitors on this machine first: the size most often wanted is
    // the one in front of you. Their own pixels, not the scaled desktop.
    for (const QScreen* screen : QGuiApplication::screens()) {
        const QSize size = screen->geometry().size() * screen->devicePixelRatio();
        const DisplayMode mode{size.width(), size.height()};
        presets_->addItem(QStringLiteral("%1 · %2").arg(mode.label(), screen->name()),
                          mode.text());
    }
    for (const auto& mode : kCommon) {
        presets_->addItem(mode.label(), mode.text());
    }
}

void ResolutionEditor::suggest(const QStringList& list) {
    // The first common size the list lacks, so the boxes never open on a
    // size Add would refuse; the user's own numbers are left alone.
    DisplayMode suggestion{1920, 1080};
    for (const auto& mode : kCommon) {
        if (DisplayModes::refusal_to_add(list, mode).isEmpty()) {
            suggestion = mode;
            break;
        }
    }
    const QSignalBlocker quiet_width(width_);
    const QSignalBlocker quiet_height(height_);
    width_->setValue(suggestion.width);
    height_->setValue(suggestion.height);
}

void ResolutionEditor::validate() {
    const DisplayMode mode{width_->value(), height_->value()};
    const QString why =
        DisplayModes::refusal_to_add(settings_.string_list_value(K::kDisplayModes), mode);
    add_->setEnabled(why.isEmpty() && isEnabled());
    reason_->setText(why);
    reason_->setVisible(edited_ && !why.isEmpty());
    Widgets::set_flag(reason_, Theme::Tone::kProperty, QVariant(Theme::Tone::kBad));
}

void ResolutionEditor::write(const QStringList& list) {
    settings_.set_value(K::kDisplayModes, list);
    rebuild();
}

void ResolutionEditor::move(int row, int by) {
    QStringList list = DisplayModes::normalized(settings_.string_list_value(K::kDisplayModes));
    const int to = row + by;
    if (row < 0 || to < 0 || row >= list.size() || to >= list.size()) {
        return;
    }
    list.move(row, to);
    write(list);
}

void ResolutionEditor::use_host_modes(bool on) {
    const QStringList list = DisplayModes::normalized(settings_.string_list_value(K::kDisplayModes));
    if (on) {
        if (!list.isEmpty()) {
            kept_ = list;
        }
        write({});
    } else if (list.isEmpty()) {
        write(kept_.isEmpty() ? QStringList{DisplayMode{1920, 1080}.text()} : kept_);
    }
}

void ResolutionEditor::rebuild() {
    const QStringList list = DisplayModes::normalized(settings_.string_list_value(K::kDisplayModes));
    setEnabled(!list.isEmpty());
    table_->setRowCount(static_cast<int>(list.size()));
    for (int row = 0; row < list.size(); ++row) {
        const auto mode = DisplayModes::parse(list.at(row));
        // The first entry is the preferred one; marking another moves it
        // to the top, which is what makes it preferred.
        auto* radio_holder = new QWidget;
        auto* radio_row = new QHBoxLayout(radio_holder);
        radio_row->setContentsMargins(12, 0, 0, 0);
        auto* radio = new QRadioButton;
        radio->setChecked(row == 0);
        radio->setToolTip(QStringLiteral("The size Windows uses"));
        radio_row->addWidget(radio);
        radio_row->addStretch(1);
        connect(radio, &QRadioButton::clicked, this, [this, row] {
            QMetaObject::invokeMethod(this, [this, row] { move(row, -row); }, Qt::QueuedConnection);
        });
        table_->setCellWidget(row, kColPreferred, radio_holder);

        auto* size = new QTableWidgetItem(mode->label());
        size->setFont(Theme::mono_font(12));
        table_->setItem(row, kColSize, size);
        table_->setItem(row, kColAspect, Theme::kind_cell(mode->aspect()));

        auto* actions = new QWidget;
        auto* action_row = new QHBoxLayout(actions);
        action_row->setContentsMargins(4, 0, 8, 0);
        action_row->setSpacing(4);
        auto* up = small(QStringLiteral("↑"), QStringLiteral("Move up"));
        auto* down = small(QStringLiteral("↓"), QStringLiteral("Move down"));
        auto* remove = small(QStringLiteral("×"), QStringLiteral("Remove"));
        up->setEnabled(row > 0);
        down->setEnabled(row + 1 < list.size());
        // Removing the last size would be asking for the host's modes,
        // which has a switch of its own that says so.
        remove->setEnabled(list.size() > 1);
        // Queued: each one rebuilds the table it sits in.
        connect(up, &QPushButton::clicked, this, [this, row] {
            QMetaObject::invokeMethod(this, [this, row] { move(row, -1); }, Qt::QueuedConnection);
        });
        connect(down, &QPushButton::clicked, this, [this, row] {
            QMetaObject::invokeMethod(this, [this, row] { move(row, 1); }, Qt::QueuedConnection);
        });
        connect(remove, &QPushButton::clicked, this, [this, row] {
            QMetaObject::invokeMethod(this, [this, row] {
                QStringList now = DisplayModes::normalized(
                    settings_.string_list_value(K::kDisplayModes));
                if (row < now.size() && now.size() > 1) {
                    now.removeAt(row);
                    write(now);
                }
            }, Qt::QueuedConnection);
        });
        action_row->addStretch(1);
        action_row->addWidget(up);
        action_row->addWidget(down);
        action_row->addWidget(remove);
        table_->setCellWidget(row, kColActions, actions);
    }
    Theme::fit_rows(table_);
    if (!edited_) {
        suggest(list);
    }
    validate();
    if (list.isEmpty()) {
        reason_->setText(QStringLiteral("The host monitors' own modes are offered."));
        reason_->setVisible(true);
        Widgets::set_flag(reason_, Theme::Tone::kProperty, QVariant());
    }
}

}  // namespace lsw::host::manager
