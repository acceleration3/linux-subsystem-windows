#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/port_forwards.hpp"
#include "lsw/host/manager/port_forward_editor.hpp"
#include "lsw/host/manager/settings_page.hpp"
#include "lsw/host/platform/host_bridges.hpp"
#include "lsw/host/services/domain_inputs.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QTemporaryDir>

#include <cstdio>

using lsw::host::config::ManagerSettings;
using lsw::host::config::PendingChanges;
using lsw::host::config::PortForward;
using lsw::host::config::PortForwards;
using lsw::host::manager::PortForwardEditor;
using lsw::host::manager::SettingsPage;
using lsw::host::platform::HostBridges;
using lsw::host::vm::DomainXmlInputs;
using K = ManagerSettings::Key;
using Mode = ManagerSettings::NetworkMode;
using Protocol = PortForward::Protocol;

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

    const QVector<PortForward> sunshine = PortForwards::sunshine();
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
    check(PortForwards::accepted(sunshine, kFirst) == sunshine, "and the whole set is acceptable");
}

void check_bridges(const QString& root) {
    const QString net = root + QStringLiteral("/net");
    QDir().mkpath(net + QStringLiteral("/br-lan/bridge"));
    QDir().mkpath(net + QStringLiteral("/br-lab/bridge"));
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

void check_notes() {
    const HostBridges::Facts good = usable_host();
    HostBridges::Facts empty;
    check(SettingsPage::bridge_note(true, QString{}, empty).contains(QStringLiteral("has no bridge"))
              && SettingsPage::bridge_note(false, QString{}, empty).contains(QStringLiteral("must exist")),
          "with no bridge on the computer the row says so and that one must exist");
    check(SettingsPage::bridge_note(true, QStringLiteral("br-lan"), good).contains(QStringLiteral("second network adapter on br-lan"))
              && SettingsPage::bridge_note(true, QStringLiteral("br-lan"), good).contains(QStringLiteral("shared adapter")),
          "bridged and usable, it says Windows gets a second adapter and LSW keeps the shared one");
    const QString refused = SettingsPage::bridge_note(true, QStringLiteral("br-lab"), good);
    check(refused.startsWith(QStringLiteral("Not in effect")) && refused.contains(QStringLiteral("allow br-lab")),
          "bridged and not usable, it says it is not in effect and exactly what is missing");
    check(SettingsPage::bridge_note(true, QString{}, good).contains(QStringLiteral("no bridge is chosen")),
          "bridged with no bridge chosen, it says none is chosen");
    check(SettingsPage::bridge_note(false, QStringLiteral("br-lan"), good).startsWith(QStringLiteral("Used only when")),
          "shared, it says the bridge is used only when bridged");
}

QString shown_refusal(PortForwardEditor& editor) {
    for (QLabel* label : editor.findChildren<QLabel*>()) {
        if (!label->isHidden() && label->property("tone").toString() == QStringLiteral("bad")) {
            return label->text();
        }
    }
    return {};
}

bool shows(PortForwardEditor& editor, const QString& text) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (QLabel* label : editor.findChildren<QLabel*>()) {
        if (!label->isHidden() && label->text().contains(text)) {
            return true;
        }
    }
    return false;
}

void check_editor(ManagerSettings& settings) {
    PendingChanges pending{settings};
    PortForwardEditor editor{settings, pending};
    check(editor.entries().isEmpty() && shows(editor, QStringLiteral("No port is forwarded")),
          "with nothing forwarded the list says so");

    check(editor.add({Protocol::Tcp, 8080, 80, false}).isEmpty(), "adding a forward is accepted");
    check(editor.entries() == QVector<PortForward>{{Protocol::Tcp, 8080, 80, false}}
              && shows(editor, QStringLiteral("TCP 8080 → 80"))
              && shows(editor, QStringLiteral("this computer only")),
          "and it is listed with its ports and who can reach it");
    check(pending.count() == 1 && settings.string_value(K::kPortForwards).isEmpty(),
          "it waits for the restart instead of applying");

    const QString duplicate = editor.add({Protocol::Tcp, 8080, 8080, true});
    check(!duplicate.isEmpty() && shown_refusal(editor) == duplicate && editor.entries().size() == 1,
          "a duplicate is refused and the reason is shown");
    const QString own = editor.add({Protocol::Tcp, 5445, 445, false});
    check(own.contains(QStringLiteral("LSW itself")) && shown_refusal(editor) == own
              && editor.entries().size() == 1,
          "a port of LSW's own is refused and the reason is shown");
    check(editor.add({Protocol::Udp, 9000, 9000, true}).isEmpty() && shown_refusal(editor).isEmpty()
              && shows(editor, QStringLiteral("UDP 9000 → 9000"))
              && shows(editor, QStringLiteral("from the network")),
          "the next good forward clears the reason");

    editor.remove({Protocol::Tcp, 8080, 80, false});
    check(editor.entries() == QVector<PortForward>{{Protocol::Udp, 9000, 9000, true}}
              && !shows(editor, QStringLiteral("TCP 8080")),
          "removing a forward takes it off the list");

    check(editor.add_sunshine().isEmpty() && editor.entries().size() == 7,
          "Add Sunshine ports adds the six ports");
    check(editor.add_sunshine().isEmpty() && editor.entries().size() == 7,
          "adding them again changes nothing");

    pending.commit();
    check(PortForwards::decode(settings.string_value(K::kPortForwards)) == editor.entries()
              && pending.empty(),
          "applying stores the list");
    editor.remove({Protocol::Tcp, 47989, 47989, true});
    check(editor.add({Protocol::Tcp, 47989, 8000, false}).isEmpty(), "a Sunshine port can be used for something else");
    const QString partial = editor.add_sunshine();
    check(partial.contains(QStringLiteral("47989")) && shown_refusal(editor) == partial
              && editor.entries().size() == 7,
          "and Add Sunshine ports then says which port it could not add");
    pending.revert();
    check(editor.entries().size() == 7 && shows(editor, QStringLiteral("TCP 47989 → 47989")),
          "discarding the pending change shows the stored list again");
    settings.set_value(K::kPortForwards, QString{});
    check(editor.entries().isEmpty() && shows(editor, QStringLiteral("No port is forwarded")),
          "a list emptied elsewhere is shown empty");
}

}

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QApplication app(argc, argv);
    ManagerSettings settings;

    check_forwards();
    check_bridges(home.path() + QStringLiteral("/host"));
    check_inputs(settings);
    check_notes();
    check_editor(settings);

    std::printf("%s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
