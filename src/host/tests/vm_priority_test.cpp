#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/manager/settings_page.hpp"
#include "lsw/host/platform/cpu_scope.hpp"
#include "lsw/host/platform/process_threads.hpp"
#include "lsw/host/services/priority_status.hpp"
#include "lsw/host/services/vm_priority.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <unistd.h>
#include <utility>

using lsw::host::config::ManagerSettings;
using lsw::host::manager::SettingsPage;
using lsw::host::platform::ICpuScope;
using lsw::host::platform::IProcessThreads;
using lsw::host::platform::ProcProcessThreads;
using lsw::host::platform::SystemdCpuScope;
using lsw::host::services::PriorityStatus;
using lsw::host::services::VmPriority;
using K = ManagerSettings::Key;
using Level = PriorityStatus::Level;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

constexpr qint64 kQemu = 5000;
constexpr qint64 kOld = 60000;
constexpr qint64 kYoung = 10;

struct FakeScope : ICpuScope {
    QSet<qint64> inside;
    QHash<qint64, int> weights;
    QStringList calls;
    QVector<std::function<void()>> waiting;
    bool refuse = false;
    bool controller = true;
    bool slow = false;

    void answer(Done done, bool ok) {
        const auto reply = [done = std::move(done), ok] { done(ok, ok ? QString() : QStringLiteral("refused")); };
        if (slow) {
            waiting.push_back(reply);
        } else {
            reply();
        }
    }
    void answer_all() {
        for (const auto& reply : std::exchange(waiting, {})) {
            reply();
        }
    }

    bool holds(qint64 pid) override { return inside.contains(pid); }
    std::optional<int> weight(qint64 pid) override {
        if (!controller) {
            return std::nullopt;
        }
        return weights.value(pid, VmPriority::kDefaultWeight);
    }
    void enter(qint64 pid, int weight, Done done) override {
        calls << QStringLiteral("enter %1 %2").arg(pid).arg(weight);
        if (!refuse) {
            inside.insert(pid);
            weights.insert(pid, weight);
        }
        answer(std::move(done), !refuse);
    }
    void set_weight(qint64 pid, int weight, Done done) override {
        calls << QStringLiteral("weight %1 %2").arg(pid).arg(weight);
        const bool ok = !refuse && inside.contains(pid);
        if (ok) {
            weights.insert(pid, weight);
        }
        answer(std::move(done), ok);
    }
};

struct FakeThreads : IProcessThreads {
    QHash<qint64, QMap<qint64, Thread>> processes;
    QHash<qint64, int> lowest;
    QSet<qint64> stubborn;
    QStringList sets;
    int lists = 0;
    int reads = 0;

    void add(qint64 pid, qint64 tid, const char* name, qint64 age_ms = kOld, int nice = 0) {
        processes[pid].insert(tid, Thread{QString::fromLatin1(name), nice, age_ms});
    }
    [[nodiscard]] int nice(qint64 pid, qint64 tid) const { return processes[pid][tid].nice; }
    [[nodiscard]] QSet<qint64> at(qint64 pid, int nice) const {
        QSet<qint64> out;
        const auto& threads = processes[pid];
        for (auto it = threads.cbegin(); it != threads.cend(); ++it) {
            if (it->nice == nice) {
                out.insert(it.key());
            }
        }
        return out;
    }

    QVector<qint64> list(qint64 pid) override {
        ++lists;
        return processes.value(pid).keys().toVector();
    }
    std::optional<Thread> read(qint64 pid, qint64 tid) override {
        ++reads;
        const auto threads = processes.value(pid);
        return threads.contains(tid) ? std::optional<Thread>{threads[tid]} : std::nullopt;
    }
    int lowest_nice(qint64 pid) override { return lowest.value(pid, -11); }
    bool set_nice(qint64 tid, int nice) override {
        sets << QStringLiteral("%1=%2").arg(tid).arg(nice);
        if (stubborn.contains(tid) && nice < 0) {
            return false;
        }
        for (auto& threads : processes) {
            if (threads.contains(tid)) {
                threads[tid].nice = nice;
            }
        }
        return true;
    }
};

struct Machine {
    FakeScope scope;
    FakeScope mirror_scope;
    FakeThreads threads;
    qint64 pid = kQemu;
    int lookups = 0;
    int changes = 0;
    VmPriority priority;

