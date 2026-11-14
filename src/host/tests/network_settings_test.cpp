#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/port_forwards.hpp"
#include "lsw/host/manager/network_state.hpp"
#include "lsw/host/manager/port_forward_editor.hpp"
#include "lsw/host/platform/host_addresses.hpp"
#include "lsw/host/platform/host_bridges.hpp"
#include "lsw/host/services/domain_inputs.hpp"
#include "lsw/host/services/vm_status.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QUdpSocket>

#include <cstdio>

using lsw::host::config::ManagerSettings;
using lsw::host::config::PendingChanges;
using lsw::host::config::PortForward;
using lsw::host::config::PortForwards;
using lsw::host::config::PortPreset;
using lsw::host::manager::NetworkState;
using lsw::host::manager::PortForwardEditor;
using lsw::host::platform::HostAddresses;
using lsw::host::platform::HostBridges;
using lsw::host::services::VmStatus;
using lsw::host::vm::DomainDescription;
using lsw::host::vm::DomainXmlInputs;
using K = ManagerSettings::Key;
using Mode = ManagerSettings::NetworkMode;
using Protocol = PortForward::Protocol;
using ForwardState = NetworkState::ForwardState;
using Met = HostBridges::Requirement::State;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void write(const QString& path, const QByteArray& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text);
    }
}

HostBridges::Facts usable_host() {
    HostBridges::Facts facts;
    facts.bridges = {QStringLiteral("br-lan"), QStringLiteral("br-lab")};
    facts.helper_places = {QStringLiteral("/helpers/qemu-bridge-helper")};
    facts.helper = QStringLiteral("/helpers/qemu-bridge-helper");
    facts.helper_privileged = true;
    facts.rules_file = QStringLiteral("/rules/bridge.conf");
    facts.rules_readable = true;
    facts.rules = {{true, QStringLiteral("br-lan")}};
    return facts;
}

void check_forwards() {
    constexpr int kFirst = 1024;
    const QVector<PortForward> none;
    check(PortForwards::refusal(none, {Protocol::Tcp, 8080, 80, false}, kFirst).isEmpty(),
          "an ordinary forward is accepted");
    check(PortForwards::refusal(none, {Protocol::Tcp, 1, 80, false}, 1).isEmpty()
              && PortForwards::refusal(none, {Protocol::Udp, 65535, 65535, true}, kFirst).isEmpty(),
          "ports 1 and 65535 are ports");
    check(PortForwards::refusal(none, {Protocol::Tcp, 0, 80, false}, kFirst).contains(QStringLiteral("1 to 65535"))
              && !PortForwards::refusal(none, {Protocol::Tcp, 65536, 80, false}, kFirst).isEmpty()
              && !PortForwards::refusal(none, {Protocol::Tcp, 8080, 0, false}, kFirst).isEmpty()
              && !PortForwards::refusal(none, {Protocol::Tcp, 8080, 70000, false}, kFirst).isEmpty(),
          "a port outside 1 to 65535 on either side is refused, and the refusal says so");
    check(PortForwards::refusal(none, {Protocol::Tcp, 443, 443, false}, kFirst).contains(QStringLiteral("1024")),
          "a host port only the administrator may listen on is refused, naming the limit");
    check(PortForwards::refusal(none, {Protocol::Tcp, 443, 443, false}, 80).isEmpty(),
          "and accepted where the computer allows it");
    bool own_refused = !PortForwards::own().isEmpty();
    for (const PortForward& own : PortForwards::own()) {
        const QString refusal =
            PortForwards::refusal(none, {own.protocol, own.host_port, 9000, true}, kFirst);
        own_refused = own_refused && refusal.contains(QStringLiteral("LSW itself"))
                      && refusal.contains(QString::number(own.host_port));
    }
    check(own_refused, "a host port LSW itself forwards is refused, saying whose it is");
    check(PortForwards::refusal(none, {Protocol::Udp, 5102, 5102, false}, kFirst).isEmpty(),
          "the same number on the other protocol does not clash with LSW's");
    const QVector<PortForward> one = {{Protocol::Tcp, 8080, 80, false}};
    check(PortForwards::refusal(one, {Protocol::Tcp, 8080, 8080, true}, kFirst)
              .contains(QStringLiteral("already forwarded")),
          "a host port forwarded already is refused, whatever it would lead to");
    check(PortForwards::refusal(one, {Protocol::Udp, 8080, 80, false}, kFirst).isEmpty()
              && PortForwards::refusal(one, {Protocol::Tcp, 8081, 80, false}, kFirst).isEmpty(),
          "the other protocol, or another host port to the same guest port, is not a duplicate");

    const QVector<PortForward> mixed = {
        {Protocol::Tcp, 8080, 80, false},
        {Protocol::Udp, 47998, 47998, true},
    };
    check(PortForwards::decode(PortForwards::encode(mixed)) == mixed, "forwards round-trip");
    check(PortForwards::encode({}).isEmpty() && PortForwards::decode(QString{}).isEmpty(),
          "no forwards are stored as nothing");
    check(PortForwards::decode(QStringLiteral("junk tcp:1:2 icmp:1:2:local tcp:9000:90:local"))
              == QVector<PortForward>{{Protocol::Tcp, 9000, 90, false}},
          "records that are not forwards are left out");
    check(PortForwards::accepted({{Protocol::Tcp, 8080, 80, false},
                                  {Protocol::Tcp, 8080, 81, false},
                                  {Protocol::Tcp, 5445, 445, false},
                                  {Protocol::Tcp, 0, 1, false},
                                  {Protocol::Udp, 9000, 9000, true}},
                                 kFirst)
              == QVector<PortForward>{{Protocol::Tcp, 8080, 80, false}, {Protocol::Udp, 9000, 9000, true}},
          "only forwards that would be accepted reach the guest's definition");
}

