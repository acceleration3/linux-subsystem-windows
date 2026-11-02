#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/windows_fs_launcher.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/services/windows_mount.hpp"
#include "lsw/host/vm/guest_stats_probe.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QUuid>
#include <cstdio>
#include <functional>
#include <vector>

using lsw::host::platform::MountTable;
using lsw::host::platform::WindowsFsLauncher;
using lsw::host::services::MountStatus;
using lsw::host::services::StatusHub;
using lsw::host::services::VmStatus;
using lsw::host::services::WindowsMount;
using State = MountStatus::State;
using K = lsw::host::config::ManagerSettings::Key;

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

class FakeHelper : public WindowsFsLauncher {
public:
    struct Call {
        QString verb;
        QString share;
        QString user;
        QString password;
    };

    QString unavailable() const override { return missing; }

    bool owns_mount() const override { return owned; }

    explicit FakeHelper(QString mount_point) : point(std::move(mount_point)) {}

    void mount(const QString& share, const QString& user, const QString& password,
               Done done) override {
        calls.push_back({QStringLiteral("mount"), share, user, password});
        finish(std::move(done), [this, share] {
            add(share, point);
            owned = true;
        });
    }

    void unmount(Done done) override {
        calls.push_back({QStringLiteral("unmount"), {}, {}, {}});
        finish(std::move(done), [this] {
            drop(point);
            owned = false;
        });
    }

    Result unmount_now() override {
        calls.push_back({QStringLiteral("unmount_now"), {}, {}, {}});
        drop(point);
        owned = false;
        return {true, {}};
    }

    void add(const QString& share, const QString& point) {
        const QString source =
            QStringLiteral("//127.0.0.1") + share.mid(share.indexOf(QLatin1Char('/')));
        table += QStringLiteral("100 28 0:50 / %1 rw - fuse.lsw-windows-fs %2 rw,user_id=1000\n")
                     .arg(point, source)
                     .toUtf8();
    }

    void drop(const QString& point) {
        QByteArray kept;
        for (const QByteArray& line : table.split('\n')) {
            if (!line.isEmpty() && !line.contains(" " + point.toUtf8() + " ")) {
                kept += line + '\n';
            }
        }
        table = kept;
    }

    void complete(bool ok = true, const QString& error = {}) {
        auto run = std::move(held);
        held = nullptr;
        if (run) {
            run(ok, error);
        }
    }

    int count(const QString& verb) const {
        int n = 0;
        for (const auto& call : calls) {
            n += call.verb == verb ? 1 : 0;
        }
        return n;
    }

    QString point;
    QByteArray table;
    QString missing;
    bool owned = false;
    bool hold = false;
    bool next_ok = true;
    QString next_error;
    std::vector<Call> calls;
    std::function<void(bool, const QString&)> held;

private:
    void finish(Done done, std::function<void()> apply) {
        auto run = [this, done, apply](bool ok, const QString& error) {
            if (ok) {
                apply();
            }
            done({ok, error});
        };
        if (hold) {
            held = run;
        } else {
            run(next_ok, next_error);
        }
    }
};

