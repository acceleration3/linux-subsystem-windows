#include "lsw/host/config/log_verbosity.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/port_forwards.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/apply_bar.hpp"
#include "lsw/host/manager/bridge_picker.hpp"
#include "lsw/host/manager/nav_rail.hpp"
#include "lsw/host/manager/network_page.hpp"
#include "lsw/host/manager/port_forward_editor.hpp"
#include "lsw/host/manager/settings_page.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/choice_card.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/vm/guest_stats_probe.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTcpServer>
#include <QTemporaryDir>
#include <cstdio>

using lsw::host::config::ManagerSettings;
using lsw::host::config::PendingChanges;
using lsw::host::config::PortForward;
using lsw::host::config::PortForwards;
using lsw::host::manager::ApplyBar;
using lsw::host::manager::BridgePicker;
using lsw::host::manager::NavRail;
using lsw::host::manager::NetworkPage;
using lsw::host::manager::NetworkState;
using lsw::host::manager::PortForwardEditor;
using lsw::host::manager::SettingsPage;
using lsw::host::platform::HostAddresses;
using lsw::host::platform::HostBridges;
using lsw::host::services::StatusHub;
using lsw::host::services::VmStatus;
using lsw::host::ui::ChoiceCard;
using lsw::host::vm::DomainDescription;
using K = ManagerSettings::Key;
using Mode = ManagerSettings::NetworkMode;
using Protocol = PortForward::Protocol;
using ForwardState = NetworkState::ForwardState;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

class NoProbe : public lsw::host::vm::IGuestStatsProbe {
public:
    bool available() const override { return false; }

    lsw::host::vm::GuestStatsSample sample() override { return {}; }
};

struct FakeHost {
    HostBridges::Facts facts;
    QVector<HostAddresses::Interface> interfaces;

    NetworkPage::Host source() {
        return {[this] { return facts; }, [this] { return interfaces; }};
    }
};

void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

bool shows(QWidget& page, const QString& text) {
    settle();
    for (QLabel* label : page.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&page) && label->text().contains(text)) {
            return true;
        }
    }
    return false;
}

ChoiceCard* card(QWidget& page, const QString& title) {
    for (ChoiceCard* known : page.findChildren<ChoiceCard*>()) {
        if (known->accessibleName() == title) {
            return known;
        }
    }
    return nullptr;
}

template<typename Button>
Button* button(QWidget& page, const QString& name) {
    settle();
    for (Button* known : page.findChildren<Button*>()) {
        if (known->isVisibleTo(&page) &&
            (known->text() == name || known->accessibleName() == name)) {
            return known;
        }
    }
    return nullptr;
}

void press(QWidget* widget, int key) {
    QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(widget, &down);
    QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(widget, &up);
}

VmStatus running_guest(const QVector<PortForward>& forwards, const QStringList& bridges,
                       const QString& ipv4) {
    VmStatus status;
    status.stats.running = true;
    status.stats.agent_reachable = true;
    DomainDescription definition;
    definition.forwards = PortForwards::own() + forwards;
    definition.bridges = bridges;
    status.running_device = definition;
    status.guest_ipv4 = ipv4;
    return status;
}

HostBridges::Facts host_with_bridges() {
    HostBridges::Facts facts;
    facts.bridges = {QStringLiteral("br-lab"), QStringLiteral("br-lan")};
    facts.details = {{QStringLiteral("br-lab"), QStringLiteral("down"), 0},
                     {QStringLiteral("br-lan"), QStringLiteral("up"), 2}};
    facts.helper_places = {QStringLiteral("/helpers/qemu-bridge-helper")};
    facts.helper = QStringLiteral("/helpers/qemu-bridge-helper");
    facts.helper_privileged = true;
    facts.rules_file = QStringLiteral("/rules/bridge.conf");
    facts.rules_readable = true;
    facts.rules = {{true, QStringLiteral("br-lan")}};
    return facts;
}

