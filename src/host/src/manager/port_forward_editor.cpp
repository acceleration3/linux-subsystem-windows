#include "lsw/host/manager/port_forward_editor.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using config::PortForward;
using config::PortForwards;
using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;

constexpr int kPortWidth = 150;

QString reach_text(bool from_network) {
    return from_network ? QStringLiteral("Reachable from the network")
                        : QStringLiteral("Reachable from this computer only");
}

}

PortForwardEditor::PortForwardEditor(config::ManagerSettings& settings,
                                     config::PendingChanges& pending, QWidget* parent)
    : QWidget(parent), pending_(pending) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(12, 10, 12, 12);
    column->setSpacing(8);

    rows_ = new QVBoxLayout;
    rows_->setSpacing(4);
    column->addLayout(rows_);
    empty_ = Widgets::meta(QStringLiteral("No port is forwarded besides LSW's own."), true);
    column->addWidget(empty_);

    auto* form = new QHBoxLayout;
    form->setSpacing(8);
    protocol_ = new QComboBox;
    protocol_->addItem(PortForwards::protocol_name(PortForward::Protocol::Tcp));
    protocol_->addItem(PortForwards::protocol_name(PortForward::Protocol::Udp));
    host_port_ = new QLineEdit;
    host_port_->setPlaceholderText(QStringLiteral("Host port"));
    host_port_->setFixedWidth(kPortWidth);
    guest_port_ = new QLineEdit;
    guest_port_->setPlaceholderText(QStringLiteral("Port in Windows (same if empty)"));
    guest_port_->setMinimumWidth(kPortWidth);
    reach_ = new QComboBox;
    reach_->addItem(QStringLiteral("This computer only"), false);
    reach_->addItem(QStringLiteral("The network"), true);
    reach_->setToolTip(QStringLiteral("Who can connect to the port on this computer"));
    auto* add_button = Widgets::button(QStringLiteral("Add"));
    form->addWidget(protocol_);
    form->addWidget(host_port_);
    form->addWidget(new QLabel(QStringLiteral("→")));
    form->addWidget(guest_port_, 1);
    form->addWidget(reach_);
    form->addWidget(add_button);
    column->addLayout(form);

    refusal_ = Widgets::meta(QString{}, true);
    refusal_->setProperty(Theme::Tone::kProperty, Theme::Tone::kBad);
    refusal_->hide();
    column->addWidget(refusal_);

    connect(add_button, &QPushButton::clicked, this, &PortForwardEditor::add_typed);
    connect(host_port_, &QLineEdit::returnPressed, this, &PortForwardEditor::add_typed);
    connect(guest_port_, &QLineEdit::returnPressed, this, &PortForwardEditor::add_typed);
    connect(&pending_, &config::PendingChanges::changed, this, &PortForwardEditor::rebuild);
    connect(&settings, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kPortForwards) {
            rebuild();
        }
    });
    rebuild();
}

QVector<PortForward> PortForwardEditor::entries() const {
    return PortForwards::decode(pending_.staged_or_current(K::kPortForwards).toString());
}

void PortForwardEditor::stage(const QVector<PortForward>& entries) {
    pending_.stage(K::kPortForwards, PortForwards::encode(entries));
    rebuild();
}

void PortForwardEditor::say(const QString& refusal) {
    refusal_->setText(refusal);
    refusal_->setVisible(!refusal.isEmpty());
}

QString PortForwardEditor::add(const PortForward& entry) {
    QVector<PortForward> now = entries();
    const QString refusal = PortForwards::refusal(now, entry);
    say(refusal);
    if (refusal.isEmpty()) {
        now.push_back(entry);
        stage(now);
    }
    return refusal;
}

QString PortForwardEditor::add_sunshine() {
    QVector<PortForward> now = entries();
    QStringList refusals;
    for (const PortForward& entry : PortForwards::sunshine()) {
        if (now.contains(entry)) {
            continue;
        }
        const QString refusal = PortForwards::refusal(now, entry);
        if (refusal.isEmpty()) {
            now.push_back(entry);
        } else {
            refusals << refusal;
        }
    }
    const QString said = refusals.isEmpty()
                             ? QString{}
                             : QStringLiteral("Not all Sunshine ports were added. %1")
                                   .arg(refusals.join(QLatin1Char(' ')));
    stage(now);
    say(said);
    return said;
}

void PortForwardEditor::remove(const PortForward& entry) {
    QVector<PortForward> now = entries();
    now.removeAll(entry);
    say(QString{});
    stage(now);
}

void PortForwardEditor::add_typed() {
    PortForward entry;
    entry.protocol = protocol_->currentIndex() == 1 ? PortForward::Protocol::Udp
                                                    : PortForward::Protocol::Tcp;
    entry.host_port = host_port_->text().trimmed().toInt();
    const QString guest = guest_port_->text().trimmed();
    entry.guest_port = guest.isEmpty() ? entry.host_port : guest.toInt();
    entry.from_network = reach_->currentData().toBool();
    if (add(entry).isEmpty()) {
        host_port_->clear();
        guest_port_->clear();
    }
}

void PortForwardEditor::rebuild() {
    while (QLayoutItem* item = rows_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const QVector<PortForward> now = entries();
    for (const PortForward& entry : now) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        auto* ports = new QLabel(QStringLiteral("%1 %2 → %3")
                                     .arg(PortForwards::protocol_name(entry.protocol))
                                     .arg(entry.host_port)
                                     .arg(entry.guest_port));
        ports->setObjectName(QString::fromLatin1(Theme::Role::kMono));
        layout->addWidget(ports);
        layout->addWidget(Widgets::meta(reach_text(entry.from_network)));
        layout->addStretch(1);
        auto* remove_button = Widgets::button(QStringLiteral("×"), Theme::Role::kIconButton);
        remove_button->setToolTip(QStringLiteral("Stop forwarding this port"));
        remove_button->setCursor(Qt::PointingHandCursor);
        connect(remove_button, &QPushButton::clicked, this, [this, entry] { remove(entry); },
                Qt::QueuedConnection);
        layout->addWidget(remove_button);
        rows_->addWidget(row);
    }
    empty_->setVisible(now.isEmpty());
}

}
