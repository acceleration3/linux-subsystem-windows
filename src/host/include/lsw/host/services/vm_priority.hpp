#pragma once

#include "lsw/host/services/priority_status.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>

namespace lsw::host::config {
class ManagerSettings;
}

namespace lsw::host::platform {
class ICpuScope;
class IProcessThreads;
}  // namespace lsw::host::platform

namespace lsw::host::services {
class VmPriority : public QObject {
    Q_OBJECT
public:
    using Level = PriorityStatus::Level;

    static constexpr int kRaisedWeight = 1000;
    static constexpr int kDefaultWeight = 100;
    static constexpr int kRaisedNice = -10;
    static constexpr qint64 kNamedAfterMs = 1000;

    VmPriority(config::ManagerSettings& settings, platform::ICpuScope& vm_scope,
               platform::ICpuScope& mirror_scope, platform::IProcessThreads& threads,
               std::function<qint64()> qemu_pid, QObject* parent = nullptr);

    [[nodiscard]] static Level level_of(const QString& value);
    [[nodiscard]] static bool on_frame_path(const QString& thread, const QString& main_thread);
    [[nodiscard]] static bool runs_guest_code(const QString& thread);

    void vm_seen(int boot);
    void mirror_started(qint64 pid);
    void mirror_ended(qint64 pid);

    [[nodiscard]] const PriorityStatus& status() const noexcept { return status_; }

Q_SIGNALS:
    void changed();

private:
    struct Weight {
        bool calling = false;
        bool asked = false;
        bool raised = false;
        bool refused = false;
        bool kept = false;

        [[nodiscard]] bool touched() const noexcept { return calling || asked || raised; }
    };

    struct Threads {
        QString main_thread;
        QSet<qint64> seen;
        QSet<qint64> raised;
        int nice = 0;
        bool refused = false;
        bool unnamed = false;
        bool told_limit = false;
    };

    [[nodiscard]] Level wanted() const;
    [[nodiscard]] Weight* weight_of(qint64 pid, bool mirror);
    void setting_changed();
    void apply(bool confirm);
    void forget_vm();
    void weigh(qint64 pid, bool mirror, Level level, bool confirm);
    void refuse(qint64 pid, bool mirror, Weight& weight, const QString& error);
    [[nodiscard]] bool raise();
    void raise_threads(const QVector<qint64>& alive);
    void settle();
    void publish(Level level);

    config::ManagerSettings& settings_;
    platform::ICpuScope& vm_scope_;
    platform::ICpuScope& mirror_scope_;
    platform::IProcessThreads& threads_;
    std::function<qint64()> qemu_pid_;
    PriorityStatus status_;
    qint64 vm_pid_ = -1;
    Weight vm_weight_;
    Threads vm_threads_;
    QHash<qint64, Weight> mirrors_;
    int boot_ = -1;
    bool told_mirror_refusal_ = false;
};

}  // namespace lsw::host::services