void shoot(NetworkPage& page, const QString& dir, const QString& name, int height) {
    if (dir.isEmpty()) {
        return;
    }
    page.resize(814, height);
    page.show();
    settle();
    const QString path = QDir(dir).filePath(name + QStringLiteral(".png"));
    std::printf("%s %s\n", page.grab().save(path) ? "shot" : "FAIL shot", qPrintable(path));
    page.hide();
}

}  // namespace

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("XDG_CACHE_HOME", home.path().toUtf8());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    lsw::host::ui::Theme::apply();
    const QString shots = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{};

    lsw::host::link::AgentRpcClient rpc;
    lsw::host::config::Settings settings;
    ManagerSettings manager_settings;
    lsw::host::vm::LibvirtConnection libvirt;
    lsw::host::vm::Lifecycle lifecycle{libvirt};
    NoProbe probe;
    StatusHub hub{libvirt, lifecycle, rpc, probe, settings, manager_settings};
    PendingChanges pending{manager_settings};
    FakeHost host;
    host.interfaces = {{QStringLiteral("lo"), true, true, {QStringLiteral("127.0.0.1")}},
                       {QStringLiteral("eth-test"), true, false, {QStringLiteral("192.0.2.10")}},
                       {QStringLiteral("br-lan"), true, false, {QStringLiteral("203.0.113.1")}}};

    NetworkPage page{hub, manager_settings, pending, host.source()};
    int summaries = 0;
    QObject::connect(&page, &NetworkPage::summary_changed, &page, [&summaries] { ++summaries; });
    ChoiceCard* shared = card(page, QStringLiteral("Shared with this computer"));
    ChoiceCard* bridged = card(page, QStringLiteral("Bridged to a host bridge"));
    auto* picker = page.findChild<BridgePicker*>();
    auto* forwards = page.findChild<PortForwardEditor*>();

    check(shared != nullptr && bridged != nullptr && picker != nullptr && forwards != nullptr,
          "the page has the two ways to connect, the bridge and the forwarded ports");
    if (shared == nullptr || bridged == nullptr || picker == nullptr || forwards == nullptr) {
        return 1;
    }
    check(shows(page, QStringLiteral("How Windows is connected")) &&
              shows(page, QStringLiteral("Forwarded ports")),
          "each part has its heading");
    check(shared->selected() && !bridged->selected() && !shared->in_use() && !bridged->in_use() &&
              !shared->waiting() && !bridged->waiting(),
          "untouched and with the guest off, shared is chosen and nothing is in use or waiting");
    check(shows(page, QStringLiteral("The guest is off")) &&
              !shows(page, QStringLiteral("Shared adapter")),
          "with the guest off it says so instead of showing adapters");
    check(!picker->isVisibleTo(&page), "the bridge part is not there while shared is chosen");
    check(shows(page, QStringLiteral("A forwarded port lets")) &&
              shows(page, QStringLiteral("no port")),
          "with nothing forwarded it says what a forwarded port is for");
    check(page.summary() ==
              QStringLiteral("Guest off · starts shared with this computer · no port to forward"),
          "the summary says the guest is off and what it starts with");
    check(shows(page, QStringLiteral("LSW itself uses TCP 5102, 5103, 5445")),
          "the ports LSW uses itself are named");
    shoot(page, shots, QStringLiteral("network-1-empty-shared"), 560);

    const int active_port = [] {
        QTcpServer finder;
        finder.listen(QHostAddress::LocalHost, 0);
        return static_cast<int>(finder.serverPort());
    }();
    const PortForward active{Protocol::Tcp, active_port, 80, false};
    manager_settings.set_value(K::kPortForwards, PortForwards::encode({active}));
    hub.set_vm(running_guest({active}, {}, QStringLiteral("192.0.2.44")));
    check(shared->in_use() && !bridged->in_use() && shared->selected(),
          "with the guest running on the shared network, shared carries In use");
    check(!shows(page, QStringLiteral("The guest is off")) &&
              shows(page, QStringLiteral("Shared adapter")) &&
              !shows(page, QStringLiteral("Bridged adapter")) &&
              shows(page, QStringLiteral("192.0.2.44")) &&
              shows(page, QStringLiteral("as Windows reported when it started")),
          "and the page shows its adapter and the address Windows reported");
    check(page.summary() == QStringLiteral("Shared with this computer · 1 port forwarded"),
          "the summary is what the guest runs with");
    check(forwards->state_of(active) == ForwardState::Active &&
              shows(page, QStringLiteral("Active")) && shows(page, QStringLiteral("1 port")),
          "the forward the guest runs with is active");
    hub.set_guest_address(QString{});
    check(shows(page, QStringLiteral("Windows has not reported one yet")) &&
              !shows(page, QStringLiteral("192.0.2.44")),
          "without a reported address it says none was reported");
    hub.set_guest_address(QStringLiteral("192.0.2.44"));

    const int before = summaries;
    press(bridged, Qt::Key_Space);
    check(pending.keys().contains(K::kNetworkMode) &&
              pending.staged_or_current(K::kNetworkMode).toString() == Mode::kBridged &&
              manager_settings.string_value(K::kNetworkMode) == Mode::kShared,
          "choosing bridged from the keyboard stages it and stores nothing");
    check(bridged->selected() && bridged->waiting() && !bridged->in_use() && shared->in_use() &&
              !shared->selected() && !shared->waiting(),
          "bridged is chosen and waits for a restart while shared stays in use");
    check(
        summaries > before &&
            page.summary() ==
                QStringLiteral(
                    "Shared with this computer · 1 port forwarded · 1 change waits for a restart"),
        "the summary says a change waits");
    check(ApplyBar::page_of(K::kNetworkMode) == NavRail::Page::Network &&
              ApplyBar::page_of(K::kNetworkBridge) == NavRail::Page::Network &&
              ApplyBar::page_of(K::kPortForwards) == NavRail::Page::Network &&
              ApplyBar::page_of(K::kCpuCores) == NavRail::Page::Settings,
          "changes to the network count on the Network entry of the rail");
    check(
        picker->isVisibleTo(&page) && shows(page, QStringLiteral("A bridge is a virtual switch")) &&
            shows(page, QStringLiteral("one must exist here")) &&
            button<QRadioButton>(page, QStringLiteral("Join")) == nullptr,
        "bridged on a computer without a bridge explains what a bridge is and that one must exist");
    shoot(page, shots, QStringLiteral("network-4-bridged-no-bridge"), 700);

    host.facts = host_with_bridges();
    page.read_host();
    check(!shows(page, QStringLiteral("A bridge is a virtual switch")) &&
              shows(page, QStringLiteral("br-lan")) &&
              shows(page, QStringLiteral("203.0.113.1 · 2 interfaces")) &&
              shows(page, QStringLiteral("no address · no interface")) &&
              shows(page, QStringLiteral("Up")) && shows(page, QStringLiteral("Down")),
          "with bridges it lists each with its state, address and interfaces");
    check(shows(page, QStringLiteral("Choose a bridge to see")),
          "and asks for a choice before judging one");
    auto* join_lab = button<QRadioButton>(page, QStringLiteral("Join br-lab"));
    check(join_lab != nullptr, "each bridge can be chosen");
    if (join_lab != nullptr) {
        join_lab->click();
    }
    check(pending.staged_or_current(K::kNetworkBridge).toString() == QStringLiteral("br-lab") &&
              manager_settings.string_value(K::kNetworkBridge).isEmpty(),
          "choosing a bridge stages it");
    join_lab = button<QRadioButton>(page, QStringLiteral("Join br-lab"));
    check(join_lab != nullptr && join_lab->isChecked() &&
              !button<QRadioButton>(page, QStringLiteral("Join br-lan"))->isChecked(),
          "and marks it");
    check(
        shows(page, QStringLiteral("QEMU's bridge helper is installed")) &&
            shows(page, QStringLiteral("The helper may create the adapter")) &&
            shows(page, QStringLiteral("/rules/bridge.conf does not allow br-lab")) &&
            shows(page, QStringLiteral("Add this line to /rules/bridge.conf")) &&
            shows(page, QStringLiteral("allow br-lab")) &&
            button<QPushButton>(page, QStringLiteral("Copy allow br-lab")) != nullptr,
        "the checklist says what is met and, for what is not, the line to add with a copy button");
    if (auto* copy = button<QPushButton>(page, QStringLiteral("Copy allow br-lab"))) {
        copy->click();
    }
    shoot(page, shots, QStringLiteral("network-3-bridged-failing-checklist"), 900);
    host.facts.helper_privileged = false;
    page.read_host();
    check(shows(page, QStringLiteral("The helper may not create the adapter")) &&
              shows(page, QStringLiteral("chmod u+s /helpers/qemu-bridge-helper")),
          "a helper without privilege is said with the command");
    host.facts.helper.clear();
    page.read_host();
    check(shows(page, QStringLiteral("QEMU's bridge helper is not installed")) &&
              shows(page, QStringLiteral("Looked for /helpers/qemu-bridge-helper")),
          "a missing helper is said with where it was looked for");
    host.facts = host_with_bridges();
    page.read_host();
    button<QRadioButton>(page, QStringLiteral("Join br-lan"))->click();
    check(shows(page, QStringLiteral("/rules/bridge.conf allows br-lan")) &&
              !shows(page, QStringLiteral("does not allow")),
          "a bridge that is ready has every line met");

    QTcpServer squatter;
    squatter.listen(QHostAddress::LocalHost, 0);
    const PortForward blocked{Protocol::Tcp, static_cast<int>(squatter.serverPort()), 443, false};
    const int staged_port = [] {
        QTcpServer finder;
        finder.listen(QHostAddress::LocalHost, 0);
        return static_cast<int>(finder.serverPort());
    }();
    const PortForward staged{Protocol::Udp, staged_port, staged_port, true};
    check(forwards->add(staged).isEmpty() && forwards->add(blocked).isEmpty(),
          "forwards are added on the page");
    check(pending.keys().contains(K::kPortForwards) &&
              PortForwards::decode(pending.staged_or_current(K::kPortForwards).toString()) ==
                  QVector<PortForward>{active, staged, blocked} &&
              PortForwards::decode(manager_settings.string_value(K::kPortForwards)) ==
                  QVector<PortForward>{active},
          "and reach the pending changes without being stored");
    check(forwards->state_of(active) == ForwardState::Active &&
              forwards->state_of(staged) == ForwardState::AfterRestart &&
              forwards->state_of(blocked) == ForwardState::PortInUse &&
              shows(page, QStringLiteral("Active")) &&
              shows(page, QStringLiteral("After restart")) &&
              shows(page, QStringLiteral("Port in use")) && shows(page, QStringLiteral("3 ports")),
          "the rows show one active, one waiting for a restart and one whose port is in use");
    check(shows(page, QStringLiteral("Other machines connect to")) &&
              shows(page, QStringLiteral("192.0.2.10")) &&
              shows(page, QStringLiteral("203.0.113.1")) &&
              !shows(page, QStringLiteral("127.0.0.1")),
          "a forward open to the network lists this computer's addresses, not loopback");
    check(page.summary() ==
              QStringLiteral(
                  "Shared with this computer · 1 port forwarded · 3 changes wait for a restart"),
          "the summary counts the three waiting changes");
    shoot(page, shots, QStringLiteral("network-5-running-forwards-in-each-state"), 1000);

    squatter.close();
    page.show();
    settle();
    check(forwards->state_of(blocked) == ForwardState::AfterRestart,
          "showing the page checks the ports again");
    QTcpServer again;
    again.listen(QHostAddress::LocalHost, static_cast<quint16>(blocked.host_port));
    hub.set_vm(running_guest({active}, {}, QStringLiteral("192.0.2.44")));
    check(forwards->state_of(blocked) == ForwardState::AfterRestart,
          "a status refresh that changes nothing does not touch the ports");
    hub.set_vm(VmStatus{});
    check(forwards->state_of(blocked) == ForwardState::PortInUse,
          "the guest stopping while the page is shown checks them again");
    again.close();
    page.hide();
    hub.set_vm(running_guest({active}, {}, QStringLiteral("192.0.2.44")));

    auto* host_port = [&page]() -> QLineEdit* {
        for (QLineEdit* edit : page.findChildren<QLineEdit*>()) {
            if (edit->accessibleName() == QStringLiteral("Port on this computer")) {
                return edit;
            }
        }
        return nullptr;
    }();
    check(host_port != nullptr, "the row for adding has a field for the port on this computer");
    if (host_port != nullptr) {
        host_port->setText(QStringLiteral("5445"));
        press(host_port, Qt::Key_Return);
        check(shows(page, QStringLiteral("LSW itself uses TCP port 5445")) &&
                  host_port->text() == QStringLiteral("5445") && forwards->entries().size() == 3,
              "Enter on a port LSW uses is refused in place and the field keeps what was typed");
        const int typed = [] {
            QTcpServer finder;
            finder.listen(QHostAddress::LocalHost, 0);
            return static_cast<int>(finder.serverPort());
        }();
        host_port->setText(QString::number(typed));
        press(host_port, Qt::Key_Return);
        check(forwards->entries().size() == 4 &&
                  forwards->entries().last() == PortForward{Protocol::Tcp, typed, typed, false} &&
                  host_port->text().isEmpty() &&
                  !shows(page, QStringLiteral("LSW itself uses TCP port 5445")),
              "Enter adds a typed port, to the same port in Windows, and empties the field for the "
              "next");
    }

    pending.revert();
    check(!pending.keys().contains(K::kPortForwards) &&
              forwards->entries() == QVector<PortForward>{active} && shared->selected() &&
              !bridged->waiting() && !picker->isVisibleTo(&page),
          "reverting puts the page back to what is stored");

    hub.set_vm(running_guest({active}, {QStringLiteral("br-lan")}, QStringLiteral("203.0.113.9")));
    check(bridged->in_use() && !shared->in_use() &&
              shows(page, QStringLiteral("Bridged adapter on br-lan")) &&
              page.summary() == QStringLiteral("Bridged to br-lan · 1 port forwarded"),
          "a guest that runs bridged shows it, with its second adapter");
    manager_settings.set_value(K::kNetworkMode, QString{Mode::kBridged});
    manager_settings.set_value(K::kNetworkBridge, QStringLiteral("br-lab"));
    hub.set_vm(running_guest({active}, {}, QStringLiteral("192.0.2.44")));
    check(shared->in_use() && bridged->selected() &&
              shows(page, QStringLiteral("Bridged is not in effect")),
          "a guest set to bridged that started shared says bridged is not in effect");

    hub.set_vm(VmStatus{});
    manager_settings.set_value(K::kNetworkMode, QString{Mode::kShared});
    manager_settings.set_value(K::kNetworkBridge, QString{});
    manager_settings.set_value(K::kPortForwards, QString{});
    settle();
    if (auto* sunshine = button<QPushButton>(page, QStringLiteral("Sunshine / Moonlight"))) {
        sunshine->click();
    }
    check(forwards->entries().size() == 6 &&
              pending.keys() == QStringList{QString{K::kPortForwards}} &&
              page.summary() ==
                  QStringLiteral(
                      "Guest off · starts shared with this computer · no port to forward · "
                      "1 change waits for a restart"),
          "the Sunshine set staged with the guest off waits as one change");
    shoot(page, shots, QStringLiteral("network-2-shared-sunshine-staged"), 820);
    pending.revert();

    lsw::host::config::LogVerbosity verbosity{manager_settings};
    SettingsPage settings_page{manager_settings, pending, hub, verbosity};
    check(!shows(settings_page, QStringLiteral("Forwarded ports")) &&
              !shows(settings_page, QStringLiteral("Bridge")) &&
              settings_page.findChild<PortForwardEditor*>() == nullptr &&
              shows(settings_page, QStringLiteral("Processor cores")),
          "the Settings page no longer has the network group");

    NavRail rail;
    if (!shots.isEmpty()) {
        rail.resize(186, 560);
        const QString path = QDir(shots).filePath(QStringLiteral("network-6-rail.png"));
        std::printf("%s %s\n", rail.grab().save(path) ? "shot" : "FAIL shot", qPrintable(path));
    }
    QPushButton* entry = nullptr;
    int position = -1;
    int index = 0;
    QStringList labels;
    for (QPushButton* item : rail.findChildren<QPushButton*>()) {
        const auto names = item->findChildren<QLabel*>();
        if (names.isEmpty() || !item->isCheckable()) {
            continue;
        }
        labels << names.first()->text();
        if (names.first()->text() == QStringLiteral("Network")) {
            entry = item;
            position = index;
        }
        ++index;
    }
    check(entry != nullptr && position > 0 && labels.value(position - 1) == QStringLiteral("USB") &&
              labels.value(position + 1) == QStringLiteral("Console"),
          "the rail has a Network entry after USB and before Console");
    NavRail::Page selected = NavRail::Page::Overview;
    QObject::connect(&rail, &NavRail::page_selected, &rail,
                     [&selected](NavRail::Page chosen) { selected = chosen; });
    if (entry != nullptr) {
        entry->click();
    }
    check(selected == NavRail::Page::Network && rail.current() == NavRail::Page::Network,
          "and choosing it selects the Network page");

    std::printf("%s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