const PortPreset& preset(const QString& name) {
    for (const PortPreset& known : PortForwards::presets()) {
        if (known.name == name) {
            return known;
        }
    }
    static const PortPreset none;
    return none;
}

void check_presets() {
    constexpr int kFirst = 1024;
    const QVector<PortForward> sunshine = preset(QStringLiteral("Sunshine / Moonlight")).ports;
    QStringList tcp;
    QStringList udp;
    bool straight = true;
    for (const PortForward& entry : sunshine) {
        (entry.protocol == Protocol::Tcp ? tcp : udp) << QString::number(entry.host_port);
        straight = straight && entry.host_port == entry.guest_port && entry.from_network;
    }
    check(tcp.join(QLatin1Char(' ')) == QStringLiteral("47984 47989 48010")
              && udp.join(QLatin1Char(' ')) == QStringLiteral("47998 47999 48000"),
          "the Sunshine set is TCP 47984, 47989, 48010 and UDP 47998 to 48000");
    check(straight, "each to the same port in Windows and reachable from the network");

    QVector<PortForward> every;
    bool each_passes = PortForwards::presets().size() == 5;
    bool others_local = true;
    for (const PortPreset& known : PortForwards::presets()) {
        each_passes = each_passes && !known.name.isEmpty() && !known.description.isEmpty()
                      && !known.ports.isEmpty()
                      && PortForwards::accepted(known.ports, kFirst) == known.ports;
        every += known.ports;
        if (known.name != QStringLiteral("Sunshine / Moonlight")) {
            for (const PortForward& port : known.ports) {
                others_local = others_local && !port.from_network;
            }
        }
    }
    check(each_passes, "there are five named sets and every port of each would be accepted");
    check(PortForwards::accepted(every, kFirst) == every, "the sets do not clash with one another");
    check(others_local, "every set but Sunshine is reachable from this computer only");
    check(preset(QStringLiteral("SSH")).ports == QVector<PortForward>{{Protocol::Tcp, 2222, 22, false}}
              && preset(QStringLiteral("Web server")).ports
                     == QVector<PortForward>{{Protocol::Tcp, 8080, 80, false}, {Protocol::Tcp, 8443, 443, false}}
              && preset(QStringLiteral("Remote Desktop")).ports
                     == QVector<PortForward>{{Protocol::Tcp, 3389, 3389, false}, {Protocol::Udp, 3389, 3389, false}}
              && preset(QStringLiteral("SQL Server")).ports
                     == QVector<PortForward>{{Protocol::Tcp, 1433, 1433, false}},
          "SSH and the web server use host ports an ordinary user may listen on");
    check(PortForwards::mapping(preset(QStringLiteral("SSH"))).contains(QStringLiteral("TCP 2222 → 22"))
              && PortForwards::mapping(preset(QStringLiteral("SSH"))).contains(QStringLiteral("this computer only"))
              && PortForwards::mapping(preset(QStringLiteral("Sunshine / Moonlight")))
                     .contains(QStringLiteral("from the network")),
          "a set says which port leads where and who can reach it");

    const PortPreset& web = preset(QStringLiteral("Web server"));
    const QVector<PortForward> other = {{Protocol::Tcp, 9000, 9000, false}};
    check(!PortForwards::applied(other, web), "a set none of whose ports are forwarded is not applied");
    QStringList refusals;
    const QVector<PortForward> added = PortForwards::with(other, web, &refusals, kFirst);
    check(refusals.isEmpty() && added.size() == 3 && added.first() == other.first()
              && PortForwards::applied(added, web),
          "adding a set keeps what was there and makes it applied");
    check(PortForwards::with(added, web, &refusals, kFirst) == added && refusals.isEmpty(),
          "adding it again changes nothing");
    QVector<PortForward> opened = added;
    opened[1].from_network = true;
    check(PortForwards::applied(opened, web), "a set stays applied when a row is opened to the network");
    QVector<PortForward> partial = added;
    partial.removeLast();
    check(!PortForwards::applied(partial, web), "a set with a port missing is not applied");
    check(PortForwards::without(opened, web) == other, "removing a set takes its ports and nothing else");
    const QVector<PortForward> taken = {{Protocol::Tcp, 8080, 3000, false}};
    const QVector<PortForward> around = PortForwards::with(taken, web, &refusals, kFirst);
    check(refusals.size() == 1 && refusals.first().contains(QStringLiteral("8080")) && around.size() == 2
              && !PortForwards::applied(around, web),
          "a port of a set that is taken is refused by name and the set is not applied");
    refusals.clear();
    (void)PortForwards::with({}, preset(QStringLiteral("SSH")), &refusals, 4096);
    check(refusals.size() == 1 && refusals.first().contains(QStringLiteral("4096")),
          "a set is checked against what this computer lets an ordinary user listen on");

    check(PortForwards::from_hostfwd(PortForwards::hostfwd({Protocol::Udp, 47998, 47998, true}))
                  == PortForward{Protocol::Udp, 47998, 47998, true}
              && PortForwards::from_hostfwd(PortForwards::hostfwd({Protocol::Tcp, 8080, 80, false}))
                     == PortForward{Protocol::Tcp, 8080, 80, false}
              && !PortForwards::from_hostfwd(QStringLiteral("id=lsw-net"))
              && !PortForwards::from_hostfwd(QStringLiteral("hostfwd=tcp::0-:80")),
          "a forward is read back from the option QEMU is given");
}

