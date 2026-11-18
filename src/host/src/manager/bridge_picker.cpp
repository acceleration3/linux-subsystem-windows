#include "lsw/host/manager/bridge_picker.hpp"

#include "lsw/host/ui/item_list.hpp"
#include "lsw/host/ui/item_row.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <algorithm>

namespace lsw::host::manager {

namespace {

using platform::HostAddresses;
using platform::HostBridges;
using ui::StatusPill;
using ui::Theme;
using ui::Widgets;
using State = HostBridges::Requirement::State;

constexpr int kMarkWidth = 16;

QLabel* state_pill(const QString& state) {
    if (state == QLatin1StringView("up")) {
        return StatusPill::make(StatusPill::State::Done, QStringLiteral("Up"));
    }
    if (state == QLatin1StringView("down")) {
        return StatusPill::make(StatusPill::State::Pending, QStringLiteral("Down"));
    }
    return StatusPill::make(StatusPill::State::Pending,
                            state.isEmpty() ? QStringLiteral("State unknown") : state);
}

QLabel* mark(State state) {
    auto* label = new QLabel(state == State::Met     ? QStringLiteral("✓")
                             : state == State::Unmet ? QStringLiteral("✗")
                                                     : QStringLiteral("–"));
    label->setFont(Theme::mono_font(13));
    label->setFixedWidth(kMarkWidth);
    label->setProperty(Theme::Tone::kProperty, state == State::Met     ? Theme::Tone::kOk
                                               : state == State::Unmet ? Theme::Tone::kBad
                                                                       : Theme::Tone::kFaint);
    return label;
}

}  // namespace

BridgePicker::BridgePicker(QWidget* parent) : QWidget(parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    none_ = Widgets::empty_state(
        QStringLiteral("No bridge on this computer"),
        QStringLiteral("A bridge is a virtual switch on this computer that puts machines on one "
                       "of its networks. One must exist here before Windows can join it, and LSW "
                       "does not create bridges."));
    none_->setContentsMargins(12, 12, 12, 12);
    column->addWidget(none_);

    list_ = new ui::ItemList;
    column->addWidget(list_);

    checks_ = new QWidget;
    checks_box_ = new QVBoxLayout(checks_);
    checks_box_->setContentsMargins(12, 11, 12, 12);
    checks_box_->setSpacing(8);
    rule_ = Widgets::separator();
    column->addWidget(rule_);
    column->addWidget(checks_);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
}

QString BridgePicker::facts_line(const HostBridges::Bridge& bridge, const QStringList& addresses) {
    const QString members = bridge.members == 0 ? QStringLiteral("no interface")
                            : bridge.members == 1
                                ? QStringLiteral("1 interface")
                                : QStringLiteral("%1 interfaces").arg(bridge.members);
    return QStringLiteral("%1 · %2").arg(addresses.isEmpty() ? QStringLiteral("no address")
                                                             : addresses.join(QStringLiteral(", ")),
                                         members);
}

void BridgePicker::show_state(const HostBridges::Facts& facts,
                              const QVector<HostAddresses::Interface>& interfaces,
                              const QString& picked) {
    const bool nothing = facts.bridges.isEmpty() && picked.isEmpty();
    none_->setVisible(nothing);
    list_->setVisible(!nothing);
    rule_->setVisible(!nothing);
    checks_->setVisible(!nothing);
    rebuild_rows(facts, interfaces, picked);
    rebuild_checks(facts, picked);
}

void BridgePicker::rebuild_rows(const HostBridges::Facts& facts,
                                const QVector<HostAddresses::Interface>& interfaces,
                                const QString& picked) {
    QVector<HostBridges::Bridge> bridges = facts.details;
    for (const QString& name : facts.bridges) {
        const bool known =
            std::any_of(bridges.begin(), bridges.end(),
                        [&name](const HostBridges::Bridge& b) { return b.name == name; });
        if (!known) {
            bridges.push_back({name, QString{}, 0});
        }
    }
    const bool absent = !picked.isEmpty() && !facts.bridges.contains(picked);
    QStringList signature{QString::number(absent)};
    for (const HostBridges::Bridge& bridge : bridges) {
        signature << bridge.name << bridge.state << QString::number(bridge.members)
                  << HostAddresses::of(interfaces, bridge.name).join(QLatin1Char(' '));
    }
    if (absent) {
        signature << picked;
    }
    if (signature != rows_signature_) {
        rows_signature_ = signature;
        for (QRadioButton* radio : std::as_const(radios_)) {
            group_->removeButton(radio);
        }
        radios_.clear();
        list_->clear();
        const auto add = [this](const QString& name, const QString& sub, QLabel* pill,
                                const QString& note) {
            auto* row = new ui::ItemRow(ui::LineIcon::Shape::Network);
            row->set_name(name);
            row->set_sub(sub);
            if (!note.isEmpty()) {
                row->set_note(note, Theme::Tone::kBad);
            }
            row->trailing()->addWidget(pill);
            auto* radio = new QRadioButton(QStringLiteral("Join"));
            radio->setAutoExclusive(false);
            radio->setAccessibleName(QStringLiteral("Join %1").arg(name));
            connect(radio, &QRadioButton::clicked, this, [this, name] { Q_EMIT chosen(name); });
            group_->addButton(radio);
            radios_.insert(name, radio);
            row->trailing()->addWidget(radio);
            list_->add(row);
        };
        for (const HostBridges::Bridge& bridge : bridges) {
            add(bridge.name, facts_line(bridge, HostAddresses::of(interfaces, bridge.name)),
                state_pill(bridge.state), QString{});
        }
        if (absent) {
            add(picked, QString{},
                StatusPill::make(StatusPill::State::Failed, QStringLiteral("Missing")),
                QStringLiteral("The picked bridge is not on this computer."));
        }
    }
    group_->setExclusive(false);
    for (auto it = radios_.constBegin(); it != radios_.constEnd(); ++it) {
        it.value()->setChecked(it.key() == picked);
    }
    group_->setExclusive(true);
}

void BridgePicker::rebuild_checks(const HostBridges::Facts& facts, const QString& picked) {
    const QVector<HostBridges::Requirement> lines = HostBridges::readiness(facts, picked);
    QStringList signature{picked};
    for (const HostBridges::Requirement& line : lines) {
        signature << QString::number(static_cast<int>(line.state)) << line.text << line.detail
                  << line.fix;
    }
    if (signature == checks_signature_ && checks_box_->count() > 0) {
        return;
    }
    checks_signature_ = signature;
    while (QLayoutItem* item = checks_box_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    if (picked.isEmpty()) {
        checks_box_->addWidget(Widgets::meta(QStringLiteral("Pick a bridge to check it"), true));
        return;
    }
    for (const HostBridges::Requirement& line : lines) {
        auto* holder = new QWidget;
        auto* row = new QHBoxLayout(holder);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(9);
        row->addWidget(mark(line.state), 0, Qt::AlignTop);

        auto* text = new QVBoxLayout;
        text->setSpacing(5);
        auto* head = new QHBoxLayout;
        head->setSpacing(8);
        auto* said = new QLabel(line.text);
        if (line.state == State::Unknown) {
            said->setObjectName(QString::fromLatin1(Theme::Role::kDim));
        }
        head->addWidget(said);
        if (line.fix.isEmpty() && !line.detail.isEmpty()) {
            head->addWidget(Widgets::meta(line.detail, true), 1);
        } else {
            head->addStretch(1);
        }
        text->addLayout(head);
        if (!line.fix.isEmpty()) {
            auto* fix = new QHBoxLayout;
            fix->setSpacing(10);
            fix->addWidget(Widgets::meta(line.detail));
            auto* code = new QLabel(line.fix);
            code->setObjectName(QString::fromLatin1(Theme::Role::kCode));
            code->setTextInteractionFlags(Qt::TextSelectableByMouse);
            fix->addWidget(code);
            auto* copy = Widgets::button(QStringLiteral("Copy"), Theme::Role::kQuiet);
            Widgets::compact(copy);
            copy->setAccessibleName(QStringLiteral("Copy %1").arg(line.fix));
            connect(copy, &QPushButton::clicked, this,
                    [fixed = line.fix] { QApplication::clipboard()->setText(fixed); });
            fix->addWidget(copy);
            fix->addStretch(1);
            text->addLayout(fix);
        }
        row->addLayout(text, 1);
        checks_box_->addWidget(holder);
    }
    const bool ready =
        std::all_of(lines.begin(), lines.end(),
                    [](const HostBridges::Requirement& line) { return line.state == State::Met; });
    if (!ready) {
        checks_box_->addWidget(Widgets::meta(
            QStringLiteral("Windows starts on the shared network until every line is met"), true));
    }
}

}  // namespace lsw::host::manager
