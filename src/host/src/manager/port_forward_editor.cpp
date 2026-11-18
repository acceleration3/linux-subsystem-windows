#include "lsw/host/manager/port_forward_editor.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/ui/flow_layout.hpp"
#include "lsw/host/ui/segmented_control.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace lsw::host::manager {

namespace {

using config::PortForward;
using config::PortForwards;
using config::PortPreset;
using platform::HostAddresses;
using ui::StatusPill;
using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;
using ForwardState = NetworkState::ForwardState;

constexpr int kSide = 12;
constexpr int kGap = 14;
constexpr int kProtocolWidth = 76;
constexpr int kPortWidth = 132;
constexpr const char* kForward = "forward";

QVector<ui::SegmentedControl::Option> reach_options() {
    return {{QStringLiteral("This computer"), {}, false},
            {QStringLiteral("The network"), {}, true}};
}

QString row_key(const PortForward& entry) {
    return QStringLiteral("%1 %2")
        .arg(PortForwards::protocol_name(entry.protocol))
        .arg(entry.host_port);
}

QString one(const PortForward& entry) {
    return PortForwards::encode({entry});
}

QString full_text(const PortForward& entry) {
    return QStringLiteral("%1 %2 → %3")
        .arg(PortForwards::protocol_name(entry.protocol))
        .arg(entry.host_port)
        .arg(entry.guest_port);
}

QWidget* fixed_cell(QWidget* widget, int width) {
    auto* cell = new QWidget;
    cell->setFixedWidth(width);
    auto* row = new QHBoxLayout(cell);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(widget, 0, Qt::AlignLeft | Qt::AlignVCenter);
    row->addStretch(1);
    return cell;
}

QLabel* number(int port) {
    auto* label = new QLabel(QString::number(port));
    label->setObjectName(QString::fromLatin1(Theme::Role::kMono));
    label->setFixedWidth(kPortWidth);
    return label;
}

QLabel* pill_for(ForwardState state) {
    switch (state) {
    case ForwardState::Active:
        return StatusPill::make(StatusPill::State::Done, QStringLiteral("Active"));
    case ForwardState::PortInUse:
        return StatusPill::make(StatusPill::State::Failed, QStringLiteral("Port in use"));
    case ForwardState::AfterRestart:
        return StatusPill::make(StatusPill::State::Pending, QStringLiteral("After restart"));
    case ForwardState::NextStart:
        break;
    }
    return StatusPill::make(StatusPill::State::Pending, QStringLiteral("Next start"));
}

void clear(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

}  // namespace

PortForwardEditor::PortForwardEditor(config::ManagerSettings& settings,
                                     config::PendingChanges& pending, QWidget* parent)
    : QWidget(parent), pending_(pending) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    build_presets(column);
    build_table(column);
    build_notes(column);

    connect(&pending_, &config::PendingChanges::changed, this, [this] {
        if (!staging_ && entries() != shown_) {
            undo_.reset();
            offered_ = -1;
            say(QString{}, Under::Host);
        }
        rebuild();
        Q_EMIT changed();
    });
    connect(&settings, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kPortForwards) {
            rebuild();
            Q_EMIT changed();
        }
    });
    rebuild();
}

