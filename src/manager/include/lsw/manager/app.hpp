#pragma once

#include "lsw/manager/apps/app_config_store.hpp"
#include "lsw/manager/apps/log_store.hpp"
#include "lsw/manager/cli.hpp"
#include "lsw/manager/config/settings.hpp"
#include "lsw/manager/config/vm_state.hpp"
#include "lsw/manager/single_instance.hpp"
#include "lsw/manager/vm/agent_rpc_client.hpp"
#include "lsw/manager/vm/libvirt_conn.hpp"
#include "lsw/manager/vm/lifecycle.hpp"
#include "lsw/manager/vm/monitor.hpp"

#include <QApplication>

#include <memory>

namespace lsw::manager::mcp { class McpServer; }

namespace lsw::manager::ui::manager {
class MainWindow;
}

namespace lsw::manager::ui::wizard {
class Wizard;
}

namespace lsw::manager {

// Composition root. Owns one of every long-lived service object and
// binds them together with signal/slot connections. Both ``main`` and
// future tests poke at ``App`` rather than the QApplication directly.
class App : public QApplication {
    Q_OBJECT
public:
    App(int& argc, char** argv);
    ~App() override;

    // Hand the CLI options to the App. Called from ``main`` after
    // parsing argv. If ``opts.unattended()`` is true, ``run()`` skips
    // the GUI wizard and drives the install pipeline directly.
    void set_cli_options(CliOptions opts) { cli_ = std::move(opts); }

    // Show the wizard (first run) or main window (subsequent runs)
    // based on ``Settings::is_first_run()``. If CLI options request
    // unattended mode, drive the pipeline headlessly instead.
    // Returns the QApp exit code.
    [[nodiscard]] int run();

    [[nodiscard]] config::Settings& settings() noexcept { return settings_; }

    [[nodiscard]] config::VmState& vms() noexcept { return vms_; }

    [[nodiscard]] vm::LibvirtConnection& libvirt() noexcept { return libvirt_; }

    [[nodiscard]] vm::Lifecycle& lifecycle() noexcept { return lifecycle_; }

    [[nodiscard]] vm::Monitor& monitor() noexcept { return monitor_; }

    [[nodiscard]] apps::AppConfigStore& app_configs() noexcept { return app_configs_; }

    // Shared per-VM RPC channel and log buffer. Hoisted up from
    // VmDetailPanel so they outlive the panel (the wizard touches
    // rpc_ for download/setup steps, and the MCP server holds a
    // reference for as long as the manager process runs).
    [[nodiscard]] vm::AgentRpcClient& rpc() noexcept { return rpc_; }
    [[nodiscard]] apps::LogStore& log_store() noexcept { return log_store_; }

private Q_SLOTS:
    // Wizard "Done" — persist and switch to the main window.
    void on_wizard_complete();

    // SingleInstance "raise" delivered via DBus.
    void on_raise_request();

private:
    void show_main_window();
    void show_wizard();
    // Drive the install pipeline directly from CliOptions. Prints
    // step transitions / progress to stdout, returns when the
    // pipeline emits ``completed`` or ``failed``. Returns 0/!=0 via
    // the QApplication exit code.
    void run_unattended();

    // Look up the libvirt domain ``kVmName`` and, if it exists, build
    // a VmRecord for it (UUID from libvirt, all paths derived from
    // ``kVmName``) and add it to ``vms_``. libvirt is the source of
    // truth — there's no JSON manifest.
    void populate_existing_vm();

    CliOptions cli_;
    SingleInstance single_instance_;
    config::Settings settings_;
    config::VmState vms_;
    vm::LibvirtConnection libvirt_;
    vm::Lifecycle lifecycle_;
    vm::Monitor monitor_;
    apps::AppConfigStore app_configs_;

    // Persistent agent channel + log ring shared by every consumer
    // (wizard, manager UI, MCP server). The VM's (cid, port) is set
    // on rpc_ once we have a target via ``set_target``; the channel's
    // reader thread reconnects automatically across VM restarts.
    vm::AgentRpcClient rpc_;
    apps::LogStore log_store_;

    // MCP server — owns its own QTcpServer. Constructed lazily in
    // ``run()`` so we don't bind a port if the manager was launched
    // for a one-off operation.
    std::unique_ptr<mcp::McpServer> mcp_server_;

    ui::manager::MainWindow* main_window_ = nullptr;
    ui::wizard::Wizard* wizard_ = nullptr;
};

}  // namespace lsw::manager
