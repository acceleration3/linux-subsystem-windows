#pragma once

#include "lsw/host/app/cli_options.hpp"
#include "lsw/host/platform/single_instance.hpp"
#include "lsw/host/services/host_services.hpp"

#include <QApplication>

#include <memory>

namespace lsw::host::mcp { class McpServer; }
namespace lsw::host::manager { class MainWindow; class GuestAppLaunchController; }
namespace lsw::host::wizard { class Wizard; }

namespace lsw::host::app {

class UnattendedRunner;

// Composition root. One concern: deciding which of the three run modes
// this process is in — first-run wizard, everyday manager, or headless
// unattended install — and standing up exactly what that mode needs.
//
// It deliberately does NOT expose its services to the objects it
// creates: they take a ``services::HostServices&`` instead, so nothing
// below the composition root depends on the composition root.
class App : public QApplication {
    Q_OBJECT
public:
    App(int& argc, char** argv);
    ~App() override;

    // Hand the CLI options to the App. Called from ``main`` after
    // parsing argv, which needs a live QApplication.
    void set_cli_options(CliOptions opts) { cli_ = std::move(opts); }

    // Show the wizard (first run) or main window (subsequent runs)
    // based on ``Settings::is_first_run()``. If CLI options request
    // unattended mode, drive the pipeline headlessly instead.
    // Returns the QApp exit code.
    [[nodiscard]] int run();

private Q_SLOTS:
    // Wizard "Done" — persist and hand off to a fresh manager process.
    void on_wizard_complete();

    // SingleInstance "raise" delivered via DBus.
    void on_raise_request();

    // SingleInstance "launch_app <id>" delivered via DBus from an XDG
    // launcher click. Delegated to the launch controller, which owns
    // the (VM may be cold) start / wait / launch / mirror sequence.
    void on_launch_app_request(const QString& app_id);

private:
    // True when this process is a secondary invocation: the primary owns
    // the DBus name, so we forward whatever we were asked to do and quit.
    [[nodiscard]] bool forward_to_primary();

    void show_main_window();
    void show_wizard();
    // Render every page of both windows into ``dir`` and exit. See
    // CliOptions::screenshot_dir for why this exists.
    [[nodiscard]] int capture_screens(const QString& dir);

    // Stand up the MCP server (manager mode only). Localhost-only, no
    // auth. Bind failure is non-fatal — logged and skipped. Not started
    // in wizard / unattended mode: there's no app to drive yet, and it
    // would hold the port the spawned manager wants.
    void start_mcp_server();

    // First-run handoff: the wizard has finished provisioning. Release
    // the single-instance lock, spawn a fresh copy of this binary (which
    // — first-run now complete — boots straight into the manager) and
    // quit this process. Falls back to an in-process manager window if
    // the spawn fails.
    void spawn_manager_and_quit();

    [[nodiscard]] manager::GuestAppLaunchController& launch_controller();

    CliOptions cli_;
    platform::SingleInstance single_instance_;
    services::HostServices services_;

    // Created lazily, per run mode. Widgets are owned by Qt's parent
    // tree once shown; the rest by these pointers.
    std::unique_ptr<mcp::McpServer> mcp_server_;
    std::unique_ptr<UnattendedRunner> unattended_;
    std::unique_ptr<manager::GuestAppLaunchController> launch_controller_;
    manager::MainWindow* main_window_ = nullptr;
    wizard::Wizard* wizard_ = nullptr;
};

}  // namespace lsw::host::app