    explicit Machine(ManagerSettings& settings)
        : priority(settings, scope, mirror_scope, threads, [this] {
              ++lookups;
              return pid;
          }) {
        QObject::connect(&priority, &VmPriority::changed, [this] { ++changes; });
    }

    void boot(qint64 qemu = kQemu) {
        pid = qemu;
        threads.add(qemu, qemu, "qemu-system-x86");
        threads.add(qemu, qemu + 1, "qemu-system-x86");
        threads.add(qemu, qemu + 2, "IO mon_iothread");
        threads.add(qemu, qemu + 3, "call_rcu");
        threads.add(qemu, qemu + 4, "CPU 0/KVM");
        threads.add(qemu, qemu + 5, "CPU 1/KVM");
        threads.add(qemu, qemu + 6, "worker");
        threads.add(qemu, qemu + 7, "av-context");
        threads.add(qemu, qemu + 8, "dxvk-submit");
        threads.add(qemu, qemu + 9, "dxvk-shader-n");
        threads.add(qemu, qemu + 10, "qemu-sy:disk$0");
        threads.add(qemu, qemu + 11, "qemu-syste:cs0");
        threads.add(qemu, qemu + 12, "vkd3d_queue");
    }

    [[nodiscard]] static QSet<qint64> frame(qint64 qemu = kQemu) {
        return {qemu, qemu + 1, qemu + 2, qemu + 7, qemu + 8, qemu + 11, qemu + 12};
    }
};

void set(ManagerSettings& settings, const char* level) {
    settings.set_value(K::kPriority, QString::fromLatin1(level));
}

void names_are_told_apart() {
    const QString main = QStringLiteral("qemu-system-x86");
    check(VmPriority::level_of(QStringLiteral("high")) == Level::High &&
              VmPriority::level_of(QStringLiteral("highest")) == Level::Highest &&
              VmPriority::level_of(QStringLiteral("anything")) == Level::Normal,
          "the stored value names the level, and an unknown one is Normal");
    check(ManagerSettings::default_value(K::kPriority).toString() == QLatin1String("normal") &&
              !ManagerSettings::needs_restart(K::kPriority),
          "priority is Normal unless chosen, and needs no restart");
    bool frame = true;
    for (const char* name : {"qemu-system-x86", "IO mon_iothread", "IO iothread1", "av-context", "dxvk-cs",
                             "dxvk-submit", "dxvk-queue", "dxvk-frame", "dxvk-descriptor", "vkd3d_queue",
                             "vkd3d_fence", "vkd3d-swapchain", "qemu-sys:gdrv0", "qemu-syste:cs0"}) {
        frame = frame && VmPriority::on_frame_path(QString::fromLatin1(name), main);
    }
    check(frame, "the main loop, I/O threads, device and executor threads are on the frame path");
    bool other = true;
    for (const char* name : {"CPU 0/KVM", "CPU 31/KVM", "worker", "call_rcu", "dxvk-shader-n", "dxvk-cache",
                             "vkd3d-disk$", "qemu-sy:disk$0", "qemu-syste:sh3", "qemu-s:traceq0",
                             "kvm-nx-lpage-re", "PipeWire thread"}) {
        other = other && !VmPriority::on_frame_path(QString::fromLatin1(name), main);
    }
    check(other, "vCPUs, disk workers, shader compilers and caches are not");
    check(!VmPriority::on_frame_path(QStringLiteral("CPU 0/KVM"), QStringLiteral("CPU 0/KVM")) &&
              VmPriority::runs_guest_code(QStringLiteral("CPU 12/KVM")),
          "a vCPU thread is never on the frame path, whatever the main thread is called");
}

void normal_touches_nothing(ManagerSettings& settings) {
    set(settings, "normal");
    Machine machine(settings);
    machine.boot();
    machine.threads.add(7000, 7000, "remotewin");
    machine.priority.vm_seen(1);
    machine.priority.mirror_started(7000);
    machine.priority.vm_seen(1);
    check(machine.scope.calls.isEmpty() && machine.mirror_scope.calls.isEmpty() &&
              machine.threads.sets.isEmpty(),
          "Normal: no scope is made for the guest or a mirror, no weight set, no thread touched");
    check(machine.lookups == 0 && machine.threads.lists == 0 && machine.threads.reads == 0,
          "Normal: the guest's process is not even looked at");
    check(machine.priority.status().wanted == Level::Normal && !machine.priority.status().weight_raised &&
              !machine.priority.status().threads_raised,
          "Normal: the status shows nothing raised");
}

