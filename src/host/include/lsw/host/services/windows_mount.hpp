#pragma once

#include "lsw/host/platform/mount_table.hpp"
#include "lsw/host/platform/windows_fs_launcher.hpp"
#include "lsw/host/services/mount_status.hpp"

#include <QObject>
#include <QString>
#include <functional>

namespace lsw::host::config {
class ManagerSettings;
}

namespace lsw::host::services {

class StatusHub;

class WindowsMount : public QObject {
    Q_OBJECT
public:
    using TableReader = std::function<platform::MountTable()>;
    using MountedHook = std::function<void(const QString& mount_point)>;

    WindowsMount(StatusHub& status, config::ManagerSettings& settings,
                 platform::WindowsFsLauncher& helper, QString mount_point, TableReader table = {},
                 MountedHook mounted = {}, QObject* parent = nullptr);

    void start(bool guest_running);

    void unmount_for_shutdown();

private:
    void reconcile();
    void unblock();
    void unmount();
    void mount(const QString& share);
    void settle(const QString& share);
    void fail(const QString& detail);
    void publish(MountStatus::State state, const QString& path = {}, const QString& detail = {});

    StatusHub& status_;
    config::ManagerSettings& settings_;
    platform::WindowsFsLauncher& helper_;
    QString mount_point_;
    TableReader table_;
    MountedHook mounted_;
    bool started_ = false;
    bool closing_ = false;
    bool busy_ = false;
    bool blocked_ = false;
    bool start_running_ = false;
    bool was_running_ = false;
    bool was_online_ = false;
};

}  // namespace lsw::host::services
