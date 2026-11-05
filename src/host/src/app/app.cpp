#include "lsw/host/app/app.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/app/unattended_runner.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/manager/collect_logs_dialog.hpp"
#include "lsw/host/manager/document_opener.hpp"
#include "lsw/host/manager/guest_app_launch_controller.hpp"
#include "lsw/host/manager/app_config_dialog.hpp"
#include "lsw/host/manager/app_error_dialog.hpp"
#include "lsw/host/manager/launch_splash.hpp"
#include "lsw/host/manager/update_dialog.hpp"
#include "lsw/host/services/guest_update_status.hpp"
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
#include "lsw/host/wizard/vm_size_page.hpp"
#include "lsw/host/wizard/wizard.hpp"

#include <QDir>
#include <QIcon>
#include <QMetaObject>
#include <QProcess>

#include <cstdint>
#include <string>
#include <utility>

namespace lsw::host::app {

App::App(int& argc, char** argv)
    : QApplication(argc, argv), single_instance_(this), services_(this) {
    setApplicationName("lsw-manager");
    setOrganizationName("lsw");
    setOrganizationDomain("lsw");
    setDesktopFileName(QStringLiteral("lsw-manager"));
    setWindowIcon(QIcon(QStringLiteral(":/lsw/lsw-manager.svg")));

    ui::Theme::apply();

    auto& configs = services_.app_configs();
    connect(&configs, &apps::AppConfigStore::changed, this,
            [this] { services_.apply_integrations(); });

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
        document_opener_->set_restarter(
            [this](QString* reason) { return main_window().restart_guest(reason); });
    }
    return *document_opener_;
}