void high_weighs_the_guest(ManagerSettings& settings) {
    set(settings, "high");
    Machine machine(settings);
    machine.boot();
    machine.threads.add(7000, 7000, "remotewin");
    machine.priority.vm_seen(1);
    check(machine.scope.calls == QStringList{QStringLiteral("enter 5000 1000")},
          "High: the guest's process enters its own scope with weight 1000");
    check(!machine.priority.status().weight_raised,
          "High: the weight is not shown as raised before it is seen in force");
    machine.priority.vm_seen(1);
    machine.priority.mirror_started(7000);
    machine.priority.vm_seen(1);
    check(machine.priority.status().weight_raised && !machine.priority.status().weight_refused,
          "High: the weight is shown as raised once the scope holds the guest at it");
    check(machine.scope.calls.size() == 1, "High: the scope is asked for once, not at every update");
    check(machine.threads.sets.isEmpty() && machine.threads.lists == 0,
          "High: no thread of the guest or of a mirror is touched or listed");
    check(machine.mirror_scope.calls == QStringList{QStringLiteral("enter 7000 1000")} &&
              machine.mirror_scope.weights.value(7000) == 1000,
          "High: a mirror that starts enters its own scope with weight 1000");
}

void highest_raises_the_frame_path(ManagerSettings& settings) {
    set(settings, "highest");
    Machine machine(settings);
    machine.boot();
    machine.priority.vm_seen(1);
    machine.priority.vm_seen(1);
    check(machine.scope.calls == QStringList{QStringLiteral("enter 5000 1000")} &&
              machine.priority.status().weight_raised,
          "Highest: the guest gets the weight as under High");
    check(machine.threads.at(kQemu, -10) == Machine::frame(),
          "Highest: exactly the frame-path threads run at nice -10");
    check(machine.threads.nice(kQemu, kQemu + 4) == 0 && machine.threads.nice(kQemu, kQemu + 5) == 0 &&
              !machine.threads.sets.join(' ').contains(QStringLiteral("5004=")) &&
              !machine.threads.sets.join(' ').contains(QStringLiteral("5005=")),
          "Highest: no vCPU thread is touched");
    check(machine.priority.status().threads_raised && machine.priority.status().nice == -10 &&
              !machine.priority.status().clamped() && !machine.priority.status().threads_refused,
          "Highest: the status shows the threads raised to -10");

    const int reads = machine.threads.reads;
    const qsizetype sets = machine.threads.sets.size();
    machine.priority.vm_seen(1);
    check(machine.threads.reads == reads && machine.threads.sets.size() == sets,
          "an update with no new thread reads and sets nothing again");

    machine.threads.add(kQemu, 5100, "av-context");
    machine.threads.add(kQemu, 5101, "dxvk-shader-h");
    machine.threads.add(kQemu, 5102, "av-context", kYoung);
    machine.priority.vm_seen(1);
    check(machine.threads.nice(kQemu, 5100) == -10 && machine.threads.nice(kQemu, 5101) == 0,
          "a frame-path thread that appears later is raised at the next update, another is not");
    check(machine.threads.nice(kQemu, 5102) == 0,
          "a thread too young to carry its final name is left for a later update");
    machine.threads.processes[kQemu][5102].age_ms = kOld;
    machine.threads.processes[kQemu][5102].name = QStringLiteral("dxvk-cs");
    machine.priority.vm_seen(1);
    check(machine.threads.nice(kQemu, 5102) == -10, "and is raised once it is old enough");

    machine.threads.add(kQemu, 5103, "dxvk-shader-n", kOld, -10);
    machine.threads.add(kQemu, 5104, "CPU 2/KVM", kOld, -10);
    machine.priority.vm_seen(1);
    check(machine.threads.nice(kQemu, 5103) == 0 && machine.threads.nice(kQemu, 5104) == 0,
          "a thread off the frame path that inherited the raised nice, a vCPU included, is put back");

    machine.threads.processes[kQemu].remove(5100);
    const int changes = machine.changes;
    set(settings, "high");
    QSet<qint64> expected = Machine::frame();
    expected.insert(5102);
    QSet<qint64> restored;
    for (const QString& call : machine.threads.sets.mid(machine.threads.sets.lastIndexOf(QStringLiteral("5104=0")) + 1)) {
        if (call.endsWith(QLatin1String("=0"))) {
            restored.insert(call.section('=', 0, 0).toLongLong());
        }
    }
    check(machine.threads.at(kQemu, -10).isEmpty() && restored == expected,
          "back to High: exactly the threads it raised return to nice 0");
    check(machine.priority.status().weight_raised && !machine.priority.status().threads_raised &&
              machine.changes == changes + 1 && machine.scope.calls.size() == 1,
          "back to High: the weight stays and the status follows at once");

    set(settings, "normal");
    check(machine.scope.calls.last() == QLatin1String("weight 5000 100") &&
              machine.scope.inside.contains(kQemu) && !machine.priority.status().weight_raised,
          "back to Normal: the weight returns to the default and the guest stays in its scope");
    const qsizetype calls = machine.scope.calls.size();
    const qsizetype thread_sets = machine.threads.sets.size();
    machine.priority.vm_seen(1);
    check(machine.scope.calls.size() == calls && machine.threads.sets.size() == thread_sets,
          "and Normal then touches nothing more");

    set(settings, "highest");
    check(machine.scope.calls.last() == QLatin1String("weight 5000 1000") &&
              machine.threads.at(kQemu, -10) == expected,
          "the setting applies the moment it changes, reusing the scope the guest is in");
}

