#include "lsw/host/manager/network_page.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/port_forwards.hpp"
#include "lsw/host/manager/bridge_picker.hpp"
#include "lsw/host/manager/port_forward_editor.hpp"
#include "lsw/host/manager/setting_controls.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/choice_card.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <utility>

namespace lsw::host::manager {

namespace {

using config::PortForward;
using config::PortForwards;
using platform::HostAddresses;
using platform::HostBridges;
using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;
using Mode = config::ManagerSettings::NetworkMode;
using Area = services::StatusHub::Area;

}  // namespace

NetworkPage::Host NetworkPage::Host::system() {
    return {[] { return HostBridges::probe(); }, [] { return HostAddresses::system(); }};
}

NetworkPage::NetworkPage(services::StatusHub& status, config::ManagerSettings& settings,
                         config::PendingChanges& pending, Host host, QWidget* parent)
    : ui::ScrollPage(parent), status_(status), settings_(settings), pending_(pending),
      host_(std::move(host)), controls_(new SettingControls(settings, pending, this)) {
    build_connection();
    build_bridge();
    build_forwards();
    column()->addStretch(1);

    controls_->follow(K::kNetworkMode, this, [this] { refresh(); });
    controls_->follow(K::kNetworkBridge, this, [this] { refresh(); });
    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area != Area::Vm) {
            return;
        }
        const NetworkState::Live before = seen_;
        refresh();
        if (isVisible() && !(seen_ == before)) {
            forwards_->check_ports();
        }
    });
    connect(forwards_, &PortForwardEditor::changed, this, &NetworkPage::refresh);
    read_host();
}

void NetworkPage::showEvent(QShowEvent* event) {
    ui::ScrollPage::showEvent(event);
    forwards_->check_ports();
    read_host();
}

void NetworkPage::read_host() {
    facts_ = host_.bridges();
    interfaces_ = host_.interfaces();
    refresh();
}

int NetworkPage::waiting() const {
    int count = 0;
    for (const QString& key : pending_.keys()) {
        if (key == K::kNetworkMode || key == K::kNetworkBridge || key == K::kPortForwards) {
            ++count;
        }
    }
    return count;
}

QString NetworkPage::summary() const {
    const bool bridged = settings_.string_value(K::kNetworkMode) == Mode::kBridged;
    const QString bridge = settings_.string_value(K::kNetworkBridge);
    const QString next_bridge =
        bridged && HostBridges::missing(facts_, bridge).isEmpty() ? bridge : QString{};
    const auto next_forwards =
        PortForwards::accepted(PortForwards::decode(settings_.string_value(K::kPortForwards)))
            .size();
    return NetworkState::summary(NetworkState::live(status_.vm()), next_bridge,
                                 static_cast<int>(next_forwards), waiting());
}

void NetworkPage::build_connection() {
    auto panel = ui::Panel::area(QStringLiteral("How Windows is connected"),
                                 QStringLiteral("the network Windows starts with"));
    panel.header->addWidget(Widgets::tag(true));

    auto* cards = new QHBoxLayout;
    cards->setSpacing(10);
    shared_ = new ui::ChoiceCard(
        QStringLiteral("Shared with this computer"),
        QStringLiteral("Windows reaches the network through this computer. Other machines reach "
                       "Windows only through the forwarded ports."));
    bridged_ = new ui::ChoiceCard(
        QStringLiteral("Bridged to a host bridge"),
        QStringLiteral("Windows appears on the network as a machine of its own, with its own "
                       "address."));
    connect(shared_, &ui::ChoiceCard::chosen, this,
            [this] { choose_mode(QString{Mode::kShared}); });
    connect(bridged_, &ui::ChoiceCard::chosen, this,
            [this] { choose_mode(QString{Mode::kBridged}); });
    cards->addWidget(shared_, 1);
    cards->addWidget(bridged_, 1);
    panel.body->addLayout(cards);

    off_ =
        Widgets::meta(QStringLiteral("The guest is off, so Windows is not connected now."), true);
    panel.body->addWidget(off_);

    now_ = new QWidget;
    auto* grid = new QGridLayout(now_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);
    grid->addWidget(Widgets::caption(QStringLiteral("Adapters")), 0, 0,
                    Qt::AlignLeft | Qt::AlignVCenter);
    adapters_ = new QLabel;
    grid->addWidget(adapters_, 0, 1);
    grid->addWidget(Widgets::caption(QStringLiteral("Windows address")), 1, 0,
                    Qt::AlignLeft | Qt::AlignVCenter);
    auto* address_row = new QHBoxLayout;
    address_row->setSpacing(10);
    address_ = new QLabel;
    address_->setObjectName(QString::fromLatin1(Theme::Role::kMono));
    address_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* copy = Widgets::button(QStringLiteral("Copy"), Theme::Role::kQuiet);
    Widgets::compact(copy);
    copy->setAccessibleName(QStringLiteral("Copy the Windows address"));
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(address_->text()); });
    address_copy_ = copy;
    address_note_ = Widgets::meta(QString{});
    address_row->addWidget(address_);
    address_row->addWidget(copy);
    address_row->addWidget(address_note_);
    address_row->addStretch(1);
    grid->addLayout(address_row, 1, 1);
    panel.body->addWidget(now_);

    not_bridged_ = Widgets::prose(QString{});
    not_bridged_->setProperty(Theme::Tone::kProperty, Theme::Tone::kWarm);
    panel.body->addWidget(not_bridged_);

    column()->addWidget(panel.frame);
}

