#include "lsw/host/services/vm_priority.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/platform/cpu_scope.hpp"
#include "lsw/host/platform/process_threads.hpp"

#include <QRegularExpression>
#include <QVector>
#include <algorithm>
#include <utility>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

const QSet<QString>& frame_threads() {
    static const QSet<QString> names = {
        QStringLiteral("av-context"),      QStringLiteral("dxvk-cs"),
        QStringLiteral("dxvk-submit"),     QStringLiteral("dxvk-queue"),
        QStringLiteral("dxvk-frame"),      QStringLiteral("dxvk-descriptor"),
        QStringLiteral("vkd3d_queue"),     QStringLiteral("vkd3d_fence"),
        QStringLiteral("vkd3d-swapchain"),
    };
    return names;
}

}  // namespace

VmPriority::VmPriority(config::ManagerSettings& settings, platform::ICpuScope& vm_scope,
                       platform::ICpuScope& mirror_scope, platform::IProcessThreads& threads,
                       std::function<qint64()> qemu_pid, QObject* parent)
    : QObject(parent), settings_(settings), vm_scope_(vm_scope), mirror_scope_(mirror_scope),
      threads_(threads), qemu_pid_(std::move(qemu_pid)) {
    status_.wanted = wanted();
    status_.wanted_nice = kRaisedNice;
    connect(&settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kPriority) {
            setting_changed();
        }
    });
}

VmPriority::Level VmPriority::level_of(const QString& value) {
    if (value == QLatin1String("highest")) {
        return Level::Highest;
    }
    return value == QLatin1String("high") ? Level::High : Level::Normal;
}

bool VmPriority::runs_guest_code(const QString& thread) {
    return thread.startsWith(QLatin1String("CPU "));
}

bool VmPriority::on_frame_path(const QString& thread, const QString& main_thread) {
    static const QRegularExpression driver_queue{QStringLiteral(":(gdrv|cs)[0-9]+$")};
    if (runs_guest_code(thread)) {
        return false;
    }
    return thread == main_thread || thread.startsWith(QLatin1String("IO ")) ||
           frame_threads().contains(thread) || driver_queue.match(thread).hasMatch();
}

VmPriority::Level VmPriority::wanted() const {
    return level_of(settings_.string_value(K::kPriority));
}

VmPriority::Weight* VmPriority::weight_of(qint64 pid, bool mirror) {
    if (!mirror) {
        return pid == vm_pid_ ? &vm_weight_ : nullptr;
    }
    const auto found = mirrors_.find(pid);
    return found != mirrors_.end() ? &*found : nullptr;
}

void VmPriority::setting_changed() {
    const auto again = [](Weight& weight) {
        weight.refused = false;
        weight.kept = false;
    };
    again(vm_weight_);
    for (Weight& mirror : mirrors_) {
        again(mirror);
    }
    told_mirror_refusal_ = false;
    vm_threads_.seen = vm_threads_.raised;
    vm_threads_.refused = false;
    vm_threads_.told_limit = false;
    apply(false);
}

void VmPriority::forget_vm() {
    vm_pid_ = -1;
    vm_weight_ = Weight{};
    vm_threads_ = Threads{};
}

void VmPriority::vm_seen(int boot) {
    if (boot != boot_) {
        boot_ = boot;
        forget_vm();
    }
    apply(true);
}

void VmPriority::mirror_started(qint64 pid) {
    if (pid <= 0) {
        return;
    }
    mirrors_.insert(pid, Weight{});
    apply(false);
}

void VmPriority::mirror_ended(qint64 pid) {
    if (mirrors_.remove(pid)) {
        publish(wanted());
    }
}

void VmPriority::apply(bool confirm) {
    const Level level = wanted();
    const bool touching =
        level != Level::Normal || vm_weight_.touched() || !vm_threads_.raised.isEmpty();
    if (boot_ >= 0 && vm_pid_ <= 0 && touching) {
        vm_pid_ = qemu_pid_();
    }
    if (vm_pid_ > 0) {
        weigh(vm_pid_, false, level, confirm);
    }
    const QList<qint64> mirrors = mirrors_.keys();
    for (const qint64 pid : mirrors) {
        weigh(pid, true, level, confirm);
    }
    if (vm_pid_ > 0) {
        if (level != Level::Highest) {
            settle();
        } else if (!raise()) {
            forget_vm();
        }
    }
    publish(level);
}

void VmPriority::refuse(qint64 pid, bool mirror, Weight& weight, const QString& error) {
    weight.refused = true;
    if (mirror && std::exchange(told_mirror_refusal_, true)) {
        return;
    }
    LSW_WARN("priority: {} (pid {}) could not be given processor weight {}: {}. The rest of the "
             "setting still applies",
             mirror ? "a window mirror" : "the guest", pid, kRaisedWeight, error.toStdString());
}