manager::MainWindow& App::main_window() {
    if (main_window_ == nullptr) {
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
    if (!cli_.screenshot_dir.isEmpty()) {
        log_artifacts();
        (void)services_.open();
        services_.upkeep().disable();
        (void)vm::DomainImporter::import_existing(services_.libvirt(),
                                                  services_.settings(), services_.vms());
        return capture_screens(cli_.screenshot_dir);
    }
    if (forward_to_primary()) {
        return 0;
    }
    apps::DesktopLauncher::sync_manager_entry();
    log_artifacts();
    if (!services_.open()) {
        return 1;
    }
    (void)vm::DomainImporter::import_existing(services_.libvirt(), services_.settings(),
                                              services_.vms());
    services_.apply_integrations();
    services_.host_shutdown().start();
    services_.clipboard().start();

    if (cli_.unattended()) {
        unattended_ = std::make_unique<UnattendedRunner>(services_, cli_, this);
        connect(unattended_.get(), &UnattendedRunner::finished, this, &App::exit);
        unattended_->start();
    } else if (cli_.reinstall || services_.settings().is_first_run()
               || services_.vms().primary() == nullptr) {
        show_wizard();
    } else {
        start_mcp_server();
        show_main_window();
        const config::VmRecord* primary = services_.vms().primary();
        if (services_.manager_settings().bool_value(config::ManagerSettings::Key::kAutostart)
            && primary != nullptr && cli_.launch_app_id.isEmpty() && cli_.files.isEmpty()
            && !services_.lifecycle().is_running(primary->uuid)) {
            (void)services_.start_vm(primary->uuid);
        }
    }

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
    for (int i = 0; i < 100 && !services_.status().graphics().probed; ++i) {
        processEvents(QEventLoop::AllEvents, 50);
    }
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
        {manager::NavRail::Page::Settings, "manager-06-settings-full", 3600},
        {manager::NavRail::Page::Display, "manager-07-display", 900},
        {manager::NavRail::Page::Files, "manager-08-files", 700},
        {manager::NavRail::Page::Usb, "manager-09-usb", 800},
        {manager::NavRail::Page::Maintenance, "manager-10-maintenance", 700},
        {manager::NavRail::Page::GpuStatus, "manager-11-gpu-status", 860},
    };
    for (const auto& entry : kPages) {
        window->resize(1000, entry.height);
        window->show_page(entry.page);
        shoot(window, QString::fromLatin1(entry.name));
    }
    window->deleteLater();

    {
        tools::DownloadImageDialog download{services_.settings()};
        download.resize(620, 620);
        shoot(&download, QStringLiteral("dialog-1-download"));

        tools::TrimImageDialog trim{services_.settings(), QString{}};
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

        services::GuestUpdateStatus demo;
        demo.state = services::GuestUpdateStatus::State::Running;
        demo.step = QStringLiteral("Updating Windows");
        demo.guest_version = QStringLiteral("0.1.0");
        demo.shipped_version = QStringLiteral("0.2.0");
        const services::GuestUpdateStatus real_update = services_.status().update();
        demo.live_steps = {
            {QStringLiteral("verify"), QStringLiteral("done"), {}},
            {QStringLiteral("stop"), QStringLiteral("done"), {}},
            {QStringLiteral("agent and mirror"), QStringLiteral("done"), {}},
            {QStringLiteral("driver"), QStringLiteral("running"), {}},
            {QStringLiteral("loaders"), QStringLiteral("pending"), {}},
            {QStringLiteral("shared folders"), QStringLiteral("pending"), {}},
            {QStringLiteral("service and firewall"), QStringLiteral("pending"), {}},
            {QStringLiteral("start"), QStringLiteral("pending"), {}},
        };
        manager::UpdateDialog updating{services_.status()};
        services_.status().set_update(demo);
        shoot(&updating, QStringLiteral("dialog-7-update"));

        services::GuestUpdateStatus finished = demo;
        finished.state = services::GuestUpdateStatus::State::Succeeded;
        finished.step.clear();
        finished.guest_version = finished.shipped_version;
        finished.restart = services::GuestUpdateStatus::Restart::NotNeeded;
        for (auto& step : finished.live_steps) {
            step.status = step.name == QLatin1String("loaders") ? QStringLiteral("skipped")
                                                                  : QStringLiteral("done");
        }
        services_.status().set_update(finished);
        shoot(&updating, QStringLiteral("dialog-7-update-finished"));

        services::GuestUpdateStatus restarting = demo;
        restarting.restart = services::GuestUpdateStatus::Restart::Running;
        restarting.restart_reasons = {QStringLiteral("the GPU driver did not switch over live")};
        for (auto& step : restarting.live_steps) {
            step.status = QStringLiteral("done");
        }
        services_.status().set_update(restarting);
        shoot(&updating, QStringLiteral("dialog-7-update-restarting"));
        services_.status().set_update(real_update);
        updating.close();

        manager::CollectLogsDialog collecting{services_.log_collector()};
        collecting.setAttribute(Qt::WA_DeleteOnClose, false);
        for (int step = 0; step < services::LogCollector::StepCount; ++step) {
            Q_EMIT services_.log_collector().step_started(step);
        }
        Q_EMIT services_.log_collector().finished(
            QDir(services_.settings().logs_dir()).filePath(QStringLiteral("lsw-logs-20261102-143518.tar.gz")));
        shoot(&collecting, QStringLiteral("dialog-8-collect-logs"));

        using Problem = services::AppErrorReport::Problem;
        services::AppErrorReport startup;
        startup.kind = services::AppErrorReport::Kind::Startup;
        startup.image = QStringLiteral("C:\\Games\\th06c\\th06c.exe");
        startup.pid = 5604;
        startup.exit_code = 0xC0000135;
        startup.exit_code_hex = QStringLiteral("0xC0000135");
        startup.code_name = QStringLiteral("DLL not found");
        for (const QString& module : {QStringLiteral("MSVCP140.dll"), QStringLiteral("VCRUNTIME140.dll"),
                                      QStringLiteral("VCRUNTIME140_1.dll")}) {
            Problem problem;
            problem.module = module;
            problem.importer = QStringLiteral("th06c.exe");
            startup.problems.append(problem);
        }
        manager::AppErrorDialog start_error{startup};
        start_error.setAttribute(Qt::WA_DeleteOnClose, false);
        shoot(&start_error, QStringLiteral("dialog-9-app-startup-error"));

        services::AppErrorReport crash;
        crash.kind = services::AppErrorReport::Kind::Crash;
        crash.image = QStringLiteral("C:\\Tools\\crashme.exe");
        crash.pid = 6536;
        crash.exit_code = 0xC0000005;
        crash.exit_code_hex = QStringLiteral("0xC0000005");
        crash.code_name = QStringLiteral("access violation");
        crash.module = QStringLiteral("crashme.exe");
        crash.offset = QStringLiteral("0x1009");
        manager::AppErrorDialog crash_error{crash};
        crash_error.setAttribute(Qt::WA_DeleteOnClose, false);
        shoot(&crash_error, QStringLiteral("dialog-10-app-crash-error"));
    }

    auto* wiz = new wizard::Wizard(services_);
    wiz->resize(880, 620);
    for (int id = wizard::Wizard::Welcome; id <= wizard::Wizard::Done; ++id) {
        wiz->setStartId(id);
        wiz->restart();
        if (auto* payload = qobject_cast<wizard::PayloadPage*>(wiz->currentPage())) {
            payload->demo_fill();
        }
        if (auto* size = qobject_cast<wizard::VmSizePage*>(wiz->currentPage())) {
            size->demo_fill();
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
    mcp_server_ = std::make_unique<mcp::McpServer>(services_.rpc(), services_.log_store(), services_.guest_update(), services_.status(), services_.log_collector(), services_.log_cleaner(), this);
    const int port = qEnvironmentVariableIntValue("LSW_MCP_PORT");
    (void)mcp_server_->listen(port > 0 ? static_cast<std::uint16_t>(port) : std::uint16_t{5105});
}

void App::on_wizard_complete() {
    services_.settings().mark_first_run_complete();
    if (wizard_ != nullptr) {
        wizard_->hide();
    }
    spawn_manager_and_quit();
}

void App::spawn_manager_and_quit() {
    if (hand_over({})) {
        LSW_INFO("wizard done: spawned manager process, exiting wizard");
        return;
    }
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

}