void NetworkPage::build_bridge() {
    auto panel = ui::Panel::area(QStringLiteral("Bridge"),
                                 QStringLiteral("the bridge of this computer Windows joins"));
    panel.header->addWidget(Widgets::tag(true));
    panel.body->setContentsMargins(0, 0, 0, 0);
    panel.body->setSpacing(0);
    picker_ = new BridgePicker;
    connect(picker_, &BridgePicker::chosen, this,
            [this](const QString& bridge) { controls_->put(K::kNetworkBridge, bridge); });
    panel.body->addWidget(picker_);
    panel.add_foot(QStringLiteral("LSW changes nothing on this computer: the bridge, QEMU's helper "
                                  "and its rules are set up outside it."));
    bridge_area_ = panel.frame;
    column()->addWidget(panel.frame);
}

void NetworkPage::build_forwards() {
    auto panel = ui::Panel::area(QStringLiteral("Forwarded ports"),
                                 QStringLiteral("a port of this computer that leads to a port in "
                                                "Windows"));
    panel.header->addWidget(Widgets::tag(true));
    forward_count_ = Widgets::meta(QString{});
    panel.header->addWidget(forward_count_);
    panel.body->setContentsMargins(0, 0, 0, 0);
    panel.body->setSpacing(0);
    forwards_ = new PortForwardEditor(settings_, pending_);
    panel.body->addWidget(forwards_);

    QStringList own;
    for (const PortForward& forward : PortForwards::own()) {
        own << QString::number(forward.host_port);
    }
    QString foot = QStringLiteral("LSW itself uses TCP %1.").arg(own.join(QStringLiteral(", ")));
    const int first = PortForwards::first_unprivileged_port();
    if (first > PortForwards::kLowestPort) {
        foot += QStringLiteral(" This computer lets only the administrator listen on ports below "
                               "%1, so those cannot be forwarded.")
                    .arg(first);
    }
    panel.add_foot(foot);
    column()->addWidget(panel.frame);
}

void NetworkPage::choose_mode(const QString& mode) {
    controls_->put(K::kNetworkMode, mode);
}

void NetworkPage::refresh() {
    const NetworkState::Live live = NetworkState::live(status_.vm());
    seen_ = live;
    const QString staged_mode = controls_->current(K::kNetworkMode).toString();
    const bool bridged = staged_mode == Mode::kBridged;
    const bool mode_waits = pending_.keys().contains(K::kNetworkMode);

    shared_->set_selected(!bridged);
    bridged_->set_selected(bridged);
    shared_->set_in_use(live.known && live.bridges.isEmpty());
    bridged_->set_in_use(live.known && !live.bridges.isEmpty());
    shared_->set_waiting(mode_waits && !bridged);
    bridged_->set_waiting(mode_waits && bridged);

    refresh_now(live);

    bridge_area_->setVisible(bridged);
    picker_->show_state(facts_, interfaces_, controls_->current(K::kNetworkBridge).toString());

    forwards_->set_live(live);
    forwards_->set_host_addresses(HostAddresses::reachable(interfaces_));
    order_tabs();
    const auto count = forwards_->entries().size();
    forward_count_->setText(count == 0   ? QStringLiteral("no port")
                            : count == 1 ? QStringLiteral("1 port")
                                         : QStringLiteral("%1 ports").arg(count));
    Q_EMIT summary_changed();
}

void NetworkPage::order_tabs() {
    QWidget* previous = address_copy_;
    QWidget::setTabOrder(shared_, bridged_);
    QWidget::setTabOrder(bridged_, previous);
    for (QAbstractButton* button : picker_->findChildren<QAbstractButton*>()) {
        if (button->isVisibleTo(picker_)) {
            QWidget::setTabOrder(previous, button);
            previous = button;
        }
    }
    if (QWidget* first = forwards_->findChild<QAbstractButton*>()) {
        QWidget::setTabOrder(previous, first);
    }
}

void NetworkPage::refresh_now(const NetworkState::Live& live) {
    off_->setVisible(!live.running);
    now_->setVisible(live.known);
    if (live.known) {
        QStringList adapters{QStringLiteral("Shared adapter")};
        for (const QString& bridge : live.bridges) {
            adapters << QStringLiteral("Bridged adapter on %1").arg(bridge);
        }
        adapters_->setText(adapters.join(QStringLiteral(" · ")));
        const bool reported = !live.guest_ipv4.isEmpty();
        address_->setText(live.guest_ipv4);
        address_->setVisible(reported);
        address_copy_->setVisible(reported);
        address_note_->setText(reported ? QStringLiteral("as Windows reported when it started")
                                        : QStringLiteral("Windows has not reported one yet"));
    }
    const bool stored_bridged = settings_.string_value(K::kNetworkMode) == Mode::kBridged;
    const bool refused = live.known && stored_bridged && live.bridges.isEmpty();
    not_bridged_->setVisible(refused);
    if (refused) {
        not_bridged_->setText(QStringLiteral("Bridged is not in effect: Windows started with the "
                                             "shared network only."));
    }
}

}  // namespace lsw::host::manager