void check_bridges(const QString& root) {
    const QString net = root + QStringLiteral("/net");
    QDir().mkpath(net + QStringLiteral("/br-lan/bridge"));
    QDir().mkpath(net + QStringLiteral("/br-lab/bridge"));
    QDir().mkpath(net + QStringLiteral("/br-lan/brif/eth-test"));
    QDir().mkpath(net + QStringLiteral("/br-lan/brif/tap-test"));
    write(net + QStringLiteral("/br-lan/operstate"), "up\n");
    write(net + QStringLiteral("/br-lab/operstate"), "down\n");
    QDir().mkpath(net + QStringLiteral("/eth-test/queues"));
    QDir().mkpath(net + QStringLiteral("/lo"));
    write(root + QStringLiteral("/rules/more.conf"), "deny br-lab\n");
    write(root + QStringLiteral("/rules/bridge.conf"),
          QByteArray("# who may be joined\n\nallow br-lan\ninclude ") + root.toUtf8()
              + "/rules/more.conf\nallow   br-lab\n");
    write(root + QStringLiteral("/helpers/qemu-bridge-helper"), "#!/bin/sh\n");
    QFile::setPermissions(root + QStringLiteral("/helpers/qemu-bridge-helper"),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    HostBridges::Places places;
    places.interfaces = net;
    places.helpers = {root + QStringLiteral("/nowhere/qemu-bridge-helper"),
                      root + QStringLiteral("/helpers/qemu-bridge-helper")};
    places.rules_file = root + QStringLiteral("/rules/bridge.conf");
    const HostBridges::Facts found = HostBridges::probe(places);
    check(found.bridges == QStringList{QStringLiteral("br-lab"), QStringLiteral("br-lan")},
          "the bridges are the interfaces that are bridges, nothing else");
    check(found.details.size() == 2 && found.details.at(0).name == QStringLiteral("br-lab")
              && found.details.at(0).state == QStringLiteral("down") && found.details.at(0).members == 0
              && found.details.at(1).name == QStringLiteral("br-lan")
              && found.details.at(1).state == QStringLiteral("up") && found.details.at(1).members == 2,
          "each bridge comes with whether it is up and how many interfaces it joins");
    check(found.helper == places.helpers.at(1), "the helper is the first one that exists");
    check(!found.helper_privileged, "a helper without setuid root or a capability is not privileged");
    check(found.rules_readable && found.rules.size() == 3 && found.rules.at(0).allow
              && found.rules.at(0).bridge == QStringLiteral("br-lan") && !found.rules.at(1).allow
              && found.rules.at(1).bridge == QStringLiteral("br-lab") && found.rules.at(2).allow,
          "the rules are read in order, through includes, without comments");
    check(HostBridges::missing(found, QStringLiteral("br-lan")).contains(places.helpers.at(1))
              && HostBridges::missing(found, QStringLiteral("br-lan")).contains(QStringLiteral("setuid")),
          "an unprivileged helper is named as what is missing");

    HostBridges::Places bare;
    bare.interfaces = root + QStringLiteral("/no-net");
    bare.helpers = {root + QStringLiteral("/nowhere/qemu-bridge-helper")};
    bare.rules_file = root + QStringLiteral("/nowhere/bridge.conf");
    const HostBridges::Facts nothing = HostBridges::probe(bare);
    check(nothing.bridges.isEmpty() && nothing.helper.isEmpty() && !nothing.rules_readable
              && nothing.rules.isEmpty(),
          "a computer with no bridge, helper or rules shows none");

    const HostBridges::Facts good = usable_host();
    check(HostBridges::missing(good, QStringLiteral("br-lan")).isEmpty(),
          "a bridge that exists, with a privileged helper allowed to use it, misses nothing");
    check(HostBridges::missing(good, QString{}).contains(QStringLiteral("no bridge is chosen")),
          "no chosen bridge is said");
    check(HostBridges::missing(good, QStringLiteral("br-gone")).contains(QStringLiteral("br-gone")),
          "a bridge that is not on the computer is said by name");
    check(HostBridges::missing(good, QStringLiteral("br-lab")).contains(QStringLiteral("allow br-lab"))
              && HostBridges::missing(good, QStringLiteral("br-lab")).contains(good.rules_file),
          "a bridge the rules do not allow is said with the line that is missing and the file");
    HostBridges::Facts no_helper = good;
    no_helper.helper.clear();
    check(HostBridges::missing(no_helper, QStringLiteral("br-lan")).contains(good.helper_places.at(0)),
          "a missing helper is said with where it was looked for");
    HostBridges::Facts plain_helper = good;
    plain_helper.helper_privileged = false;
    check(!HostBridges::missing(plain_helper, QStringLiteral("br-lan")).isEmpty(),
          "a helper without privileges is not usable");
    HostBridges::Facts unreadable = good;
    unreadable.rules.clear();
    unreadable.rules_readable = false;
    check(HostBridges::missing(unreadable, QStringLiteral("br-lan")).contains(QStringLiteral("cannot be read")),
          "rules that cannot be read are said so");
    HostBridges::Facts allow_all = good;
    allow_all.rules = {{true, QString{}}};
    check(HostBridges::missing(allow_all, QStringLiteral("br-lab")).isEmpty(), "allow all allows any bridge");
    HostBridges::Facts denied = good;
    denied.rules = {{true, QString{}}, {false, QStringLiteral("br-lan")}};
    check(HostBridges::missing(denied, QStringLiteral("br-lan")).contains(QStringLiteral("denies"))
              && HostBridges::missing(denied, QStringLiteral("br-lab")).isEmpty(),
          "a deny wins over an allow, for that bridge only");
}

void check_inputs(ManagerSettings& settings) {
    const HostBridges::Facts good = usable_host();
    check(ManagerSettings::default_value(K::kNetworkMode).toString() == Mode::kShared
              && ManagerSettings::default_value(K::kNetworkBridge).toString().isEmpty()
              && ManagerSettings::default_value(K::kPortForwards).toString().isEmpty(),
          "the network is shared, with no bridge and no forwards, unless chosen");
    check(ManagerSettings::needs_restart(K::kNetworkMode) && ManagerSettings::needs_restart(K::kNetworkBridge)
              && ManagerSettings::needs_restart(K::kPortForwards),
          "every network setting needs a restart of the guest");

    DomainXmlInputs inputs;
    lsw::host::services::network_inputs_from_settings(inputs, settings, good);
    check(inputs.forwards.isEmpty() && inputs.bridge.isEmpty(), "the defaults give the guest nothing extra");

    settings.set_value(K::kNetworkBridge, QStringLiteral("br-lan"));
    lsw::host::services::network_inputs_from_settings(inputs, settings, good);
    check(inputs.bridge.isEmpty(), "a chosen bridge is not joined while the network is shared");

    settings.set_value(K::kNetworkMode, QString{Mode::kBridged});
    lsw::host::services::network_inputs_from_settings(inputs, settings, good);
    check(inputs.bridge == QStringLiteral("br-lan"), "bridged with a usable helper, the guest joins the bridge");

    HostBridges::Facts plain_helper = good;
    plain_helper.helper_privileged = false;
    lsw::host::services::network_inputs_from_settings(inputs, settings, plain_helper);
    check(inputs.bridge.isEmpty(), "bridged without a usable helper, the guest keeps the shared network only");

    settings.set_value(K::kNetworkBridge, QStringLiteral("br-lab"));
    lsw::host::services::network_inputs_from_settings(inputs, settings, good);
    check(inputs.bridge.isEmpty(), "bridged to a bridge the rules do not allow, likewise");

    settings.set_value(K::kPortForwards, QStringLiteral("tcp:8080:80:local tcp:5102:1:network udp:9000:9000:network"));
    lsw::host::services::network_inputs_from_settings(inputs, settings, good);
    check(inputs.forwards
              == QVector<PortForward>{{Protocol::Tcp, 8080, 80, false}, {Protocol::Udp, 9000, 9000, true}},
          "the stored forwards reach the definition, without one that clashes with LSW's own");

    settings.set_value(K::kNetworkMode, QString{Mode::kShared});
    settings.set_value(K::kNetworkBridge, QString{});
    settings.set_value(K::kPortForwards, QString{});
}

const HostBridges::Requirement* line(const QVector<HostBridges::Requirement>& lines, const QString& text) {
    for (const HostBridges::Requirement& known : lines) {
        if (known.text.contains(text)) {
            return &known;
        }
    }
    return nullptr;
}

bool all_met(const QVector<HostBridges::Requirement>& lines) {
    for (const HostBridges::Requirement& known : lines) {
        if (known.state != Met::Met) {
            return false;
        }
    }
    return !lines.isEmpty();
}

void check_readiness() {
    const HostBridges::Facts good = usable_host();
    const QString lan = QStringLiteral("br-lan");
    check(HostBridges::readiness(good, QString{}).isEmpty(), "with no bridge chosen there is nothing to check");
    const auto ready = HostBridges::readiness(good, lan);
    check(ready.size() == 3 && all_met(ready) && line(ready, QStringLiteral("helper is installed")) != nullptr
              && line(ready, QStringLiteral("helper is installed"))->detail == good.helper
              && line(ready, QStringLiteral("may create the adapter")) != nullptr
              && line(ready, QStringLiteral("bridge.conf allows br-lan")) != nullptr,
          "a usable bridge has three lines, all met: the helper, its privilege and the rules");

    HostBridges::Facts no_helper = good;
    no_helper.helper.clear();
    const auto without_helper = HostBridges::readiness(no_helper, lan);
    const auto* not_installed = line(without_helper, QStringLiteral("not installed"));
    const auto* unchecked = line(without_helper, QStringLiteral("may create the adapter"));
    check(not_installed != nullptr && not_installed->state == Met::Unmet
              && not_installed->detail.contains(good.helper_places.at(0)) && unchecked != nullptr
              && unchecked->state == Met::Unknown,
          "a missing helper is said with where it was looked for, and its privilege is not judged");

    HostBridges::Facts plain = good;
    plain.helper_privileged = false;
    const auto* may_not = line(HostBridges::readiness(plain, lan), QStringLiteral("may not create"));
    check(may_not != nullptr && may_not->state == Met::Unmet
              && may_not->fix == QStringLiteral("chmod u+s /helpers/qemu-bridge-helper"),
          "an unprivileged helper is said with the command that gives it the privilege");

    const auto refused = HostBridges::readiness(good, QStringLiteral("br-lab"));
    const auto* not_allowed = line(refused, QStringLiteral("does not allow br-lab"));
    check(not_allowed != nullptr && not_allowed->state == Met::Unmet
              && not_allowed->fix == QStringLiteral("allow br-lab")
              && not_allowed->detail.contains(good.rules_file),
          "a bridge the rules do not allow is said with the line to add and the file it goes in");

    HostBridges::Facts unreadable = good;
    unreadable.rules.clear();
    unreadable.rules_readable = false;
    const auto* cannot_read = line(HostBridges::readiness(unreadable, lan), QStringLiteral("cannot be read"));
    check(cannot_read != nullptr && cannot_read->state == Met::Unmet
              && cannot_read->fix == QStringLiteral("allow br-lan"),
          "rules that cannot be read are said so, with the line they need");

    HostBridges::Facts denied = good;
    denied.rules = {{true, QString{}}, {false, lan}};
    const auto* denies = line(HostBridges::readiness(denied, lan), QStringLiteral("denies br-lan"));
    check(denies != nullptr && denies->state == Met::Unmet && denies->fix.isEmpty(),
          "a bridge the rules deny is said so");

    const auto gone = HostBridges::readiness(good, QStringLiteral("br-gone"));
    check(line(gone, QStringLiteral("no bridge named br-gone")) != nullptr
              && line(gone, QStringLiteral("no bridge named br-gone"))->state == Met::Unmet,
          "a chosen bridge that is not on the computer is the first line");

    bool agree = true;
    for (const HostBridges::Facts& facts : {good, no_helper, plain, unreadable, denied}) {
        for (const QString& bridge : {lan, QStringLiteral("br-lab"), QStringLiteral("br-gone")}) {
            agree = agree && all_met(HostBridges::readiness(facts, bridge))
                                 == HostBridges::missing(facts, bridge).isEmpty();
        }
    }
    check(agree, "every line is met exactly when the guest would be bridged");
}

void check_addresses() {
    const QVector<HostAddresses::Interface> interfaces = {
        {QStringLiteral("lo"), true, true, {QStringLiteral("127.0.0.1")}},
        {QStringLiteral("eth-test"), true, false, {QStringLiteral("192.0.2.10"), QStringLiteral("192.0.2.11")}},
        {QStringLiteral("wlan-test"), false, false, {QStringLiteral("198.51.100.7")}},
        {QStringLiteral("auto-test"), true, false, {QStringLiteral("169.254.3.4")}},
        {QStringLiteral("br-lan"), true, false, {QStringLiteral("203.0.113.1")}},
        {QStringLiteral("six-test"), true, false, {}},
    };
    check(HostAddresses::reachable(interfaces)
              == QVector<HostAddresses::Address>{{QStringLiteral("192.0.2.10"), QStringLiteral("eth-test")},
                                                 {QStringLiteral("192.0.2.11"), QStringLiteral("eth-test")},
                                                 {QStringLiteral("203.0.113.1"), QStringLiteral("br-lan")}},
          "other machines are told the addresses of interfaces that are up, not loopback or self-assigned");
    check(HostAddresses::reachable({}).isEmpty(), "a computer with no interface has no address to tell");
    check(HostAddresses::of(interfaces, QStringLiteral("br-lan")) == QStringList{QStringLiteral("203.0.113.1")}
              && HostAddresses::of(interfaces, QStringLiteral("br-none")).isEmpty(),
          "a bridge's addresses are those of the interface of its name");
}

void check_ports_in_use() {
    QTcpServer listener;
    const bool listening = listener.listen(QHostAddress::LocalHost, 0);
    const int tcp_port = listener.serverPort();
    check(listening && !NetworkState::host_port_free({Protocol::Tcp, tcp_port, 80, false})
              && !NetworkState::host_port_free({Protocol::Tcp, tcp_port, 80, true}),
          "a TCP port something listens on is in use, for this computer and for the network");
    check(NetworkState::host_port_free({Protocol::Udp, tcp_port, 80, false}),
          "the same number on UDP is not");
    listener.close();
    check(NetworkState::host_port_free({Protocol::Tcp, tcp_port, 80, false}),
          "once the listener is gone the port is free");
    check(NetworkState::host_port_free({Protocol::Tcp, tcp_port, 80, false}),
          "and asking leaves it free");

    QUdpSocket socket;
    const bool bound = socket.bind(QHostAddress::LocalHost, 0);
    const int udp_port = socket.localPort();
    check(bound && !NetworkState::host_port_free({Protocol::Udp, udp_port, 80, false}),
          "a UDP port something is bound to is in use");
    socket.close();
    check(NetworkState::host_port_free({Protocol::Udp, udp_port, 80, false}), "and free once it is released");
}

const char* const kRunningXml = R"(<domain type='kvm' id='7' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
  <name>lsw</name>
  <devices>
    <interface type='bridge'>
      <source bridge='br-lan'/>
      <model type='e1000e'/>
    </interface>
    <interface type='network'>
      <source network='default' bridge='virbr-test'/>
    </interface>
  </devices>
  <qemu:commandline>
    <qemu:arg value='-netdev'/>
    <qemu:arg value='user,id=lsw-net,hostfwd=tcp:127.0.0.1:5102-:5102,hostfwd=tcp:127.0.0.1:5103-:5103,hostfwd=tcp:127.0.0.1:5445-:445,hostfwd=tcp:127.0.0.1:8080-:80,hostfwd=udp:0.0.0.0:47998-:47998'/>
    <qemu:arg value='-device'/>
    <qemu:arg value='e1000e,netdev=lsw-net,id=lsw-nic,bus=pcie.0,addr=0x10'/>
  </qemu:commandline>
</domain>)";

