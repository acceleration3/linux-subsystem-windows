#pragma once

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/link/agent_guest_stats_probe.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"
#include "lsw/host/services/status_hub.hpp"

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
//   2. ``libvirt_``   — the connection ``lifecycle_``/``status_`` wrap.
//   3. ``rpc_``       — attached to by ``log_store_`` and ``status_``.
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

    // Bind to the shared region of any VM that is already running —
    // one this process did not start. Call once the VM list is known.
    void attach_running_vms();

    [[nodiscard]] config::Settings& settings() noexcept { return settings_; }

    [[nodiscard]] config::VmState& vms() noexcept { return vms_; }

    [[nodiscard]] vm::LibvirtConnection& libvirt() noexcept { return libvirt_; }

    [[nodiscard]] vm::Lifecycle& lifecycle() noexcept { return lifecycle_; }

    // Every status the UI shows, polled in one place: read it, listen
    // to it, and poll nothing yourself.
    [[nodiscard]] StatusHub& status() noexcept { return status_; }

    [[nodiscard]] link::AgentRpcClient& rpc() noexcept { return rpc_; }

    // The one guest-window enumeration every consumer shares.
    [[nodiscard]] apps::AppConfigStore& app_configs() noexcept { return app_configs_; }

    [[nodiscard]] apps::LogStore& log_store() noexcept { return log_store_; }

    // Everything the settings page reads and writes, plus what has been
    // edited and not yet applied. Shared, because more than one page
    // stages restart keys and one apply bar commits them all.
    [[nodiscard]] config::ManagerSettings& manager_settings() noexcept { return manager_settings_; }

    [[nodiscard]] config::PendingChanges& pending_changes() noexcept { return pending_changes_; }

    // Point the agent channel at ``uuid``'s guest: its shared region
    // first, so the first frame after connecting already routes through
    // shared memory rather than the TCP envelope path, then the agent's
    // port. False if there is no such VM.
    bool bind_agent(const QUuid& uuid);

    // Start ``uuid``'s domain. Whether the guest has booted before
    // decides whether it should still be carrying the install media.
    // Returns Lifecycle::start's code.
    [[nodiscard]] int start_vm(const QUuid& uuid);

    // Push the desktop-entry and file-manager settings out to the
    // places they actually live: the XDG applications directory, the
    // menu fragment, and the two bookmark files. Called at startup and
    // whenever one of those settings changes, so a toggle takes effect
    // where the toggle says it does.
    void apply_integrations();

private:
    config::Settings settings_;
    config::ManagerSettings manager_settings_;
    // Declared after manager_settings_: it holds a reference to it.
    config::PendingChanges pending_changes_;
    config::VmState vms_;
    vm::LibvirtConnection libvirt_;
    vm::Lifecycle lifecycle_;
    link::AgentRpcClient rpc_;
    // Declared after rpc_: it registers UIA event handlers on it and
    // detaches them in its destructor.
    // Adapter handed to status_ so the VM layer can read in-guest
    // numbers without depending on the agent channel. Declared after
    // rpc_ (it holds a reference to it).
    link::AgentGuestStatsProbe guest_probe_;
    // Declared after everything it reads.
    StatusHub status_;
    apps::AppConfigStore app_configs_;
    apps::LogStore log_store_;  // declared after rpc_: detaches from it in ~LogStore
};

}  // namespace lsw::host::services