void a_refused_scope_does_not_stop_the_rest(ManagerSettings& settings) {
    set(settings, "highest");
    Machine machine(settings);
    machine.scope.refuse = true;
    machine.boot();
    machine.priority.vm_seen(1);
    machine.priority.vm_seen(1);
    machine.priority.vm_seen(1);
    check(machine.scope.calls.size() == 1, "a refused scope is asked for once, not at every update");
    check(machine.priority.status().weight_refused && !machine.priority.status().weight_raised,
          "a refused scope is reported and the weight is not shown as raised");
    check(machine.threads.at(kQemu, -10) == Machine::frame() && machine.priority.status().threads_raised,
          "the threads are raised all the same");
    machine.scope.refuse = false;
    set(settings, "high");
    machine.priority.vm_seen(1);
    check(machine.scope.calls.size() == 2 && machine.priority.status().weight_raised &&
              !machine.priority.status().weight_refused,
          "choosing a level again tries the scope again");

    Machine undelegated(settings);
    undelegated.scope.controller = false;
    undelegated.boot();
    undelegated.priority.vm_seen(1);
    undelegated.priority.vm_seen(1);
    check(undelegated.priority.status().weight_refused && !undelegated.priority.status().weight_raised,
          "a scope whose weight is not in force is reported as refused");
}

void limits_are_respected_and_reported(ManagerSettings& settings) {
    set(settings, "highest");
    {
        Machine machine(settings);
        machine.threads.lowest.insert(kQemu, -5);
        machine.boot();
        machine.priority.vm_seen(1);
        check(machine.threads.at(kQemu, -5) == Machine::frame() && machine.threads.at(kQemu, -10).isEmpty(),
              "a limit of -5 gives the threads nice -5");
        check(machine.priority.status().clamped() && machine.priority.status().nice == -5 &&
                  machine.priority.status().wanted_nice == -10,
              "and the status says the nice was clamped");
        check(SettingsPage::priority_note(machine.priority.status()).contains(QStringLiteral("nice -5 rather than -10")),
              "and so does the caption");
    }
    {
        Machine machine(settings);
        machine.threads.lowest.insert(kQemu, 0);
        machine.boot();
        machine.priority.vm_seen(1);
        machine.priority.vm_seen(1);
        check(machine.threads.sets.isEmpty(), "limits that allow nothing below 0 mean no thread is touched");
        check(machine.priority.status().threads_refused && !machine.priority.status().threads_raised &&
                  machine.priority.status().weight_raised,
              "which is reported, while the weight still applies");
        check(SettingsPage::priority_note(machine.priority.status()).contains(QStringLiteral("stay at normal priority")),
              "and the caption says the threads stay at normal priority");
    }
    {
        Machine machine(settings);
        machine.threads.stubborn.insert(kQemu + 7);
        machine.boot();
        machine.priority.vm_seen(1);
        machine.priority.vm_seen(1);
        QSet<qint64> expected = Machine::frame();
        expected.remove(kQemu + 7);
        check(machine.threads.at(kQemu, -10) == expected, "a thread the system refuses does not stop the others");
        check(machine.priority.status().threads_refused && machine.priority.status().threads_raised,
              "and the refusal is reported");
        check(machine.threads.sets.count(QStringLiteral("5007=-10")) == 1,
              "and the refused thread is not retried at every update");
    }
    {
        Machine machine(settings);
        machine.pid = kQemu;
        machine.threads.add(kQemu, kQemu, "qemu-system-x86");
        machine.threads.add(kQemu, kQemu + 1, "qemu-system-x86");
        machine.threads.add(kQemu, kQemu + 2, "qemu-system-x86");
        machine.priority.vm_seen(1);
        check(machine.threads.sets.isEmpty() && machine.priority.status().threads_unnamed &&
                  !machine.priority.status().threads_raised,
              "a QEMU whose threads carry no names gets no thread raised, and that is reported");
    }
}