void PortForwardEditor::build_presets(QVBoxLayout* column) {
    auto* holder = new QWidget;
    auto* box = new QVBoxLayout(holder);
    box->setContentsMargins(kSide, 11, kSide, 9);
    box->setSpacing(7);

    auto* chips = new QWidget;
    auto* flow = new ui::FlowLayout(chips, ui::FlowLayout::Mode::Wrap, 6, 6);
    flow->setContentsMargins(0, 0, 0, 0);
    const QVector<PortPreset>& presets = PortForwards::presets();
    for (int i = 0; i < presets.size(); ++i) {
        auto* chip = Widgets::button(presets.at(i).name, Theme::Role::kChip);
        chip->setCursor(Qt::PointingHandCursor);
        connect(chip, &QPushButton::clicked, this, [this, i] {
            const PortPreset& preset = PortForwards::presets().at(i);
            if (!PortForwards::applied(entries(), preset)) {
                (void)apply(preset);
                return;
            }
            offered_ = offered_ == i ? -1 : i;
            show_presets(entries());
            order_tabs();
        });
        flow->addWidget(chip);
        chips_.push_back(chip);
    }
    box->addWidget(chips);

    offer_ = new QWidget;
    auto* offer = new QHBoxLayout(offer_);
    offer->setContentsMargins(0, 0, 0, 0);
    offer->setSpacing(8);
    offer_text_ = Widgets::meta(QString{});
    offer_remove_ = Widgets::button(QString{});
    Widgets::compact(offer_remove_);
    offer_keep_ = Widgets::button(QStringLiteral("Keep"), Theme::Role::kQuiet);
    Widgets::compact(offer_keep_);
    connect(offer_remove_, &QPushButton::clicked, this, [this] {
        if (offered_ >= 0) {
            remove_preset(PortForwards::presets().at(offered_));
        }
    });
    connect(offer_keep_, &QPushButton::clicked, this, [this] {
        offered_ = -1;
        show_presets(entries());
    });
    offer->addWidget(offer_text_);
    offer->addWidget(offer_remove_);
    offer->addWidget(offer_keep_);
    offer->addStretch(1);
    offer_->hide();
    box->addWidget(offer_);

    preset_refusal_ = Widgets::meta(QString{}, true);
    preset_refusal_->setProperty(Theme::Tone::kProperty, Theme::Tone::kBad);
    preset_refusal_->hide();
    box->addWidget(preset_refusal_);
    column->addWidget(holder);
}

void PortForwardEditor::build_table(QVBoxLayout* column) {
    reach_ = new ui::SegmentedControl;
    reach_->set_options(reach_options());
    reach_->set_current(false);
    reach_->setToolTip(QStringLiteral("Who can connect to the port on this computer"));
    reach_width_ = reach_->sizeHint().width();

    auto* head = new QWidget;
    auto* head_row = new QHBoxLayout(head);
    head_row->setContentsMargins(kSide, 2, kSide, 7);
    head_row->setSpacing(kGap);
    const auto caption = [head_row](const QString& text, int width) {
        auto* label = Widgets::caption(text);
        if (width > 0) {
            label->setFixedWidth(width);
        }
        head_row->addWidget(label, width > 0 ? 0 : 1);
    };
    caption(QStringLiteral("Protocol"), kProtocolWidth);
    caption(QStringLiteral("On this computer"), kPortWidth);
    caption(QStringLiteral("In Windows"), kPortWidth);
    caption(QStringLiteral("Reachable from"), reach_width_);
    caption(QStringLiteral("State"), 0);
    column->addWidget(head);
    column->addWidget(Widgets::separator());

    rows_ = new QWidget;
    rows_box_ = new QVBoxLayout(rows_);
    rows_box_->setContentsMargins(0, 0, 0, 0);
    rows_box_->setSpacing(0);
    column->addWidget(rows_);

    empty_ = Widgets::empty_state(
        QStringLiteral("No port forwarded"),
        QStringLiteral("A forwarded port lets a program on this computer, or another machine, "
                       "reach a server that runs in Windows. Pick a set above or add a port "
                       "here."));
    empty_->setContentsMargins(kSide, 10, kSide, 10);
    column->addWidget(empty_);
    column->addWidget(Widgets::separator());

    auto* form = new QWidget;
    auto* form_row = new QHBoxLayout(form);
    form_row->setContentsMargins(kSide, 8, kSide, 8);
    form_row->setSpacing(kGap);
    protocol_ = new QComboBox;
    protocol_->addItem(PortForwards::protocol_name(PortForward::Protocol::Tcp));
    protocol_->addItem(PortForwards::protocol_name(PortForward::Protocol::Udp));
    protocol_->setFixedWidth(kProtocolWidth);
    protocol_->setAccessibleName(QStringLiteral("Protocol"));
    host_port_ = new QLineEdit;
    host_port_->setPlaceholderText(QStringLiteral("Port"));
    host_port_->setFixedWidth(kPortWidth);
    host_port_->setAccessibleName(QStringLiteral("Port on this computer"));
    guest_port_ = new QLineEdit;
    guest_port_->setPlaceholderText(QStringLiteral("Same port"));
    guest_port_->setFixedWidth(kPortWidth);
    guest_port_->setAccessibleName(QStringLiteral("Port in Windows"));
    add_button_ = Widgets::button(QStringLiteral("Add"));
    form_row->addWidget(protocol_);
    form_row->addWidget(host_port_);
    form_row->addWidget(guest_port_);
    form_row->addWidget(fixed_cell(reach_, reach_width_));
    form_row->addWidget(add_button_);
    form_row->addStretch(1);
    column->addWidget(form);

    refusal_row_ = new QWidget;
    auto* refusal_line = new QHBoxLayout(refusal_row_);
    refusal_line->setContentsMargins(kSide, 0, kSide, 9);
    refusal_line->setSpacing(0);
    refusal_pad_ = new QWidget;
    refusal_ = Widgets::meta(QString{}, true);
    refusal_->setProperty(Theme::Tone::kProperty, Theme::Tone::kBad);
    refusal_line->addWidget(refusal_pad_);
    refusal_line->addWidget(refusal_, 1);
    refusal_row_->hide();
    column->addWidget(refusal_row_);

    connect(add_button_, &QPushButton::clicked, this, &PortForwardEditor::add_typed);
    connect(host_port_, &QLineEdit::returnPressed, this, &PortForwardEditor::add_typed);
    connect(guest_port_, &QLineEdit::returnPressed, this, &PortForwardEditor::add_typed);
}

