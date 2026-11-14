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
}

namespace lsw::host::services {
class VmPriority : public QObject {
    Q_OBJECT
public:
    using Level = PriorityStatus::Level;

    static constexpr int kRaisedWeight = 1000;
    static constexpr int kDefaultWeight = 100;
    static constexpr int kRaisedNice = -10;
    static constexpr qint64 kNamedAfterMs = 1000;

    VmPriority(config::ManagerSettings& settings, platform::ICpuScope& scope,
               platform::IProcessThreads& threads, std::function<qint64()> qemu_pid,
               QObject* parent = nullptr);

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
    struct Process {
        qint64 pid = -1;
        bool every_thread = false;
        QString main_thread;
        QSet<qint64> seen;
        QSet<qint64> raised;
        int nice = 0;
        bool refused = false;
        bool unnamed = false;
        bool told_limit = false;
    };

    [[nodiscard]] Level wanted() const;
    void setting_changed();
    void apply();
    void weigh(Level level);
    [[nodiscard]] bool raise(Process& process);
    void raise_threads(Process& process, const QVector<qint64>& alive);
    void settle(Process& process);
    void publish(Level level);

    config::ManagerSettings& settings_;
    platform::ICpuScope& scope_;
    platform::IProcessThreads& threads_;
    std::function<qint64()> qemu_pid_;
    PriorityStatus status_;
    Process vm_;
    QHash<qint64, Process> mirrors_;
    int boot_ = -1;
    bool weight_asked_ = false;
    bool weight_raised_ = false;
    bool weight_refused_ = false;
};

}