void a_running_guest_is_taken_over(ManagerSettings& settings) {
    set(settings, "highest");
    Machine machine(settings);
    machine.boot();
    machine.scope.inside.insert(kQemu);
    machine.scope.weights.insert(kQemu, 1000);
    for (const qint64 tid : Machine::frame()) {
        machine.threads.processes[kQemu][tid].nice = -10;
    }
    machine.priority.vm_seen(4);
    machine.priority.vm_seen(4);
    check(machine.scope.calls == QStringList{QStringLiteral("weight 5000 1000")},
          "a guest already running in its scope is not moved again; its weight is set");
    check(machine.threads.sets.isEmpty() && machine.priority.status().threads_raised &&
              machine.priority.status().weight_raised,
          "threads already at the raised nice are counted, not set again");
    set(settings, "high");
    check(machine.threads.at(kQemu, 0).contains(Machine::frame()),
          "and they return to nice 0 when the level drops");

    set(settings, "high");
    Machine late(settings);
    late.boot();
    late.priority.vm_seen(2);
    late.priority.vm_seen(2);
    check(late.scope.calls == QStringList{QStringLiteral("enter 5000 1000")} && late.priority.status().weight_raised,
          "a manager that starts with the guest running applies the level at the first update");
}

void a_restarted_guest_gets_the_level_again(ManagerSettings& settings) {
    set(settings, "highest");
    Machine machine(settings);
    machine.boot();
    machine.priority.vm_seen(1);
    machine.priority.vm_seen(1);
    machine.threads.processes.remove(kQemu);
    machine.priority.vm_seen(-1);
    check(!machine.priority.status().vm_running && !machine.priority.status().weight_raised &&
              !machine.priority.status().threads_raised,
          "a stopped guest shows nothing raised");
    check(SettingsPage::priority_note(machine.priority.status()).contains(QStringLiteral("once the guest runs")),
          "and the caption says the level waits for the guest");
    machine.boot(6000);
    machine.priority.vm_seen(2);
    machine.priority.vm_seen(2);
    check(machine.scope.calls.last() == QLatin1String("enter 6000 1000") &&
              machine.threads.at(6000, -10) == Machine::frame(6000) && machine.priority.status().weight_raised,
          "the next guest process gets its scope, weight and thread priorities");
}