void PortForwardEditor::build_notes(QVBoxLayout* column) {
    notes_ = new QWidget;
    auto* box = new QVBoxLayout(notes_);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(0);
    box->addWidget(Widgets::separator());

    auto* lines = new QVBoxLayout;
    lines->setContentsMargins(kSide, 8, kSide, 9);
    lines->setSpacing(6);

    undo_line_ = new QWidget;
    auto* undo_row = new QHBoxLayout(undo_line_);
    undo_row->setContentsMargins(0, 0, 0, 0);
    undo_row->setSpacing(8);
    undo_label_ = Widgets::meta(QString{});
    undo_button_ = Widgets::button(QStringLiteral("Undo"), Theme::Role::kQuiet);
    Widgets::compact(undo_button_);
    connect(undo_button_, &QPushButton::clicked, this, &PortForwardEditor::undo);
    undo_row->addWidget(undo_label_);
    undo_row->addWidget(undo_button_);
    undo_row->addStretch(1);
    lines->addWidget(undo_line_);

    reach_line_ = new QWidget;
    reach_flow_ = new ui::FlowLayout(reach_line_, ui::FlowLayout::Mode::Wrap, 14, 4);
    reach_flow_->setContentsMargins(0, 0, 0, 0);
    lines->addWidget(reach_line_);

    box->addLayout(lines);
    column->addWidget(notes_);
}

QVector<PortForward> PortForwardEditor::entries() const {
    return PortForwards::decode(pending_.staged_or_current(K::kPortForwards).toString());
}

bool PortForwardEditor::busy(const PortForward& entry) const {
    const bool held_by_guest =
        live_.running && std::any_of(live_.forwards.begin(), live_.forwards.end(),
                                     [&entry](const PortForward& active) {
                                         return active.protocol == entry.protocol &&
                                                active.host_port == entry.host_port;
                                     });
    if (held_by_guest) {
        return false;
    }
    const QString key = one(entry);
    const auto known = busy_.constFind(key);
    if (known != busy_.constEnd()) {
        return known.value();
    }
    const bool taken = !NetworkState::host_port_free(entry);
    busy_.insert(key, taken);
    return taken;
}

ForwardState PortForwardEditor::state_of(const PortForward& entry) const {
    return NetworkState::forward_state(entry, live_, !busy(entry));
}

void PortForwardEditor::stage(const QVector<PortForward>& entries) {
    staging_ = true;
    pending_.stage(K::kPortForwards, PortForwards::encode(entries));
    staging_ = false;
    rebuild();
    Q_EMIT changed();
}

void PortForwardEditor::say(const QString& refusal, Under under) {
    refusal_->setText(refusal);
    said_under_ = under;
    refusal_pad_->setFixedWidth(kProtocolWidth + kGap +
                                (under == Under::Guest ? kPortWidth + kGap : 0));
    refusal_row_->setVisible(!refusal.isEmpty());
}