void check_state() {
    const auto running = DomainDescription::parse(QString::fromLatin1(kRunningXml));
    check(running.has_value() && running->bridges == QStringList{QStringLiteral("br-lan")}
              && running->forwards.size() == 5
              && running->forwards.contains(PortForward{Protocol::Tcp, 8080, 80, false})
              && running->forwards.contains(PortForward{Protocol::Udp, 47998, 47998, true}),
          "a definition says which bridge the guest joins and which ports it forwards");

    VmStatus off;
    off.next_boot = running;
    const NetworkState::Live stopped = NetworkState::live(off);
    check(!stopped.running && !stopped.known && stopped.forwards.isEmpty() && stopped.bridges.isEmpty(),
          "a stopped guest has nothing in effect, whatever its definition says");

    VmStatus on;
    on.stats.running = true;
    on.running_device = running;
    on.guest_ipv4 = QStringLiteral("192.0.2.44");
    const NetworkState::Live live = NetworkState::live(on);
    check(live.running && live.known && live.bridges == QStringList{QStringLiteral("br-lan")}
              && live.forwards
                     == QVector<PortForward>{{Protocol::Tcp, 8080, 80, false}, {Protocol::Udp, 47998, 47998, true}}
              && live.guest_ipv4 == QStringLiteral("192.0.2.44"),
          "a running guest has its bridge, its forwards without LSW's own, and the address Windows reported");

    const PortForward active{Protocol::Tcp, 8080, 80, false};
    const PortForward staged{Protocol::Tcp, 9000, 9000, false};
    check(NetworkState::forward_state(active, live, true) == ForwardState::Active
              && NetworkState::forward_state(active, live, false) == ForwardState::Active,
          "a forward the running guest has is active, whoever holds its port");
    check(NetworkState::forward_state({Protocol::Tcp, 8080, 80, true}, live, true) == ForwardState::AfterRestart,
          "the same forward opened to the network waits for a restart");
    check(NetworkState::forward_state(staged, live, true) == ForwardState::AfterRestart,
          "a forward the running guest lacks waits for a restart");
    check(NetworkState::forward_state(staged, stopped, true) == ForwardState::NextStart,
          "with the guest off it waits for the next start");
    check(NetworkState::forward_state(staged, live, false) == ForwardState::PortInUse
              && NetworkState::forward_state(staged, stopped, false) == ForwardState::PortInUse,
          "a forward whose port is taken on this computer says so, guest on or off");

    check(NetworkState::summary(live, QString{}, 0, 0)
              == QStringLiteral("Bridged to br-lan · 2 ports forwarded"),
          "the summary of a running guest is what it runs with");
    NetworkState::Live shared = live;
    shared.bridges.clear();
    shared.forwards.removeLast();
    check(NetworkState::summary(shared, QStringLiteral("br-lan"), 5, 2)
              == QStringLiteral("Shared with this computer · 1 port forwarded · 2 changes wait for a restart"),
          "not what is stored, and it says how many changes wait");
    shared.forwards.clear();
    check(NetworkState::summary(shared, QString{}, 0, 1)
              == QStringLiteral("Shared with this computer · no port forwarded · 1 change waits for a restart"),
          "one change waits, in the singular");
    check(NetworkState::summary(stopped, QString{}, 0, 0)
              == QStringLiteral("Guest off · starts shared with this computer · no port to forward")
              && NetworkState::summary(stopped, QStringLiteral("br-lan"), 3, 0)
                     == QStringLiteral("Guest off · starts bridged to br-lan · 3 ports to forward"),
          "with the guest off it says what the guest starts with");
    NetworkState::Live unread;
    unread.running = true;
    check(NetworkState::summary(unread, QString{}, 0, 0) == QStringLiteral("Running"),
          "a running guest whose definition is not read yet claims nothing");
}