void VmPriority::weigh(qint64 pid, bool mirror, Level level, bool confirm) {
    Weight* weight = weight_of(pid, mirror);
    if (weight == nullptr || weight->calling) {
        return;
    }
    platform::ICpuScope& scope = mirror ? mirror_scope_ : vm_scope_;
    if (level == Level::Normal) {
        if ((!weight->raised && !weight->asked) || weight->kept) {
            return;
        }
        weight->calling = true;
        scope.set_weight(pid, kDefaultWeight, [this, pid, mirror](bool done, const QString& error) {
            Weight* called = weight_of(pid, mirror);
            if (called == nullptr) {
                return;
            }
            called->calling = false;
            if (done) {
                called->asked = false;
                called->raised = false;
                LSW_INFO("priority: the processor weight of pid {} is back at {}", pid,
                         kDefaultWeight);
            } else {
                called->kept = true;
                LSW_WARN("priority: the processor weight of pid {} could not be put back: {}", pid,
                         error.toStdString());
            }
            publish(wanted());
        });
        return;
    }
    if (weight->raised || weight->refused) {
        return;
    }
    if (weight->asked) {
        if (!confirm) {
            return;
        }
        weight->asked = false;
        if (scope.holds(pid) && scope.weight(pid) == kRaisedWeight) {
            weight->raised = true;
            LSW_INFO("priority: {} (pid {}) runs in its own scope with processor weight {}",
                     mirror ? "a window mirror" : "the guest", pid, kRaisedWeight);
        } else {
            refuse(pid, mirror, *weight,
                   QStringLiteral("the scope was asked for but the weight is not in force; the cpu "
                                  "controller may not be delegated to this session"));
        }
        return;
    }
    weight->calling = true;
    const auto asked = [this, pid, mirror](bool done, const QString& error) {
        Weight* called = weight_of(pid, mirror);
        if (called == nullptr) {
            return;
        }
        called->calling = false;
        if (done) {
            called->asked = true;
        } else {
            refuse(pid, mirror, *called, error);
        }
        publish(wanted());
    };
    if (scope.holds(pid)) {
        scope.set_weight(pid, kRaisedWeight, asked);
    } else {
        scope.enter(pid, kRaisedWeight, asked);
    }
}

bool VmPriority::raise() {
    Threads& process = vm_threads_;
    const QVector<qint64> alive = threads_.list(vm_pid_);
    if (alive.isEmpty()) {
        return false;
    }
    if (process.nice == 0 && !process.refused) {
        const int lowest = threads_.lowest_nice(vm_pid_);
        process.nice = std::max(kRaisedNice, lowest);
        if (process.nice != kRaisedNice && !process.told_limit) {
            process.told_limit = true;
            if (process.nice < 0) {
                LSW_WARN("priority: the limits of pid {} allow nice {}, not {}; using {}", vm_pid_,
                         lowest, kRaisedNice, process.nice);
            } else {
                LSW_WARN("priority: the limits of pid {} allow no nice below 0; its threads stay "
                         "at normal priority",
                         vm_pid_);
            }
        }
    }
    if (process.nice >= 0) {
        process.nice = 0;
        process.refused = true;
        return true;
    }
    raise_threads(alive);
    return true;
}

void VmPriority::raise_threads(const QVector<qint64>& alive) {
    using Thread = platform::IProcessThreads::Thread;
    Threads& process = vm_threads_;
    const QSet<qint64> now(alive.cbegin(), alive.cend());
    process.seen.intersect(now);
    process.raised.intersect(now);

    QVector<std::pair<qint64, Thread>> fresh;
    bool guest_code_named = !process.main_thread.isEmpty();
    for (const qint64 tid : alive) {
        if (process.seen.contains(tid)) {
            continue;
        }
        const auto thread = threads_.read(vm_pid_, tid);
        if (!thread) {
            continue;
        }
        guest_code_named = guest_code_named || runs_guest_code(thread->name);
        if (thread->age_ms >= kNamedAfterMs) {
            fresh.push_back({tid, *thread});
        }
    }
    if (process.main_thread.isEmpty()) {
        const auto main = std::find_if(fresh.cbegin(), fresh.cend(), [this](const auto& entry) {
            return entry.first == vm_pid_;
        });
        process.unnamed = main != fresh.cend() && !guest_code_named;
        if (main == fresh.cend() || process.unnamed) {
            return;
        }
        process.main_thread = main->second.name;
    }

    bool refused_now = false;
    for (const auto& [tid, thread] : std::as_const(fresh)) {
        process.seen.insert(tid);
        if (!on_frame_path(thread.name, process.main_thread)) {
            if (thread.nice == process.nice) {
                (void)threads_.set_nice(tid, 0);
            }
            continue;
        }
        if (thread.nice == process.nice || threads_.set_nice(tid, process.nice)) {
            process.raised.insert(tid);
        } else {
            refused_now = true;
        }
    }
    if (refused_now && !process.refused) {
        LSW_WARN("priority: the system refused nice {} for threads of pid {}", process.nice,
                 vm_pid_);
    }
    process.refused = process.refused || refused_now;
}

void VmPriority::settle() {
    Threads& process = vm_threads_;
    if (!process.raised.isEmpty()) {
        const QVector<qint64> alive = threads_.list(vm_pid_);
        process.raised.intersect(QSet<qint64>(alive.cbegin(), alive.cend()));
    }
    for (const qint64 tid : std::as_const(process.raised)) {
        (void)threads_.set_nice(tid, 0);
    }
    if (!process.raised.isEmpty()) {
        LSW_INFO("priority: {} threads of pid {} are back at nice 0", process.raised.size(),
                 vm_pid_);
    }
    const QString main_thread = process.main_thread;
    process = Threads{};
    process.main_thread = main_thread;
}

void VmPriority::publish(Level level) {
    PriorityStatus next;
    next.wanted = level;
    next.wanted_nice = kRaisedNice;
    next.vm_running = boot_ >= 0;
    next.weight_raised = vm_weight_.raised;
    next.weight_refused = vm_weight_.refused;
    next.threads_refused = vm_threads_.refused;
    next.threads_unnamed = vm_threads_.unnamed;
    next.threads_raised = !vm_threads_.raised.isEmpty();
    next.nice = next.threads_raised ? vm_threads_.nice : 0;
    for (const Weight& mirror : std::as_const(mirrors_)) {
        next.mirrors_refused = next.mirrors_refused || mirror.refused;
    }
    if (next == status_) {
        return;
    }
    status_ = next;
    Q_EMIT changed();
}

}  // namespace lsw::host::services