QString PortForwardEditor::add(const PortForward& entry) {
    QVector<PortForward> now = entries();
    const QString refusal = PortForwards::refusal(now, entry);
    const bool guest_side = entry.host_port >= PortForwards::kLowestPort &&
                            entry.host_port <= PortForwards::kHighestPort &&
                            (entry.guest_port < PortForwards::kLowestPort ||
                             entry.guest_port > PortForwards::kHighestPort);
    say(refusal, guest_side ? Under::Guest : Under::Host);
    if (refusal.isEmpty()) {
        undo_.reset();
        offered_ = -1;
        preset_refusal_->hide();
        now.push_back(entry);
        stage(now);
    }
    return refusal;
}

QString PortForwardEditor::apply(const PortPreset& preset) {
    QStringList refusals;
    const QVector<PortForward> now = PortForwards::with(entries(), preset, &refusals);
    const QString said = refusals.isEmpty()
                             ? QString{}
                             : QStringLiteral("Not all ports of %1 were added. %2")
                                   .arg(preset.name, refusals.join(QLatin1Char(' ')));
    preset_refusal_->setText(said);
    preset_refusal_->setVisible(!said.isEmpty());
    say(QString{}, Under::Host);
    undo_.reset();
    offered_ = -1;
    stage(now);
    return said;
}

void PortForwardEditor::remove(const PortForward& entry) {
    const QVector<PortForward> before = entries();
    QVector<PortForward> now = before;
    now.removeAll(entry);
    if (now == before) {
        return;
    }
    say(QString{}, Under::Host);
    preset_refusal_->hide();
    undo_ = before;
    undo_text_ = QStringLiteral("Removed %1.").arg(full_text(entry));
    offered_ = -1;
    stage(now);
}

void PortForwardEditor::remove_preset(const PortPreset& preset) {
    const QVector<PortForward> before = entries();
    const QVector<PortForward> now = PortForwards::without(before, preset);
    if (now == before) {
        return;
    }
    say(QString{}, Under::Host);
    preset_refusal_->hide();
    undo_ = before;
    const auto gone = before.size() - now.size();
    undo_text_ = gone == 1
                     ? QStringLiteral("Removed the port of %1.").arg(preset.name)
                     : QStringLiteral("Removed the %1 ports of %2.").arg(gone).arg(preset.name);
    offered_ = -1;
    stage(now);
}

void PortForwardEditor::set_reach(const PortForward& entry, bool from_network) {
    QVector<PortForward> now = entries();
    const auto at = now.indexOf(entry);
    if (at < 0 || entry.from_network == from_network) {
        return;
    }
    now[at].from_network = from_network;
    undo_.reset();
    offered_ = -1;
    stage(now);
}

void PortForwardEditor::undo() {
    if (!undo_) {
        return;
    }
    const QVector<PortForward> back = *undo_;
    undo_.reset();
    const bool focused = undo_button_->hasFocus();
    stage(back);
    if (focused) {
        host_port_->setFocus();
    }
}

void PortForwardEditor::set_live(const NetworkState::Live& live) {
    if (live == live_) {
        return;
    }
    live_ = live;
    rebuild();
}

void PortForwardEditor::set_host_addresses(const QVector<HostAddresses::Address>& addresses) {
    if (addresses == addresses_) {
        return;
    }
    addresses_ = addresses;
    show_notes(entries());
    order_tabs();
}

void PortForwardEditor::check_ports() {
    busy_.clear();
    rebuild();
}

void PortForwardEditor::add_typed() {
    PortForward entry;
    entry.protocol =
        protocol_->currentIndex() == 1 ? PortForward::Protocol::Udp : PortForward::Protocol::Tcp;
    entry.host_port = host_port_->text().trimmed().toInt();
    const QString guest = guest_port_->text().trimmed();
    entry.guest_port = guest.isEmpty() ? entry.host_port : guest.toInt();
    entry.from_network = reach_->current().toBool();
    if (add(entry).isEmpty()) {
        host_port_->clear();
        guest_port_->clear();
        host_port_->setFocus();
        return;
    }
    QLineEdit* wrong = said_under_ == Under::Guest ? guest_port_ : host_port_;
    wrong->setFocus();
    wrong->selectAll();
}