bool shows_bad(PortForwardEditor& editor, const QString& text) {
    for (QLabel* label : editor.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&editor) && label->property("tone").toString() == QStringLiteral("bad")
            && label->text() == text) {
            return true;
        }
    }
    return false;
}

bool shows(PortForwardEditor& editor, const QString& text) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (QLabel* label : editor.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&editor) && label->text().contains(text)) {
            return true;
        }
    }
    return false;
}

QPushButton* button(PortForwardEditor& editor, const QString& text) {
    for (QPushButton* known : editor.findChildren<QPushButton*>()) {
        if (known->isVisibleTo(&editor) && known->text() == text) {
            return known;
        }
    }
    return nullptr;
}

int free_port() {
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void check_editor(ManagerSettings& settings) {
    PendingChanges pending{settings};
    PortForwardEditor editor{settings, pending};
    int changes = 0;
    QObject::connect(&editor, &PortForwardEditor::changed, &editor, [&changes] { ++changes; });
    check(editor.entries().isEmpty() && shows(editor, QStringLiteral("A forwarded port lets")),
          "with nothing forwarded it says what a forwarded port is for");
    check(button(editor, QStringLiteral("Sunshine / Moonlight")) != nullptr
              && button(editor, QStringLiteral("Remote Desktop")) != nullptr
              && button(editor, QStringLiteral("SQL Server")) != nullptr
              && button(editor, QStringLiteral("SSH")) != nullptr
              && button(editor, QStringLiteral("Web server")) != nullptr,
          "and offers the five sets");
    check(button(editor, QStringLiteral("SSH"))->toolTip().contains(QStringLiteral("TCP 2222 → 22")),
          "a set's tooltip says which port leads where");

    const int first = free_port();
    check(editor.add({Protocol::Tcp, first, 80, false}).isEmpty(), "adding a forward is accepted");
    check(editor.entries() == QVector<PortForward>{{Protocol::Tcp, first, 80, false}}
              && shows(editor, QString::number(first)) && shows(editor, QStringLiteral("TCP"))
              && !shows(editor, QStringLiteral("A forwarded port lets")),
          "and it is listed with its protocol and ports");
    check(editor.state_of({Protocol::Tcp, first, 80, false}) == ForwardState::NextStart
              && shows(editor, QStringLiteral("Next start")),
          "with the guest off it waits for the next start");
    check(pending.count() == 1 && settings.string_value(K::kPortForwards).isEmpty() && changes > 0,
          "it waits for the restart instead of applying");

    const QString duplicate = editor.add({Protocol::Tcp, first, 8080, true});
    check(!duplicate.isEmpty() && shows_bad(editor, duplicate) && editor.entries().size() == 1,
          "a duplicate is refused and the reason is shown");
    const QString own = editor.add({Protocol::Tcp, 5445, 445, false});
    check(own.contains(QStringLiteral("LSW itself")) && shows_bad(editor, own)
              && !shows_bad(editor, duplicate) && editor.entries().size() == 1,
          "a port of LSW's own is refused and the reason is shown");
    const QString range = editor.add({Protocol::Tcp, first + 1, 70000, false});
    check(range.contains(QStringLiteral("1 to 65535")) && shows_bad(editor, range),
          "a port in Windows outside 1 to 65535 is refused");
    const int second = free_port();
    check(editor.add({Protocol::Udp, second, second, true}).isEmpty() && !shows_bad(editor, range)
              && shows(editor, QString::number(second)) && shows(editor, QStringLiteral("UDP")),
          "the next good forward clears the reason");
    check(!shows(editor, QStringLiteral("Other machines connect to"))
              && shows(editor, QStringLiteral("no network address")),
          "a forward open to the network on a computer without an address says nobody can reach it");
    editor.set_host_addresses({{QStringLiteral("192.0.2.10"), QStringLiteral("eth-test")}});
    check(shows(editor, QStringLiteral("Other machines connect to")) && shows(editor, QStringLiteral("192.0.2.10"))
              && shows(editor, QStringLiteral("eth-test")) && button(editor, QStringLiteral("Copy")) != nullptr,
          "with an address it says where other machines connect, with a copy button");

    editor.set_reach({Protocol::Udp, second, second, true}, false);
    check(editor.entries().at(1) == PortForward{Protocol::Udp, second, second, false}
              && !shows(editor, QStringLiteral("Other machines connect to")),
          "switching a row to this computer only keeps the row and drops the address line");

    QTcpServer squatter;
    squatter.listen(QHostAddress::LocalHost, 0);
    const int taken = squatter.serverPort();
    check(editor.add({Protocol::Tcp, taken, 443, false}).isEmpty()
              && editor.state_of({Protocol::Tcp, taken, 443, false}) == ForwardState::PortInUse
              && shows(editor, QStringLiteral("Port in use"))
              && shows(editor, QStringLiteral("already uses TCP port %1").arg(taken))
              && shows(editor, QStringLiteral("Pick another host port")),
          "a forward whose host port is taken is added, marked in use, and says what to do");
    squatter.close();
    check(editor.state_of({Protocol::Tcp, taken, 443, false}) == ForwardState::PortInUse,
          "the mark stays until the ports are checked again");
    editor.check_ports();
    check(editor.state_of({Protocol::Tcp, taken, 443, false}) == ForwardState::NextStart
              && !shows(editor, QStringLiteral("Port in use")),
          "checking again after the port is freed clears it");

    NetworkState::Live live;
    live.running = true;
    live.known = true;
    live.forwards = {{Protocol::Tcp, first, 80, false}};
    QTcpServer guest;
    guest.listen(QHostAddress::LocalHost, static_cast<quint16>(first));
    editor.set_live(live);
    check(editor.state_of({Protocol::Tcp, first, 80, false}) == ForwardState::Active
              && shows(editor, QStringLiteral("Active")),
          "a forward the running guest has is active though its port is held");
    check(editor.state_of({Protocol::Tcp, taken, 443, false}) == ForwardState::AfterRestart
              && shows(editor, QStringLiteral("After restart")),
          "one it lacks waits for a restart");
    editor.set_reach({Protocol::Tcp, first, 80, false}, true);
    check(editor.state_of({Protocol::Tcp, first, 80, true}) == ForwardState::AfterRestart,
          "opening an active forward to the network waits for a restart, not for its own port");
    editor.set_reach({Protocol::Tcp, first, 80, true}, false);
    guest.close();
    editor.set_live({});

    check(!editor.can_undo() && button(editor, QStringLiteral("Undo")) == nullptr,
          "there is nothing to undo before something is removed");
    editor.remove({Protocol::Tcp, first, 80, false});
    check(editor.entries().size() == 2 && !editor.entries().contains(PortForward{Protocol::Tcp, first, 80, false}),
          "removing a forward takes it off the list at once");
    check(editor.can_undo() && shows(editor, QStringLiteral("Removed TCP %1 → 80.").arg(first))
              && button(editor, QStringLiteral("Undo")) != nullptr,
          "and offers to undo it");
    button(editor, QStringLiteral("Undo"))->click();
    check(editor.entries().size() == 3 && editor.entries().first() == PortForward{Protocol::Tcp, first, 80, false}
              && !editor.can_undo() && button(editor, QStringLiteral("Undo")) == nullptr,
          "undo puts it back where it was");
    editor.remove({Protocol::Tcp, first, 80, false});
    check(editor.add({Protocol::Tcp, first, 81, false}).isEmpty() && !editor.can_undo(),
          "the offer goes away with the next change");
    editor.remove({Protocol::Tcp, first, 81, false});
    editor.remove({Protocol::Udp, second, second, false});
    editor.remove({Protocol::Tcp, taken, 443, false});

    const PortPreset& sunshine = preset(QStringLiteral("Sunshine / Moonlight"));
    button(editor, QStringLiteral("Sunshine / Moonlight"))->click();
    check(editor.entries() == sunshine.ports && button(editor, QStringLiteral("✓ Sunshine / Moonlight")) != nullptr
              && button(editor, QStringLiteral("SSH")) != nullptr,
          "clicking a set adds its ports and shows it as added");
    check(editor.apply(sunshine).isEmpty() && editor.entries().size() == 6, "adding it again changes nothing");

    pending.commit();
    check(PortForwards::decode(settings.string_value(K::kPortForwards)) == editor.entries()
              && pending.empty(),
          "applying stores the list");
    editor.remove({Protocol::Tcp, 47989, 47989, true});
    check(button(editor, QStringLiteral("Sunshine / Moonlight")) != nullptr,
          "a set with a port removed no longer shows as added");
    check(editor.add({Protocol::Tcp, 47989, 8000, false}).isEmpty(), "a Sunshine port can be used for something else");
    const QString partial = editor.apply(sunshine);
    check(partial.contains(QStringLiteral("47989")) && partial.contains(QStringLiteral("Sunshine"))
              && shows_bad(editor, partial) && editor.entries().size() == 6,
          "and adding the set then says which port it could not add");
    pending.revert();
    check(editor.entries() == sunshine.ports && !editor.can_undo()
              && button(editor, QStringLiteral("✓ Sunshine / Moonlight")) != nullptr,
          "discarding the pending change shows the stored list again");

    button(editor, QStringLiteral("✓ Sunshine / Moonlight"))->click();
    check(editor.entries().size() == 6 && shows(editor, QStringLiteral("Sunshine / Moonlight is added."))
              && button(editor, QStringLiteral("Remove its 6 ports")) != nullptr
              && button(editor, QStringLiteral("Keep")) != nullptr,
          "clicking an added set offers to remove its ports and removes nothing yet");
    button(editor, QStringLiteral("Keep"))->click();
    check(editor.entries().size() == 6 && button(editor, QStringLiteral("Remove its 6 ports")) == nullptr,
          "Keep leaves them");
    button(editor, QStringLiteral("✓ Sunshine / Moonlight"))->click();
    button(editor, QStringLiteral("Remove its 6 ports"))->click();
    check(editor.entries().isEmpty() && shows(editor, QStringLiteral("Removed the 6 ports of Sunshine / Moonlight."))
              && editor.can_undo() && shows(editor, QStringLiteral("A forwarded port lets")),
          "removing the set takes its ports and can be undone");
    editor.undo();
    check(editor.entries() == sunshine.ports, "undo brings the set back");

    settings.set_value(K::kPortForwards, QString{});
    settle();
    check(editor.entries().isEmpty() && shows(editor, QStringLiteral("A forwarded port lets")),
          "a list emptied elsewhere is shown empty");
}

}

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QApplication app(argc, argv);
    ManagerSettings settings;

    check_forwards();
    check_presets();
    check_bridges(home.path() + QStringLiteral("/host"));
    check_inputs(settings);
    check_readiness();
    check_addresses();
    check_ports_in_use();
    check_state();
    check_editor(settings);

    std::printf("%s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
