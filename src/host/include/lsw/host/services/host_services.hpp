#pragma once

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/link/agent_guest_stats_probe.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/link/guest_window_model.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"
#include "lsw/host/vm/monitor.hpp"

#include <QObject>

namespace lsw::host::services {

// The set of process-lifetime services every host-side feature module
// needs, and nothing else. One concern: owning those objects in a
// construction order that satisfies their dependencies, and handing
// them out by reference.
//
// This exists so feature modules (the manager UI, the wizard UI, the
// provisioning pipeline) depend on *services*, not on the application
// object. Before, each of them took an ``App&`` and reached through it
// — which made the composition root a dependency of everything it
// composed. ``HostServices`` is that dependency edge, inverted.
//
// Construction order is load-bearing:
//   1. ``settings_``  — every path below is derived from it.
//   2. ``libvirt_``   — the connection ``lifecycle_``/``monitor_`` wrap.
//   3. ``rpc_``       — attached to by ``log_store_`` and ``monitor_``.
// Member order encodes it; do not reorder.
//
// Teardown order is the reverse, which matters for ``log_store_``: its
// destructor detaches the event handler it registered on ``rpc_``, so
// it must be declared after ``rpc_``.
class HostServices : public QObject {
    Q_OBJECT
public:
    explicit HostServices(QObject* parent = nullptr);
    ~HostServices() override;

    // Create the LSW data directories and open libvirt. Returns false
    // only if the data directories could not be created — a libvirt
    // that refuses to open is degraded, not fatal (VM controls are
    // disabled, the rest of the UI still works).
    [[nodiscard]] bool open();

    [[nodiscard]] config::Settings& settings() noexcept { return settings_; }
    [[nodiscard]] config::VmState& vms() noexcept { return vms_; }
    [[nodiscard]] vm::LibvirtConnection& libvirt() noexcept { return libvirt_; }
    [[nodiscard]] vm::Lifecycle& lifecycle() noexcept { return lifecycle_; }
    [[nodiscard]] vm::Monitor& monitor() noexcept { return monitor_; }
    [[nodiscard]] link::AgentRpcClient& rpc() noexcept { return rpc_; }
    // The one guest-window enumeration every consumer shares.
    [[nodiscard]] link::GuestWindowModel& windows() noexcept { return windows_; }
    [[nodiscard]] apps::AppConfigStore& app_configs() noexcept { return app_configs_; }
    [[nodiscard]] apps::LogStore& log_store() noexcept { return log_store_; }

private:
    config::Settings settings_;
    config::VmState vms_;
    vm::LibvirtConnection libvirt_;
    vm::Lifecycle lifecycle_;
    vm::Monitor monitor_;
    link::AgentRpcClient rpc_;
    // Declared after rpc_: it registers UIA event handlers on it and
    // detaches them in its destructor.
    link::GuestWindowModel windows_;
    // Adapter handed to monitor_ so the VM layer can read in-guest
    // numbers without depending on the agent channel. Declared after
    // rpc_ (it holds a reference to it).
    link::AgentGuestStatsProbe guest_probe_;
    apps::AppConfigStore app_configs_;
    apps::LogStore log_store_;  // declared after rpc_: detaches from it in ~LogStore
};

}  // namespace lsw::host::services