VmStatus machine(const QUuid& uuid, bool running, bool online) {
    VmStatus status;
    status.uuid = uuid;
    status.stats.running = running;
    status.stats.agent_reachable = online;
    return status;
}

}  // namespace

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("XDG_CACHE_HOME", home.path().toUtf8());
    QApplication app(argc, argv);

    lsw::host::link::AgentRpcClient rpc;
    lsw::host::config::Settings settings;
    lsw::host::config::ManagerSettings manager_settings;
    lsw::host::vm::LibvirtConnection libvirt;
    lsw::host::vm::Lifecycle lifecycle{libvirt};
    NoProbe probe;
    StatusHub hub{libvirt, lifecycle, rpc, probe, settings, manager_settings};

    const QUuid vm = QUuid::createUuid();
    const QString point = home.filePath(QStringLiteral("Windows"));
    manager_settings.set_value(K::kSmbEnabled, true);
    manager_settings.set_value(K::kShareScope, QStringLiteral("C$"));
    manager_settings.set_value(K::kGuestUser, QStringLiteral("alice"));
    manager_settings.set_value(K::kGuestPassword, QStringLiteral("secret"));

    {
        FakeHelper helper{point};
        helper.add(QStringLiteral("127.0.0.1:5445/C$"), point);
        WindowsMount mount{hub, manager_settings, helper, point,
                           [&] { return MountTable::parse(helper.table); }};
        mount.start(false);
        check(helper.count(QStringLiteral("unmount")) == 1 && hub.mount().state == State::NoGuest,
              "at start with Windows stopped, a stale mount of the guest is cleared");
        check(!helper.table.contains(point.toUtf8()), "and it is gone from the table");
    }

    {
        FakeHelper helper{point};
        helper.add(QStringLiteral("127.0.0.1:5445/C$"), point);
        WindowsMount mount{hub, manager_settings, helper, point,
                           [&] { return MountTable::parse(helper.table); }};
        mount.start(true);
        check(helper.calls.empty(),
              "at start with Windows running nothing is touched before its status is known");
        hub.set_vm(machine(vm, true, false));
        check(helper.count(QStringLiteral("unmount")) == 1 && hub.mount().state == State::Waiting,
              "a mount left by an earlier run is not adopted, it is unmounted");
        hub.set_vm(machine(vm, false, false));
    }

    std::vector<QString> hooked;
    FakeHelper helper{point};
    WindowsMount mount{hub,
                       manager_settings,
                       helper,
                       point,
                       [&] { return MountTable::parse(helper.table); },
                       [&](const QString& where) { hooked.push_back(where); }};
    mount.start(false);
    check(hub.mount().state == State::NoGuest && helper.calls.empty(),
          "stopped guest, nothing mounted: status says why");

    hub.set_vm(machine(vm, true, false));
    check(helper.calls.empty() && hub.mount().state == State::Waiting,
          "VM running but the agent silent: no mount, waiting");

    manager_settings.set_value(K::kSmbEnabled, false);
    hub.set_vm(machine(vm, true, true));
    check(helper.calls.empty() && hub.mount().state == State::Off,
          "agent answering but the setting off: no mount");
    manager_settings.set_value(K::kSmbEnabled, true);
    check(helper.count(QStringLiteral("mount")) == 1, "turning the setting on mounts");
    check(helper.calls.back().share == QStringLiteral("127.0.0.1:5445/C$") &&
              helper.calls.back().user == QStringLiteral("alice") &&
              helper.calls.back().password == QStringLiteral("secret"),
          "the launcher gets the share and the credentials, no path");
    check(hub.mount().state == State::Mounted && hub.mount().path == point,
          "status says mounted at the path");
    check(hooked.size() == 1 && hooked[0] == point,
          "the indexer hook runs once with the mount point");
    hub.set_vm(machine(vm, true, true));
    hub.set_vm(machine(vm, true, true));
    check(helper.count(QStringLiteral("mount")) == 1, "further status ticks do not mount again");

    manager_settings.set_value(K::kShareScope, QStringLiteral("profile"));
    check(helper.count(QStringLiteral("unmount")) == 1 &&
              helper.count(QStringLiteral("mount")) == 2 &&
              helper.calls.back().share == QStringLiteral("127.0.0.1:5445/Users"),
          "changing the scope unmounts and mounts the other share");

    hub.set_vm(machine(vm, false, false));
    check(helper.count(QStringLiteral("unmount")) == 2 && hub.mount().state == State::NoGuest &&
              MountTable::parse(helper.table).entries().isEmpty(),
          "the VM stopping unmounts");

    hub.set_vm(machine(vm, true, true));
    check(helper.count(QStringLiteral("mount")) == 3 && hub.mount().state == State::Mounted,
          "the VM running with an answering agent mounts again");
    manager_settings.set_value(K::kSmbEnabled, false);
    check(helper.count(QStringLiteral("unmount")) == 3 && hub.mount().state == State::Off,
          "turning the setting off unmounts");
    manager_settings.set_value(K::kSmbEnabled, true);
    check(helper.count(QStringLiteral("mount")) == 4, "and on again mounts");

    mount.unmount_for_shutdown();
    check(helper.count(QStringLiteral("unmount_now")) == 1 &&
              MountTable::parse(helper.table).entries().isEmpty(),
          "the host shutting down unmounts at once, before the guest is powered off");
    hub.set_vm(machine(vm, true, true));
    check(helper.count(QStringLiteral("mount")) == 4, "and nothing mounts again during shutdown");
    hub.set_vm(machine(vm, false, false));
    hub.set_vm(machine(vm, true, true));

    {
        FakeHelper failing{point};
        failing.next_ok = false;
        failing.next_error = QStringLiteral("mounting failed");
        WindowsMount retry{hub, manager_settings, failing, point,
                           [&] { return MountTable::parse(failing.table); }};
        hub.set_vm(machine(vm, false, false));
        retry.start(false);
        hub.set_vm(machine(vm, true, true));
        check(failing.count(QStringLiteral("mount")) == 1 && hub.mount().state == State::Failed &&
                  hub.mount().detail == QStringLiteral("mounting failed"),
              "a failed mount is reported with the helper's message");
        hub.set_vm(machine(vm, true, true));
        hub.set_vm(machine(vm, true, true));
        check(failing.count(QStringLiteral("mount")) == 1,
              "and is not retried while nothing changed");
        hub.set_vm(machine(vm, false, false));
        hub.set_vm(machine(vm, true, true));
        check(failing.count(QStringLiteral("mount")) == 2, "a new boot tries again");
        failing.next_ok = true;
        manager_settings.set_value(K::kGuestPassword, QStringLiteral("changed"));
        check(failing.count(QStringLiteral("mount")) == 3 && hub.mount().state == State::Mounted,
              "a changed setting tries again");
        hub.set_vm(machine(vm, false, false));
    }

    {
        FakeHelper absent{point};
        absent.missing = QStringLiteral("fusermount3 is not installed");
        WindowsMount none{hub, manager_settings, absent, point,
                          [&] { return MountTable::parse(absent.table); }};
        none.start(false);
        hub.set_vm(machine(vm, true, true));
        hub.set_vm(machine(vm, true, true));
        check(absent.calls.empty() && hub.mount().state == State::Unavailable &&
                  hub.mount().detail == QStringLiteral("fusermount3 is not installed"),
              "a missing tool is a status, not a mount attempt or a retry");
        hub.set_vm(machine(vm, false, false));
    }

    {
        FakeHelper slow{point};
        slow.hold = true;
        WindowsMount pending{hub, manager_settings, slow, point,
                             [&] { return MountTable::parse(slow.table); }};
        pending.start(false);
        hub.set_vm(machine(vm, true, true));
        check(slow.count(QStringLiteral("mount")) == 1 && hub.mount().state == State::Mounting,
              "a mount in flight shows as mounting");
        hub.set_vm(machine(vm, true, true));
        manager_settings.set_value(K::kGuestUser, QStringLiteral("bob"));
        check(slow.count(QStringLiteral("mount")) == 1, "nothing else runs while it is in flight");
        slow.hold = false;
        slow.complete();
        check(hub.mount().state == State::Mounted && slow.count(QStringLiteral("mount")) == 1,
              "when it finishes the state is mounted");
    }

    return g_failures == 0 ? 0 : 1;
}