QWidget* PortForwardEditor::row_for(const PortForward& entry, ForwardState state) {
    auto* holder = new QWidget;
    holder->setProperty(kForward, row_key(entry));
    auto* box = new QVBoxLayout(holder);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(0);

    auto* line = new QHBoxLayout;
    line->setContentsMargins(kSide, 6, kSide, 6);
    line->setSpacing(kGap);
    auto* chip = new QLabel(PortForwards::protocol_name(entry.protocol));
    chip->setObjectName(QString::fromLatin1(Theme::Role::kStepChip));
    line->addWidget(fixed_cell(chip, kProtocolWidth));
    line->addWidget(number(entry.host_port));
    line->addWidget(number(entry.guest_port));

    auto* reach = new ui::SegmentedControl;
    reach->set_options(reach_options());
    reach->set_current(entry.from_network);
    reach->setAccessibleName(QStringLiteral("Who can reach %1").arg(row_key(entry)));
    connect(
        reach, &ui::SegmentedControl::chosen, this,
        [this, entry](const QVariant& value) { set_reach(entry, value.toBool()); },
        Qt::QueuedConnection);
    line->addWidget(fixed_cell(reach, reach_width_));

    line->addWidget(pill_for(state));
    line->addStretch(1);
    auto* remove_button = Widgets::button(QStringLiteral("×"), Theme::Role::kIconButton);
    remove_button->setToolTip(QStringLiteral("Stop forwarding this port"));
    remove_button->setAccessibleName(QStringLiteral("Stop forwarding %1").arg(row_key(entry)));
    remove_button->setCursor(Qt::PointingHandCursor);
    connect(
        remove_button, &QPushButton::clicked, this, [this, entry] { remove(entry); },
        Qt::QueuedConnection);
    line->addWidget(remove_button);
    box->addLayout(line);

    if (state == ForwardState::PortInUse) {
        auto* note = Widgets::meta(
            QStringLiteral(
                "%1 %2 is in use on this computer; pick another port or the guest will not start")
                .arg(PortForwards::protocol_name(entry.protocol))
                .arg(entry.host_port),
            true);
        note->setProperty(Theme::Tone::kProperty, Theme::Tone::kBad);
        note->setContentsMargins(kSide, 0, kSide, 8);
        box->addWidget(note);
    }
    return holder;
}

void PortForwardEditor::rebuild() {
    const QVector<PortForward> now = entries();
    QStringList signature{QString::number(live_.running)};
    QVector<ForwardState> states;
    for (const PortForward& entry : now) {
        states.push_back(state_of(entry));
        signature << one(entry) << QString::number(static_cast<int>(states.back()));
    }
    shown_ = now;
    if (signature != rows_signature_) {
        rows_signature_ = signature;
        QString focus_key;
        bool had_focus = false;
        if (QWidget* focused = QApplication::focusWidget();
            focused != nullptr && rows_->isAncestorOf(focused)) {
            had_focus = true;
            for (QWidget* at = focused; at != nullptr && at != rows_; at = at->parentWidget()) {
                if (at->property(kForward).isValid()) {
                    focus_key = at->property(kForward).toString();
                    break;
                }
            }
        }
        clear(rows_box_);
        QWidget* refocus = nullptr;
        for (qsizetype i = 0; i < now.size(); ++i) {
            QWidget* row = row_for(now.at(i), states.at(i));
            rows_box_->addWidget(row);
            if (i + 1 < now.size()) {
                auto* rule = Widgets::separator();
                rule->setProperty("soft", true);
                rows_box_->addWidget(rule);
            }
            if (had_focus && row->property(kForward).toString() == focus_key) {
                for (QAbstractButton* button : row->findChildren<QAbstractButton*>()) {
                    if (button->isChecked()) {
                        refocus = button;
                    }
                }
            }
        }
        if (had_focus) {
            if (refocus != nullptr) {
                refocus->setFocus();
            } else if (undo_) {
                undo_button_->setFocus();
            } else {
                host_port_->setFocus();
            }
        }
    }
    rows_->setVisible(!now.isEmpty());
    empty_->setVisible(now.isEmpty());
    show_presets(now);
    show_notes(now);
    order_tabs();
}

