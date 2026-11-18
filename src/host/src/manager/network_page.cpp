#include "lsw/host/manager/network_page.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/port_forwards.hpp"
#include "lsw/host/manager/bridge_picker.hpp"
#include "lsw/host/manager/port_forward_editor.hpp"
#include "lsw/host/manager/setting_controls.hpp"
#include "lsw/host/services/network_adapter_offer.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/choice_card.hpp"
#include "lsw/host/ui/info_tip.hpp"
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
using Adapter = config::ManagerSettings::NetworkAdapter;
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
    build_adapter();
    build_bridge();
    build_forwards();
    column()->addStretch(1);

    controls_->follow(K::kNetworkMode, this, [this] { refresh(); });
    controls_->follow(K::kNetworkBridge, this, [this] { refresh(); });
    controls_->follow(K::kNetworkAdapter, this, [this] { refresh(); });
    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area == Area::Update) {
            refresh();
            return;
        }
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
        if (key == K::kNetworkMode || key == K::kNetworkBridge || key == K::kPortForwards ||
            key == K::kNetworkAdapter) {
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
    auto panel = ui::Panel::area(QStringLiteral("Connection"),
                                 QStringLiteral("How Windows reaches the network, and how other "
                                                "machines reach Windows."));
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

    off_ = Widgets::meta(QStringLiteral("Not connected: the guest is off"));
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
    auto* address_key = new QHBoxLayout;
    address_key->setSpacing(6);
    address_key->addWidget(Widgets::caption(QStringLiteral("Windows address")));
    address_key->addWidget(
        new ui::InfoTip(QStringLiteral("Windows's own network address, as Windows reported it "
                                       "when it started.")));
    address_key->addStretch(1);
    grid->addLayout(address_key, 1, 0);
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
    address_missing_ = Widgets::meta(QStringLiteral("-"));
    address_row->addWidget(address_);
    address_row->addWidget(address_missing_);
    address_row->addWidget(copy);
    address_row->addStretch(1);
    grid->addLayout(address_row, 1, 1);
    panel.body->addWidget(now_);

    not_bridged_ = Widgets::prose(QString{});
    not_bridged_->setProperty(Theme::Tone::kProperty, Theme::Tone::kWarm);
    panel.body->addWidget(not_bridged_);

    column()->addWidget(panel.frame);
}

void NetworkPage::build_adapter() {
    auto panel =
        ui::Panel::area(QStringLiteral("Network adapter"),
                        QStringLiteral("The virtual network card Windows uses for the shared "
                                       "connection."));
    panel.header->addWidget(Widgets::tag(true));

    auto* cards = new QHBoxLayout;
    cards->setSpacing(10);
    virtio_ = new ui::ChoiceCard(
        QStringLiteral("Virtio on passt"),
        QStringLiteral("The faster network card. Windows needs its virtio network driver, which "
                       "the guest update installs."));
    emulated_ = new ui::ChoiceCard(
        QStringLiteral("Emulated Intel e1000e"),
        QStringLiteral(
            "An emulated Intel network card that Windows has a driver for out of the box. "
            "Slower; Windows Setup always uses it."));
    connect(virtio_, &ui::ChoiceCard::chosen, this,
            [this] { controls_->put(K::kNetworkAdapter, QString{Adapter::kVirtio}); });
    connect(emulated_, &ui::ChoiceCard::chosen, this,
            [this] { controls_->put(K::kNetworkAdapter, QString{Adapter::kEmulated}); });
    cards->addWidget(virtio_, 1);
    cards->addWidget(emulated_, 1);
    panel.body->addLayout(cards);

    driver_note_ = Widgets::meta(QString{}, true);
    panel.body->addWidget(driver_note_);

    column()->addWidget(panel.frame);
}

void NetworkPage::build_bridge() {
    auto panel =
        ui::Panel::area(QStringLiteral("Bridge"),
                        QStringLiteral("The bridge on this computer that Windows joins when "
                                       "it is bridged."));
    panel.header->addWidget(Widgets::tag(true));
    panel.body->setContentsMargins(0, 0, 0, 0);
    panel.body->setSpacing(0);
    picker_ = new BridgePicker;
    connect(picker_, &BridgePicker::chosen, this,
            [this](const QString& bridge) { controls_->put(K::kNetworkBridge, bridge); });
    panel.body->addWidget(picker_);
    panel.explain(
        QStringLiteral("LSW does not create or change bridges. The bridge and QEMU's bridge "
                       "helper must be set up on this computer first."));
    bridge_area_ = panel.frame;
    column()->addWidget(panel.frame);
}

void NetworkPage::build_forwards() {
    auto panel = ui::Panel::area(QStringLiteral("Forwarded ports"),
                                 QStringLiteral("Ports on this computer that lead to ports in "
                                                "Windows."));
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
    panel.explain(foot);
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
    refresh_adapter(live);

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
    QWidget::setTabOrder(bridged_, virtio_);
    QWidget::setTabOrder(virtio_, emulated_);
    QWidget::setTabOrder(emulated_, previous);
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

void NetworkPage::refresh_adapter(const NetworkState::Live& live) {
    const bool virtio = controls_->current(K::kNetworkAdapter).toString() == Adapter::kVirtio;
    const bool waits = pending_.keys().contains(K::kNetworkAdapter);
    const bool driver = services::NetworkAdapterOffer::driver_installed(status_.update());

    virtio_->set_selected(virtio);
    emulated_->set_selected(!virtio);
    virtio_->set_in_use(live.known && live.passt);
    emulated_->set_in_use(live.known && !live.passt);
    virtio_->set_waiting(waits && virtio);
    emulated_->set_waiting(waits && !virtio);
    virtio_->setEnabled(driver || virtio);
    driver_note_->setText(QStringLiteral("Virtio waits for its driver in Windows"));
    driver_note_->setVisible(!driver);
}

void NetworkPage::refresh_now(const NetworkState::Live& live) {
    off_->setVisible(!live.running);
    now_->setVisible(live.known);
    if (live.known) {
        QStringList adapters{live.passt ? QStringLiteral("Shared adapter, virtio-net on passt")
                                        : QStringLiteral("Shared adapter, emulated e1000e")};
        for (const QString& bridge : live.bridges) {
            adapters << QStringLiteral("Bridged adapter on %1").arg(bridge);
        }
        adapters_->setText(adapters.join(QStringLiteral(" · ")));
        const bool reported = !live.guest_ipv4.isEmpty();
        address_->setText(live.guest_ipv4);
        address_->setVisible(reported);
        address_copy_->setVisible(reported);
        address_missing_->setVisible(!reported);
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
