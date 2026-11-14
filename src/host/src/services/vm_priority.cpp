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
        QStringLiteral("av-context"),  QStringLiteral("dxvk-cs"),     QStringLiteral("dxvk-submit"),
        QStringLiteral("dxvk-queue"),  QStringLiteral("dxvk-frame"),  QStringLiteral("dxvk-descriptor"),
        QStringLiteral("vkd3d_queue"), QStringLiteral("vkd3d_fence"), QStringLiteral("vkd3d-swapchain"),
    };
    return names;
}

}

VmPriority::VmPriority(config::ManagerSettings& settings, platform::ICpuScope& scope,
                       platform::IProcessThreads& threads, std::function<qint64()> qemu_pid,
                       QObject* parent)
    : QObject(parent), settings_(settings), scope_(scope), threads_(threads),
      qemu_pid_(std::move(qemu_pid)) {
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

void VmPriority::setting_changed() {
    weight_refused_ = false;
    const auto again = [](Process& process) {
        process.seen = process.raised;
        process.refused = false;
        process.told_limit = false;
    };
    again(vm_);
    for (Process& mirror : mirrors_) {
        again(mirror);
    }
    apply();
}

void VmPriority::vm_seen(int boot) {
    if (boot != boot_) {
        boot_ = boot;
        vm_ = Process{};
        weight_asked_ = false;
        weight_raised_ = false;
        weight_refused_ = false;
    }
    apply();
}

void VmPriority::mirror_started(qint64 pid) {
    if (pid <= 0) {
        return;
    }
    Process mirror;
    mirror.pid = pid;
    mirror.every_thread = true;
    mirrors_.insert(pid, mirror);
    apply();
}

void VmPriority::mirror_ended(qint64 pid) {
    if (mirrors_.remove(pid)) {
        apply();
    }
}

void VmPriority::apply() {
    const Level level = wanted();
    const bool touching =
        level != Level::Normal || weight_asked_ || weight_raised_ || !vm_.raised.isEmpty();
    if (boot_ >= 0 && vm_.pid <= 0 && touching) {
        vm_.pid = qemu_pid_();
    }
    if (vm_.pid > 0) {
        weigh(level);
    }
    for (auto mirror = mirrors_.begin(); mirror != mirrors_.end();) {
        if (level != Level::Highest) {
            settle(*mirror);
        } else if (!raise(*mirror)) {
            mirror = mirrors_.erase(mirror);
            continue;
        }
        ++mirror;
    }
    if (vm_.pid > 0) {
        if (level != Level::Highest) {
            settle(vm_);
        } else if (!raise(vm_)) {
            vm_ = Process{};
            weight_asked_ = false;
            weight_raised_ = false;
            weight_refused_ = false;
        }
    }
    publish(level);
}

void VmPriority::weigh(Level level) {
    QString error;
    if (level == Level::Normal) {
        if (!weight_raised_ && !weight_asked_) {
            return;
        }
        if (scope_.set_weight(vm_.pid, kDefaultWeight, &error)) {
            weight_asked_ = false;
            weight_raised_ = false;
            LSW_INFO("priority: the guest's processor weight is back at {}", kDefaultWeight);
        } else {
            LSW_DEBUG("priority: the guest's processor weight could not be put back: {}",
                      error.toStdString());
        }
        return;
    }
    if (weight_raised_ || weight_refused_) {
        return;
    }
    if (weight_asked_) {
        weight_asked_ = false;
        if (scope_.holds(vm_.pid) && scope_.weight(vm_.pid) == kRaisedWeight) {
            weight_raised_ = true;
            LSW_INFO("priority: the guest (pid {}) runs in its own scope with processor weight {}",
                     vm_.pid, kRaisedWeight);
            return;
        }
        error = QStringLiteral("the scope was asked for but the weight is not in force; the cpu "
                               "controller may not be delegated to this session");
    } else if (scope_.holds(vm_.pid) ? scope_.set_weight(vm_.pid, kRaisedWeight, &error)
                                     : scope_.enter(vm_.pid, kRaisedWeight, &error)) {
        weight_asked_ = true;
        return;
    }
    weight_refused_ = true;
    LSW_WARN("priority: the guest could not be given processor weight {}: {}. The rest of the "
             "setting still applies",
             kRaisedWeight, error.toStdString());
}

bool VmPriority::raise(Process& process) {
    const QVector<qint64> alive = threads_.list(process.pid);
    if (alive.isEmpty()) {
        return false;
    }
    if (process.nice == 0 && !process.refused) {
        const int lowest = threads_.lowest_nice(process.pid);
        process.nice = std::max(kRaisedNice, lowest);
        if (process.nice != kRaisedNice && !process.told_limit) {
            process.told_limit = true;
            if (process.nice < 0) {
                LSW_WARN("priority: the limits of pid {} allow nice {}, not {}; using {}", process.pid,
                         lowest, kRaisedNice, process.nice);
            } else {
                LSW_WARN("priority: the limits of pid {} allow no nice below 0; its threads stay "
                         "at normal priority",
                         process.pid);
            }
        }
    }
    if (process.nice >= 0) {
        process.nice = 0;
        process.refused = true;
        return true;
    }
    raise_threads(process, alive);
    return true;
}

void VmPriority::raise_threads(Process& process, const QVector<qint64>& alive) {
    using Thread = platform::IProcessThreads::Thread;
    const QSet<qint64> now(alive.cbegin(), alive.cend());
    process.seen.intersect(now);
    process.raised.intersect(now);

    QVector<std::pair<qint64, Thread>> fresh;
    bool guest_code_named = process.every_thread || !process.main_thread.isEmpty();
    for (const qint64 tid : alive) {
        if (process.seen.contains(tid)) {
            continue;
        }
        const auto thread = threads_.read(process.pid, tid);
        if (!thread) {
            continue;
        }
        guest_code_named = guest_code_named || runs_guest_code(thread->name);
        if (process.every_thread || thread->age_ms >= kNamedAfterMs) {
            fresh.push_back({tid, *thread});
        }
    }
    if (!process.every_thread && process.main_thread.isEmpty()) {
        const auto main = std::find_if(fresh.cbegin(), fresh.cend(),
                                       [&process](const auto& entry) { return entry.first == process.pid; });
        process.unnamed = main != fresh.cend() && !guest_code_named;
        if (main == fresh.cend() || process.unnamed) {
            return;
        }
        process.main_thread = main->second.name;
    }

    bool refused_now = false;
    for (const auto& [tid, thread] : std::as_const(fresh)) {
        process.seen.insert(tid);
        if (!process.every_thread && !on_frame_path(thread.name, process.main_thread)) {
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
                 process.pid);
    }
    process.refused = process.refused || refused_now;
}

void VmPriority::settle(Process& process) {
    if (!process.raised.isEmpty()) {
        const QVector<qint64> alive = threads_.list(process.pid);
        process.raised.intersect(QSet<qint64>(alive.cbegin(), alive.cend()));
    }
    for (const qint64 tid : std::as_const(process.raised)) {
        (void)threads_.set_nice(tid, 0);
    }
    if (!process.raised.isEmpty()) {
        LSW_INFO("priority: {} threads of pid {} are back at nice 0", process.raised.size(),
                 process.pid);
    }
    process.raised.clear();
    process.seen.clear();
    process.nice = 0;
    process.refused = false;
    process.unnamed = false;
    process.told_limit = false;
}

void VmPriority::publish(Level level) {
    PriorityStatus next;
    next.wanted = level;
    next.wanted_nice = kRaisedNice;
    next.vm_running = boot_ >= 0;
    next.weight_raised = weight_raised_ || (weight_asked_ && level == Level::Normal);
    next.weight_refused = weight_refused_;
    const auto count = [&next](const Process& process) {
        if (process.pid <= 0) {
            return;
        }
        next.threads_refused = next.threads_refused || process.refused;
        next.threads_unnamed = next.threads_unnamed || process.unnamed;
        if (!process.raised.isEmpty()) {
            next.threads_raised = true;
            next.nice = next.nice == 0 ? process.nice : std::max(next.nice, process.nice);
        }
    };
    count(vm_);
    for (const Process& mirror : std::as_const(mirrors_)) {
        count(mirror);
    }
    if (next == status_) {
        return;
    }
    status_ = next;
    Q_EMIT changed();
}

}