void mirrors_follow_the_level(ManagerSettings& settings) {
    set(settings, "highest");
    Machine machine(settings);
    machine.boot();
    machine.priority.vm_seen(1);
    machine.threads.add(7000, 7000, "remotewin", kYoung);
    machine.threads.add(7500, 7500, "remotewin", kYoung);
    const qsizetype sets = machine.threads.sets.size();
    machine.priority.mirror_started(7000);
    machine.priority.mirror_started(7500);
    check(machine.mirror_scope.calls ==
              (QStringList{QStringLiteral("enter 7000 1000"), QStringLiteral("enter 7500 1000")}),
          "Highest: an application mirror and the desktop mirror each enter a scope with weight 1000 when spawned");
    check(machine.threads.sets.size() == sets && machine.threads.nice(7000, 7000) == 0,
          "Highest: a mirror's threads keep their nice; alone in its scope the weight is what counts");
    check(machine.scope.calls == QStringList{QStringLiteral("enter 5000 1000")},
          "a mirror never goes through the guest's scope");
    machine.priority.vm_seen(1);
    machine.priority.vm_seen(1);
    check(machine.mirror_scope.calls.size() == 2 && !machine.priority.status().mirrors_refused,
          "a mirror's scope is asked for once");

    set(settings, "high");
    check(machine.mirror_scope.calls.size() == 2 && machine.mirror_scope.weights.value(7000) == 1000,
          "Highest to High: the mirrors keep their weight");
    set(settings, "normal");
    check(machine.mirror_scope.weights.value(7000) == 100 && machine.mirror_scope.weights.value(7500) == 100 &&
              machine.scope.weights.value(kQemu) == 100,
          "Normal: the running mirrors and the guest return to weight 100");
    const qsizetype calls = machine.mirror_scope.calls.size();
    machine.threads.add(7100, 7100, "remotewin", kYoung);
    machine.priority.mirror_started(7100);
    machine.priority.vm_seen(1);
    check(machine.mirror_scope.calls.size() == calls, "Normal: a mirror spawned now is left alone, the others are not set again");

    set(settings, "high");
    check(QSet<QString>(machine.mirror_scope.calls.cbegin() + calls, machine.mirror_scope.calls.cend()) ==
              (QSet<QString>{QStringLiteral("weight 7000 1000"), QStringLiteral("weight 7500 1000"),
                             QStringLiteral("enter 7100 1000")}),
          "High again: every running mirror is weighted at once, reusing the scope it is in");

    machine.priority.mirror_ended(7100);
    machine.priority.mirror_ended(7500);
    const qsizetype after = machine.mirror_scope.calls.size();
    set(settings, "normal");
    check(machine.mirror_scope.calls.mid(after) == QStringList{QStringLiteral("weight 7000 100")},
          "a mirror that ended is forgotten");
}

void a_refused_mirror_scope_is_reported(ManagerSettings& settings) {
    set(settings, "high");
    Machine machine(settings);
    machine.mirror_scope.refuse = true;
    machine.boot();
    machine.priority.vm_seen(1);
    machine.priority.vm_seen(1);
    machine.priority.mirror_started(7000);
    machine.priority.vm_seen(1);
    machine.priority.vm_seen(1);
    check(machine.mirror_scope.calls.size() == 1 && machine.priority.status().mirrors_refused,
          "a refused mirror scope is reported and not asked for again at every update");
    check(machine.priority.status().weight_raised && !machine.priority.status().weight_refused,
          "and the guest keeps its weight");
    check(SettingsPage::priority_note(machine.priority.status()).contains(QStringLiteral("window mirrors run with the normal share")) &&
              !SettingsPage::priority_note(machine.priority.status()).contains(QStringLiteral("Not in effect")),
          "the caption says the mirrors run with the normal share, and only that");
    machine.priority.mirror_ended(7000);
    check(!machine.priority.status().mirrors_refused, "the report goes when that mirror ends");

    Machine undelegated(settings);
    undelegated.mirror_scope.controller = false;
    undelegated.boot();
    undelegated.priority.vm_seen(1);
    undelegated.priority.mirror_started(7000);
    check(!undelegated.priority.status().mirrors_refused, "a mirror's weight is not judged before the next update");
    undelegated.priority.vm_seen(1);
    check(undelegated.priority.status().mirrors_refused && undelegated.priority.status().weight_raised,
          "a mirror scope whose weight is not in force is reported as refused");
}

void answers_may_come_late(ManagerSettings& settings) {
    set(settings, "high");
    Machine machine(settings);
    machine.scope.slow = true;
    machine.mirror_scope.slow = true;
    machine.boot();
    machine.priority.vm_seen(1);
    machine.priority.mirror_started(7000);
    machine.priority.vm_seen(1);
    machine.priority.vm_seen(1);
    check(machine.scope.calls.size() == 1 && machine.mirror_scope.calls.size() == 1 &&
              !machine.priority.status().weight_raised,
          "while the answer is awaited nothing is asked twice and nothing is shown as raised");
    set(settings, "normal");
    check(machine.scope.calls.size() == 1 && machine.mirror_scope.calls.size() == 1,
          "a level chosen meanwhile waits for the answer");
    machine.scope.answer_all();
    machine.mirror_scope.answer_all();
    machine.priority.vm_seen(1);
    machine.scope.answer_all();
    machine.mirror_scope.answer_all();
    check(machine.scope.calls.last() == QLatin1String("weight 5000 100") &&
              machine.mirror_scope.calls.last() == QLatin1String("weight 7000 100") &&
              machine.scope.weights.value(kQemu) == 100 && machine.mirror_scope.weights.value(7000) == 100 &&
              !machine.priority.status().weight_raised,
          "and is applied once it has come");

    set(settings, "high");
    machine.priority.mirror_started(7200);
    machine.priority.mirror_ended(7200);
    machine.threads.processes.remove(kQemu);
    machine.priority.vm_seen(-1);
    machine.scope.answer_all();
    machine.mirror_scope.answer_all();
    check(!machine.priority.status().weight_raised && !machine.priority.status().mirrors_refused &&
              !machine.priority.status().vm_running,
          "an answer for a process that is gone changes nothing");
}

