#pragma once

#include "lsw/manager/apps/app_config_store.hpp"
#include "lsw/manager/apps/log_store.hpp"
#include "lsw/manager/config/vm_state.hpp"
#include "lsw/manager/ui/manager/app_list.hpp"
#include "lsw/manager/ui/manager/stats_widget.hpp"
#include "lsw/manager/ui/vnc_view.hpp"
#include "lsw/manager/vm/agent_rpc_client.hpp"
#include "lsw/manager/vm/lifecycle.hpp"
#include "lsw/manager/vm/monitor.hpp"

#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QWidget>

class QLabel;

namespace lsw::manager::ui::manager {

// Right-side VM detail: header (name + Start/Stop), thumbnail, stats,
// app grid.
class VmDetailPanel : public QWidget {
    Q_OBJECT
public:
    VmDetailPanel(config::VmState& vms, vm::Lifecycle& lifecycle, vm::Monitor& monitor,
                  apps::AppConfigStore& app_configs, vm::AgentRpcClient& rpc,
                  apps::LogStore& log_store, QWidget* parent = nullptr);

    void set_target(const QUuid& vm_uuid);
    void clear_target();

private Q_SLOTS:
    void on_start_clicked();
    void on_stop_clicked();
    void on_open_log_clicked();
    void on_open_console_clicked();
    void on_stats_updated(const QUuid& uuid, const vm::VmStats& stats);
    // Polls libvirt every ~2s and refreshes title text + button
    // enabled-state to match the live domain state.
    void on_state_tick();

private:
    void refresh_header();

    config::VmState& vms_;
    vm::Lifecycle& lifecycle_;
    vm::Monitor& monitor_;

    QUuid uuid_;
    bool last_running_ = false;
    bool last_agent_reachable_ = false;
    QTimer state_timer_;

    QLabel* title_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* open_log_button_ = nullptr;
    QPushButton* open_console_button_ = nullptr;
    lsw::manager::ui::VncView* preview_ = nullptr;
    StatsWidget* stats_ = nullptr;
    AppListWidget* app_list_ = nullptr;

    // Hoisted up to App so the wizard and MCP server share the same
    // channel + log buffer with the manager UI.
    vm::AgentRpcClient& rpc_;
    apps::LogStore& log_store_;
};

}  // namespace lsw::manager::ui::manager
