#pragma once

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/guest_app_launcher.hpp"
#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/link/agent_guest_stats_probe.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/agent_log_levels.hpp"
#include "lsw/host/services/guest_shells.hpp"
#include "lsw/host/services/log_collector.hpp"
#include "lsw/host/services/host_logging.hpp"
#include "lsw/host/services/guest_update.hpp"
#include "lsw/host/services/guest_upkeep.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/services/host_shutdown_watch.hpp"
#include "lsw/host/services/usb_passthrough.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"
#include "lsw/host/vm/vm_log_files.hpp"

#include <QObject>

namespace lsw::host::services {
class HostServices : public QObject {
    Q_OBJECT
public:
    explicit HostServices(QObject* parent = nullptr);
    ~HostServices() override;

    [[nodiscard]] bool open();

    [[nodiscard]] config::Settings& settings() noexcept { return settings_; }

    [[nodiscard]] config::VmState& vms() noexcept { return vms_; }

    [[nodiscard]] vm::LibvirtConnection& libvirt() noexcept { return libvirt_; }

    [[nodiscard]] vm::Lifecycle& lifecycle() noexcept { return lifecycle_; }

    [[nodiscard]] StatusHub& status() noexcept { return status_; }

    [[nodiscard]] link::AgentRpcClient& rpc() noexcept { return rpc_; }

    [[nodiscard]] GuestShells& shells() noexcept { return shells_; }

    [[nodiscard]] GuestUpdate& guest_update() noexcept { return guest_update_; }
    [[nodiscard]] GuestUpkeep& upkeep() noexcept { return upkeep_; }

    [[nodiscard]] UsbPassthrough& usb() noexcept { return usb_; }
    [[nodiscard]] HostShutdownWatch& host_shutdown() noexcept { return host_shutdown_; }

    [[nodiscard]] apps::AppConfigStore& app_configs() noexcept { return app_configs_; }

    [[nodiscard]] apps::LogStore& log_store() noexcept { return log_store_; }

    [[nodiscard]] config::LogVerbosity& log_verbosity() noexcept { return logging_.verbosity(); }
    [[nodiscard]] vm::VmLogFiles& vm_logs() noexcept { return vm_logs_; }
    [[nodiscard]] LogCollector& log_collector() noexcept { return log_collector_; }

    [[nodiscard]] apps::GuestAppLauncher& app_launcher() noexcept { return app_launcher_; }

    [[nodiscard]] config::ManagerSettings& manager_settings() noexcept { return manager_settings_; }

    [[nodiscard]] config::PendingChanges& pending_changes() noexcept { return pending_changes_; }

    bool bind_agent(const QUuid& uuid);

    [[nodiscard]] int start_vm(const QUuid& uuid);

    void apply_integrations();

private:
    config::Settings settings_;
    config::ManagerSettings manager_settings_;
    config::PendingChanges pending_changes_;
    config::VmState vms_;
    vm::LibvirtConnection libvirt_;
    vm::Lifecycle lifecycle_;
    link::AgentRpcClient rpc_;
    link::AgentGuestStatsProbe guest_probe_;
    StatusHub status_;
    GuestShells shells_;
    GuestUpdate guest_update_;
    GuestUpkeep upkeep_;
    UsbPassthrough usb_;
    HostShutdownWatch host_shutdown_;
    apps::AppConfigStore app_configs_;
    apps::LogStore log_store_;
    vm::VmLogFiles vm_logs_;
    HostLogging logging_;
    AgentLogLevels agent_log_levels_;
    LogCollector log_collector_;
    apps::GuestAppLauncher app_launcher_;
};

}