void the_caption_shows_only_what_holds() {
    PriorityStatus status;
    check(!SettingsPage::priority_note(status).contains(QStringLiteral("Not in effect")) &&
              SettingsPage::priority_note(status).contains(QStringLiteral("like any program")),
          "Normal reads as nothing changed");
    status.wanted = Level::High;
    status.vm_running = true;
    status.weight_refused = true;
    check(SettingsPage::priority_note(status).contains(QStringLiteral("Not in effect")),
          "High with a refused scope reads as not in effect");
    status.wanted = Level::Highest;
    status.threads_unnamed = true;
    check(SettingsPage::priority_note(status).contains(QStringLiteral("Not in effect")) &&
              SettingsPage::priority_note(status).contains(QStringLiteral("carry no names")),
          "Highest with neither part applied says so for both");
}

void the_system_side_reads_this_process() {
    ProcProcessThreads threads;
    const qint64 self = getpid();
    const auto own = threads.list(self);
    const auto main = threads.read(self, self);
    check(own.contains(self) && main.has_value() && !main->name.isEmpty() && main->age_ms >= 0,
          "the threads of a process are listed and read with name, nice and age");
    check(threads.list(0x7ffffff0).isEmpty() && !threads.read(0x7ffffff0, 0x7ffffff0).has_value(),
          "a process that does not exist has no threads");
    const auto parsed = ProcProcessThreads::parse_stat(
        "5007 (av) context) S 1 5000 5000 0 -1 4194368 12 0 0 0 3 4 0 0 20 -10 300 0 123456 1000 10 "
        "18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0");
    check(parsed.has_value() && parsed->nice == -10 && parsed->start_ticks == 123456,
          "nice and start time are read past a name with a bracket in it");
    check(!ProcProcessThreads::parse_stat("garbage").has_value(), "a malformed stat line is refused");
    check(ProcProcessThreads::lowest_nice_of("Max msgqueue size 819200 819200 bytes\n"
                                             "Max nice priority         31                   31\n") == -11 &&
              ProcProcessThreads::lowest_nice_of("Max nice priority         0                    0\n") == 0 &&
              ProcProcessThreads::lowest_nice_of("Max nice priority         40                   40\n") == -20 &&
              ProcProcessThreads::lowest_nice_of("") == 0,
          "the lowest nice a process may take comes from its limits");
    SystemdCpuScope scope(QStringLiteral("lsw-vm"));
    check(scope.unit_name(5000) == QLatin1String("lsw-vm-5000.scope") &&
              SystemdCpuScope(QStringLiteral("lsw-mirror")).unit_name(7000) == QLatin1String("lsw-mirror-7000.scope") &&
              SystemdCpuScope::group_of("0::/user.slice/app.slice/lsw-vm-5000.scope\n") ==
                  QLatin1String("/user.slice/app.slice/lsw-vm-5000.scope") &&
              SystemdCpuScope::group_of("1:name=systemd:/x\n").isEmpty(),
          "a scope is named after its kind and process and found in the cgroup line");
    check(!scope.holds(self), "this process is not in a guest scope");
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    ManagerSettings settings;
    names_are_told_apart();
    normal_touches_nothing(settings);
    high_weighs_the_guest(settings);
    highest_raises_the_frame_path(settings);
    a_refused_scope_does_not_stop_the_rest(settings);
    limits_are_respected_and_reported(settings);
    a_running_guest_is_taken_over(settings);
    a_restarted_guest_gets_the_level_again(settings);
    mirrors_follow_the_level(settings);
    a_refused_mirror_scope_is_reported(settings);
    answers_may_come_late(settings);
    the_caption_shows_only_what_holds();
    the_system_side_reads_this_process();

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
