#include "lsw/host/app/app.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/app/unattended_runner.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/manager/guest_app_launch_controller.hpp"
#include "lsw/host/manager/main_window.hpp"
#include "lsw/host/manager/taskbar_entry.hpp"
#include "lsw/host/mcp/mcp_server.hpp"
#include "lsw/host/vm/domain_importer.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QMetaObject>
#include <QProcess>

namespace lsw::host::app {

App::App(int& argc, char** argv)
    : QApplication(argc, argv), single_instance_(this), services_(this) {
    setApplicationName("lsw-manager");
    setOrganizationName("lsw");
    setOrganizationDomain("lsw");

    // Wipe any per-window XDG launcher entries left over from a previous
    // manager run that crashed or was SIGKILL'd before its WindowView
    // dtors could fire. Each entry is one Plasma Task Manager / GNOME
    // Shell sidebar slot, so leftovers surface as ghost icons.
    manager::TaskbarEntry::cleanup_stale_entries();

    // Sync the host's XDG menu with whatever app configs are already on
    // disk — covers opening a .desktop entry that has been there a while
    // without first opening the manager UI.
    auto& configs = services_.app_configs();
    apps::DesktopLauncher::sync(configs.configs());
    connect(&configs, &apps::AppConfigStore::changed, this,
            [this] { apps::DesktopLauncher::sync(services_.app_configs().configs()); });

    // Turned OFF only once a real, tray-backed main window is up (see
    // ``show_main_window``). During the wizard phase we keep it ON so
    // closing the wizard actually exits the process — otherwise the event
    // loop has no UI and no tray icon and only Ctrl-C ends it.
    setQuitOnLastWindowClosed(true);

    connect(&single_instance_, &platform::SingleInstance::raise_requested,
            this, &App::on_raise_request);
    connect(&single_instance_, &platform::SingleInstance::launch_app_requested,
            this, &App::on_launch_app_request);
}

App::~App() = default;

manager::GuestAppLaunchController& App::launch_controller() {
    if (launch_controller_ == nullptr) {
        launch_controller_ =
            std::make_unique<manager::GuestAppLaunchController>(services_, this);
    }
    return *launch_controller_;
}

bool App::forward_to_primary() {
    if (single_instance_.acquire()) {
        return false;
    }
    // Another manager owns the DBus name. If we were invoked with
    // ``--launch <id>`` (an XDG launcher click on a guest app entry),
    // forward the id to that instance and exit — the primary pops the
    // splash and drives the VM. Bare re-invocations raise its window.
    if (!cli_.launch_app_id.isEmpty()) {
        LSW_INFO("forwarding --launch to running manager: {}",
                 cli_.launch_app_id.toStdString());
        (void)single_instance_.launch_app_existing(cli_.launch_app_id);
    } else {
        LSW_INFO("another lsw-manager is already running; raising it");
        (void)single_instance_.raise_existing();
    }
    return true;
}

int App::run() {
    if (forward_to_primary()) {
        return 0;
    }
    if (!services_.open()) {
        return 1;
    }
    (void)vm::DomainImporter::import_existing(services_.libvirt(), services_.settings(),
                                              services_.vms());

    if (cli_.unattended()) {
        unattended_ = std::make_unique<UnattendedRunner>(services_, cli_, this);
        connect(unattended_.get(), &UnattendedRunner::finished, this, &App::exit);
        unattended_->start();
    } else if (services_.settings().is_first_run() || services_.vms().records().isEmpty()) {
        // Wizard mode: no MCP server — there is no provisioned app to
        // drive yet, and binding the port now would deny it to the
        // manager process the wizard spawns on completion.
        show_wizard();
    } else {
        start_mcp_server();
        show_main_window();
    }

    // Invoked as ``lsw-manager --launch <id>`` with no other manager
    // running: we are the primary and own the launch. Defer to a queued
    // slot so the event loop is up and the main window has shown.
    if (!cli_.launch_app_id.isEmpty()) {
        const QString id = cli_.launch_app_id;
        QMetaObject::invokeMethod(this, [this, id] { on_launch_app_request(id); },
                                  Qt::QueuedConnection);
    }
    return exec();
}

void App::show_wizard() {
    if (wizard_ == nullptr) {
        wizard_ = new wizard::Wizard(services_);
        connect(wizard_, &wizard::Wizard::complete, this, &App::on_wizard_complete);
    }
    wizard_->show();
    wizard_->raise();
    wizard_->activateWindow();
}

void App::show_main_window() {
    if (main_window_ == nullptr) {
        main_window_ = new manager::MainWindow(services_);
    }
    main_window_->show();
    main_window_->raise();
    main_window_->activateWindow();
}

void App::start_mcp_server() {
    if (mcp_server_ != nullptr) {
        return;
    }
    // Localhost-only, no auth — single-user assumption. A failed bind is
    // non-fatal; log and skip.
    mcp_server_ = std::make_unique<mcp::McpServer>(services_.rpc(), services_.log_store(), this);
    (void)mcp_server_->listen();
}

void App::on_wizard_complete() {
    services_.settings().mark_first_run_complete();
    if (wizard_ != nullptr) {
        wizard_->hide();
    }
    // Hand off to a fresh manager PROCESS rather than swapping the UI in
    // place: the wizard and the manager are deliberately separate, so the
    // everyday manager never carries the wizard's pages or pipeline.
    spawn_manager_and_quit();
}

void App::spawn_manager_and_quit() {
    // Drop the single-instance lock FIRST, otherwise the child sees us as
    // the primary and forwards a raise() back to this (dying) process.
    single_instance_.release();
    const QString exe = QCoreApplication::applicationFilePath();
    // No --launch / wizard flags: first-run is now marked complete, so a
    // bare invocation boots straight into the manager.
    if (QProcess::startDetached(exe, QStringList{})) {
        LSW_INFO("wizard complete — spawned manager process, exiting wizard");
        quit();
        return;
    }
    // Spawn failed (rare). Do not strand the user post-install: fall back
    // to bringing the manager up in this very process. Re-acquire the
    // lock we just dropped so a later second invocation still finds us.
    LSW_WARN("could not spawn manager process; showing it in-process");
    (void)single_instance_.acquire();
    start_mcp_server();
    show_main_window();
}

void App::on_raise_request() {
    QWidget* w = main_window_ != nullptr ? static_cast<QWidget*>(main_window_)
                                         : static_cast<QWidget*>(wizard_);
    if (w == nullptr) {
        return;
    }
    w->show();
    w->raise();
    w->activateWindow();
}

void App::on_launch_app_request(const QString& app_id) {
    launch_controller().launch(app_id);
}

}  // namespace lsw::host::app