void PortForwardEditor::show_presets(const QVector<PortForward>& now) {
    const QVector<PortPreset>& presets = PortForwards::presets();
    for (int i = 0; i < presets.size(); ++i) {
        const PortPreset& preset = presets.at(i);
        const bool applied = PortForwards::applied(now, preset);
        QPushButton* chip = chips_.at(i);
        chip->setText(applied ? QStringLiteral("✓ %1").arg(preset.name) : preset.name);
        chip->setToolTip(
            QStringLiteral("%1\n%2%3")
                .arg(preset.description, PortForwards::mapping(preset),
                     applied ? QStringLiteral("\nAdded. Click to remove its ports.") : QString{}));
        if (chip->property("applied").toBool() != applied) {
            Widgets::set_flag(chip, "applied", applied);
        }
        if (offered_ == i && !applied) {
            offered_ = -1;
        }
    }
    offer_->setVisible(offered_ >= 0);
    if (offered_ >= 0) {
        const PortPreset& preset = presets.at(offered_);
        offer_text_->setText(QStringLiteral("%1 is added.").arg(preset.name));
        offer_remove_->setText(
            preset.ports.size() == 1
                ? QStringLiteral("Remove its port")
                : QStringLiteral("Remove its %1 ports").arg(preset.ports.size()));
    }
}

void PortForwardEditor::show_notes(const QVector<PortForward>& now) {
    undo_line_->setVisible(undo_.has_value());
    undo_label_->setText(undo_text_);

    const bool open = std::any_of(now.begin(), now.end(),
                                  [](const PortForward& entry) { return entry.from_network; });
    reach_line_->setVisible(open);
    QStringList signature{QString::number(open)};
    for (const HostAddresses::Address& address : addresses_) {
        signature << address.address << address.interface;
    }
    if (open && signature != reach_signature_) {
        clear(reach_flow_);
        if (addresses_.isEmpty()) {
            reach_flow_->addWidget(Widgets::meta(QStringLiteral(
                "This computer has no network address now, so other machines cannot reach "
                "these ports.")));
        }
        bool first = true;
        for (const HostAddresses::Address& address : addresses_) {
            auto* cell = new QWidget;
            auto* row = new QHBoxLayout(cell);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(7);
            if (first) {
                row->addWidget(Widgets::meta(QStringLiteral("Other machines connect to")));
                row->addSpacing(3);
                first = false;
            }
            auto* shown = new QLabel(address.address);
            shown->setObjectName(QString::fromLatin1(Theme::Role::kMono));
            shown->setTextInteractionFlags(Qt::TextSelectableByMouse);
            row->addWidget(shown);
            row->addWidget(Widgets::meta(address.interface));
            auto* copy = Widgets::button(QStringLiteral("Copy"), Theme::Role::kQuiet);
            Widgets::compact(copy);
            copy->setAccessibleName(QStringLiteral("Copy %1").arg(address.address));
            connect(copy, &QPushButton::clicked, this,
                    [text = address.address] { QApplication::clipboard()->setText(text); });
            row->addWidget(copy);
            reach_flow_->addWidget(cell);
        }
    }
    reach_signature_ = signature;
    notes_->setVisible(undo_.has_value() || open);
}

void PortForwardEditor::order_tabs() {
    QVector<QWidget*> order;
    for (QPushButton* chip : std::as_const(chips_)) {
        order.push_back(chip);
    }
    order << offer_remove_ << offer_keep_;
    for (QAbstractButton* button : rows_->findChildren<QAbstractButton*>()) {
        if (button->isVisibleTo(rows_)) {
            order.push_back(button);
        }
    }
    order << protocol_ << host_port_ << guest_port_;
    for (QAbstractButton* button : reach_->findChildren<QAbstractButton*>()) {
        order.push_back(button);
    }
    order << add_button_ << undo_button_;
    for (QAbstractButton* button : reach_line_->findChildren<QAbstractButton*>()) {
        order.push_back(button);
    }
    for (qsizetype i = 0; i + 1 < order.size(); ++i) {
        QWidget::setTabOrder(order.at(i), order.at(i + 1));
    }
}

}  // namespace lsw::host::manager
