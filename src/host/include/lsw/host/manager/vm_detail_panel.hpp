#pragma once

#include "lsw/host/manager/app_list.hpp"
#include "lsw/host/manager/stats_widget.hpp"
#include <QUuid>
#include <QWidget>


namespace lsw::host::services { class HostServices; }

namespace lsw::host::manager {

class DesktopPreview;

class VmDetailPanel : public QWidget {
    Q_OBJECT
public:
    explicit VmDetailPanel(services::HostServices& services, QWidget* parent = nullptr);

    void set_target(const QUuid& vm_uuid);

    [[nodiscard]] bool is_running() const noexcept { return last_running_; }
    [[nodiscard]] bool is_agent_online() const noexcept { return last_agent_reachable_; }


public Q_SLOTS:
    bool start_guest();
    void stop_guest();

Q_SIGNALS:
    void running_changed(bool running);
    void agent_online_changed(bool online);
    void launch_requested(const QString& app_id);

private:
    void on_status();
    void follow_preview(bool running);

private:
    services::HostServices& services_;

    QUuid uuid_;
    bool last_running_ = false;
    bool last_agent_reachable_ = false;

    DesktopPreview* preview_ = nullptr;
    StatsWidget* stats_ = nullptr;
    AppListWidget* app_list_ = nullptr;
};

}
