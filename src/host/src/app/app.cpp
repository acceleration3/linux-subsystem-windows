#include "lsw/host/app/app.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/app/unattended_runner.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/manager/document_opener.hpp"
#include "lsw/host/manager/guest_app_launch_controller.hpp"
#include "lsw/host/manager/app_config_dialog.hpp"
#include "lsw/host/manager/launch_splash.hpp"
#include "lsw/host/manager/main_window.hpp"
#include "lsw/host/manager/open_document_dialog.hpp"
#include "lsw/host/manager/remote_file_dialog.hpp"
#include "lsw/host/tools/download_image_dialog.hpp"
#include "lsw/host/tools/trim_image_dialog.hpp"
#include "lsw/host/mcp/mcp_server.hpp"
#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/platform/agent_exe_path.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"
#include "lsw/host/vm/domain_importer.hpp"
#include "lsw/host/wizard/payload_page.hpp"
#include "lsw/host/wizard/setup_page.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QDir>
#include <QIcon>
#include <QMetaObject>
#include <QProcess>

#include <string>
#include <utility>

namespace lsw::host::app {

App::App(int& argc, char** argv)
    : QApplication(argc, argv), single_instance_(this), services_(this) {
    setApplicationName("lsw-manager");
    setOrganizationName("lsw");
    setOrganizationDomain("lsw");
    // The installed entry's icon, and the one every window wears where the
    // desktop does not look the entry up (packaging/gen-icon.py draws it).
    setDesktopFileName(QStringLiteral("lsw-manager"));
    setWindowIcon(QIcon(QStringLiteral(":/lsw/lsw-manager.svg")));

    // Before any window exists, so the wizard and the manager and every
    // dialog either of them opens are all wearing it from their first
    // paint. Widgets carry object names, not stylesheets of their own.
    ui::Theme::apply();

    // Sync the host's XDG menu with whatever app configs are already on
    // disk — covers opening a .desktop entry that has been there a while
    // without first opening the manager UI.
    // Through the services rather than straight at DesktopLauncher, so
    // the category, the publish switch and the file-type handlers are
    // all applied from settings on the same pass.
    auto& configs = services_.app_configs();
    connect(&configs, &apps::AppConfigStore::changed, this,
            [this] { services_.apply_integrations(); });

    // Turned OFF only once a real, tray-backed main window is up (see
    // ``show_main_window``). During the wizard phase we keep it ON so
    // closing the wizard actually exits the process — otherwise the event
    // loop has no UI and no tray icon and only Ctrl-C ends it.
    setQuitOnLastWindowClosed(true);

    connect(&single_instance_, &platform::SingleInstance::raise_requested,
            this, &App::on_raise_request);
    connect(&single_instance_, &platform::SingleInstance::launch_app_requested,
            this, &App::on_launch_app_request);
    connect(&single_instance_, &platform::SingleInstance::open_documents_requested,
            this, &App::on_open_documents_request);
}

App::~App() = default;

manager::GuestAppLaunchController& App::launch_controller() {
    if (launch_controller_ == nullptr) {
        launch_controller_ =
            std::make_unique<manager::GuestAppLaunchController>(services_, this);
        // The splash offers the log when a guest goes quiet during a
        // launch, and the log lives in the manager window, which this
        // path deliberately never shows on its own.
        connect(launch_controller_.get(),
                &manager::GuestAppLaunchController::log_requested, this, [this] {
                    show_main_window();
                    if (main_window_ != nullptr) {
                        main_window_->show_page(manager::NavRail::Page::Log);
                    }
                });
    }
    return *launch_controller_;
}

manager::DocumentOpener& App::document_opener() {
    if (document_opener_ == nullptr) {
        document_opener_ = std::make_unique<manager::DocumentOpener>(services_, launch_controller(), this);
        // Sharing a folder to open a file from it is a restart of the guest,
        // which is the manager window's to do.
        document_opener_->set_restarter(
            [this](QString* reason) { return main_window().restart_guest(reason); });
    }
    return *document_opener_;
}

manager::MainWindow& App::main_window() {
    if (main_window_ == nullptr) {
        // Shown by show_main_window when somebody asks for it.
        main_window_ = new manager::MainWindow(services_);
        connect(main_window_, &manager::MainWindow::wizard_requested, this, &App::show_wizard);
        connect(main_window_, &manager::MainWindow::launch_requested, this,
                [this](const QString& app_id) { on_launch_app_request(app_id, QString()); });
        connect(main_window_, &manager::MainWindow::reinstall_requested, this,
                &App::on_reinstall_request);
    }
    return *main_window_;
}

bool App::forward_to_primary() {
    if (single_instance_.acquire()) {
        return false;
    }
    // Another manager owns the DBus name. If we were invoked with
    // ``--launch <id>`` (an XDG launcher click on a guest app entry),
    // forward the id to that instance and exit — the primary pops the
    // splash and drives the VM. Files to open go the same way, with the
    // application or with none. Bare re-invocations raise its window.
    if (!cli_.files.isEmpty()) {
        LSW_INFO("forwarding {} file(s) to running manager (application: {})", cli_.files.size(),
                 cli_.launch_app_id.isEmpty() ? std::string{"Windows' default"}
                                              : cli_.launch_app_id.toStdString());
        (void)single_instance_.open_documents_existing(cli_.launch_app_id, cli_.files,
                                                       cli_.activation_token);
    } else if (!cli_.launch_app_id.isEmpty()) {
        LSW_INFO("forwarding --launch to running manager: {}",
                 cli_.launch_app_id.toStdString());
        (void)single_instance_.launch_app_existing(cli_.launch_app_id, cli_.activation_token);
    } else {
        LSW_INFO("another lsw-manager is already running; raising it");
        (void)single_instance_.raise_existing();
    }
    return true;
}

void App::log_artifacts() {
    using platform::AccelVirtPaths;
    const std::pair<const char*, QString> found[] = {
        {"qemu", AccelVirtPaths::qemu_binary()},
        {"qemu data (-L)", AccelVirtPaths::qemu_data_dir()},
        {"executor", AccelVirtPaths::executor()},
        {"host d3d", AccelVirtPaths::d3d_lib_dir()},
        {"driver tree", AccelVirtPaths::driver_tree()},
        {"vulkan-1.dll x64", AccelVirtPaths::vulkan_loader(false)},
        {"vulkan-1.dll x86", AccelVirtPaths::vulkan_loader(true)},
        {"guest agent", platform::AgentExePath::locate()},
        {"mirror (host)", platform::MirrorExePaths::host()},
        {"mirror (guest)", platform::MirrorExePaths::guest()},
    };
    for (const auto& [what, path] : found) {
        LSW_INFO("artifact: {:<17} {}", what,
                 path.isEmpty() ? std::string{"(not found)"} : path.toStdString());
    }
}

int App::run() {
    // Before the single-instance check: a screenshot run must not
    // forward itself to whatever manager happens to be open, and it
    // owns nothing another one would mind.
    if (!cli_.screenshot_dir.isEmpty()) {
        log_artifacts();
        (void)services_.open();
        (void)vm::DomainImporter::import_existing(services_.libvirt(),
                                                  services_.settings(), services_.vms());
        return capture_screens(cli_.screenshot_dir);
    }
    if (forward_to_primary()) {
        return 0;
    }
    // Not before the check above: a second process that hands its request on
    // and exits (every file opened from a file manager) writes nothing. Still
    // before the event loop, whose first turn has the desktop portal look the
    // entry up by this name.
    apps::DesktopLauncher::sync_manager_entry();
    log_artifacts();
    if (!services_.open()) {
        return 1;
    }
    (void)vm::DomainImporter::import_existing(services_.libvirt(), services_.settings(),
                                              services_.vms());
    // Now that we know which VMs exist, the integrations that need one
    // can be published.
    services_.apply_integrations();

    if (cli_.unattended()) {
        unattended_ = std::make_unique<UnattendedRunner>(services_, cli_, this);
        connect(unattended_.get(), &UnattendedRunner::finished, this, &App::exit);
        unattended_->start();
    } else if (cli_.reinstall || services_.settings().is_first_run()
               || services_.vms().primary() == nullptr) {
        // Wizard mode: no MCP server — there is no provisioned app to
        // drive yet, and binding the port now would deny it to the
        // manager process the wizard spawns on completion.
        show_wizard();
    } else {
        start_mcp_server();
        show_main_window();
        // "Start the guest when the manager opens", for the manager that
        // opens on its own; a --launch click starts it anyway.
        const config::VmRecord* primary = services_.vms().primary();
        if (services_.manager_settings().bool_value(config::ManagerSettings::Key::kAutostart)
            && primary != nullptr && cli_.launch_app_id.isEmpty() && cli_.files.isEmpty()
            && !services_.lifecycle().is_running(primary->uuid)) {
            (void)services_.start_vm(primary->uuid);
        }
    }

    // Invoked as ``lsw-manager --launch <id>`` with no other manager
    // running: we are the primary and own the launch. Defer to a queued
    // slot so the event loop is up and the main window has shown.
    if (!cli_.files.isEmpty()) {
        const QString id = cli_.launch_app_id;
        const QStringList files = cli_.files;
        const QString token = cli_.activation_token;
        QMetaObject::invokeMethod(
            this, [this, id, files, token] { on_open_documents_request(id, files, token); },
            Qt::QueuedConnection);
    } else if (!cli_.launch_app_id.isEmpty()) {
        const QString id = cli_.launch_app_id;
        const QString token = cli_.activation_token;
        QMetaObject::invokeMethod(this, [this, id, token] { on_launch_app_request(id, token); },
                                  Qt::QueuedConnection);
    }
    return exec();
}

int App::capture_screens(const QString& dir) {
    QDir().mkpath(dir);
    int saved = 0;

    const auto shoot = [&](QWidget* widget, const QString& name) {
        // Show it, let the layout settle, grab it. Offscreen is enough:
        // the grab renders the widget itself rather than reading a
        // framebuffer, so no display server is needed to see what the
        // stylesheet did.
        widget->show();
        for (int i = 0; i < 4; ++i) {
            processEvents(QEventLoop::AllEvents, 50);
        }
        const QString path = QDir(dir).filePath(name + QStringLiteral(".png"));
        if (widget->grab().save(path)) {
            ++saved;
            LSW_INFO("screenshot: {}", path.toStdString());
        } else {
            LSW_WARN("screenshot: could not write {}", path.toStdString());
        }
    };

    auto* window = new manager::MainWindow(services_);
    window->resize(1000, 620);
    // The host probe runs in the background; a picture of the Graphics
    // page before it answers is a picture of "asking".
    for (int i = 0; i < 100 && !services_.status().graphics().probed; ++i) {
        processEvents(QEventLoop::AllEvents, 50);
    }
    // Each at the height the design draws it at: the Graphics and
    // Settings pages are drawn taller than the window opens, and a
    // picture of their first screenful would compare against half of it.
    static constexpr struct {
        manager::NavRail::Page page;
        const char* name;
        int height;
    } kPages[] = {
        {manager::NavRail::Page::Overview, "manager-01-overview", 620},
        {manager::NavRail::Page::Console, "manager-02-console", 620},
        {manager::NavRail::Page::Log, "manager-03-log", 620},
        {manager::NavRail::Page::Graphics, "manager-04-graphics", 860},
        {manager::NavRail::Page::Settings, "manager-05-settings", 900},
        // The whole settings page at once: the tables further down are
        // where rows of controls get clipped, and the first screenful
        // does not reach them.
        {manager::NavRail::Page::Settings, "manager-06-settings-full", 2200},
    };
    for (const auto& entry : kPages) {
        window->resize(1000, entry.height);
        window->show_page(entry.page);
        shoot(window, QString::fromLatin1(entry.name));
    }
    window->deleteLater();

    // The dialogs, too. They are where most of the controls live, and a
    // screenshot set that skips them checks the easy half.
    {
        tools::DownloadImageDialog download{services_.settings()};
        download.resize(620, 620);
        shoot(&download, QStringLiteral("dialog-1-download"));

        tools::TrimImageDialog trim{services_.settings(), QString{}};
        trim.resize(620, 790);
        shoot(&trim, QStringLiteral("dialog-2-trim"));

        apps::AppConfig sample;
        sample.name = QStringLiteral("Photoshop 2024");
        sample.path = QStringLiteral("C:\\Program Files\\Adobe\\Photoshop.exe");
        manager::AppConfigDialog config{sample, services_.rpc()};
        config.resize(560, 520);
        shoot(&config, QStringLiteral("dialog-3-application"));

        manager::RemoteFileDialog picker{services_.rpc(),
                                         manager::RemoteFileDialog::Mode::Program};
        shoot(&picker, QStringLiteral("dialog-4-browse"));

        manager::LaunchSplash splash{QStringLiteral("Photoshop 2024"), QIcon{},
                                     services_.status().last_boot_ms()};
        splash.reach(manager::LaunchSplash::Phase::MachineStarted);
        shoot(&splash, QStringLiteral("dialog-5-starting"));

        manager::OpenDocumentDialog open_document{
            {QStringLiteral("/home/accel/Documents/work/report.xlsx")},
            {QStringLiteral("/home/accel/Documents/work")},
            QStringLiteral("Excel"),
            true,
            0};
        open_document.resize(560, 290);
        shoot(&open_document, QStringLiteral("dialog-6-open-document"));
    }

    auto* wiz = new wizard::Wizard(services_);
    wiz->resize(880, 620);
    for (int id = wizard::Wizard::Welcome; id <= wizard::Wizard::Done; ++id) {
        // Straight to the page rather than through next(): the later
        // ones validate inputs the earlier ones collect, and this is a
        // picture of a page, not a run of the wizard.
        wiz->setStartId(id);
        wiz->restart();
        // Two pages are tables the pipeline fills, and there is no
        // pipeline here. An empty table is a picture of nothing, so
        // they are asked for a plausible run instead.
        if (auto* payload = qobject_cast<wizard::PayloadPage*>(wiz->currentPage())) {
            payload->demo_fill();
        }
        if (auto* setup = qobject_cast<wizard::SetupPage*>(wiz->currentPage())) {
            setup->demo_fill();
        }
        shoot(wiz, QStringLiteral("wizard-%1-page").arg(id + 1, 2, 10, QLatin1Char('0')));
    }
    wiz->deleteLater();

    LSW_INFO("screenshot: {} images written to {}", saved, dir.toStdString());
    return saved > 0 ? 0 : 1;
}

void App::show_wizard() {
    if (wizard_ == nullptr) {
        wizard_ = new wizard::Wizard(services_);
        connect(wizard_, &wizard::Wizard::complete, this, &App::on_wizard_complete);
        // Closed before its install replaced the guest, a reinstall
        // leaves the guest as it was, and the manager comes back to it.
        // Closed after, the manager finds no guest and offers the wizard
        // again.
        if (cli_.reinstall) {
            connect(wizard_, &QDialog::rejected, this, &App::spawn_manager_and_quit);
        }
    }
    ui::Widgets::bring_to_front(wizard_);
}

void App::show_main_window() {
    ui::Widgets::bring_to_front(&main_window());
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
    // No --launch / wizard flags: first-run is now marked complete, so a
    // bare invocation boots straight into the manager.
    if (hand_over({})) {
        LSW_INFO("wizard done: spawned manager process, exiting wizard");
        return;
    }
    // Spawn failed (rare). Do not strand the user post-install: fall back
    // to bringing the manager up in this very process.
    LSW_WARN("could not spawn manager process; showing it in-process");
    start_mcp_server();
    show_main_window();
}

bool App::hand_over(const QStringList& args) {
    single_instance_.release();
    if (QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) {
        quit();
        return true;
    }
    (void)single_instance_.acquire();
    return false;
}

void App::on_reinstall_request() {
    // A process of its own, as on a first run: the wizard and the
    // manager are kept apart, and the wizard's pipeline wants the agent
    // channel, the status hub and the MCP port this manager holds. The
    // guest is left running; the install stops it when it gets there.
    if (hand_over({QStringLiteral("--reinstall")})) {
        LSW_INFO("reinstall: spawned the wizard process, exiting the manager");
        return;
    }
    LSW_WARN("reinstall: could not spawn the wizard process");
}

void App::on_raise_request() {
    QWidget* w = main_window_ != nullptr ? static_cast<QWidget*>(main_window_)
                                         : static_cast<QWidget*>(wizard_);
    if (w == nullptr) {
        return;
    }
    ui::Widgets::bring_to_front(w);
}

void App::on_open_documents_request(const QString& app_id, const QStringList& files,
                                    const QString& activation_token) {
    document_opener().open(app_id, files, activation_token);
}

void App::on_launch_app_request(const QString& app_id, const QString& activation_token) {
    launch_controller().launch(app_id, activation_token);
}

}  // namespace lsw::host::app
