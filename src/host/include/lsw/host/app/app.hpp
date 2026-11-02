#pragma once

#include "lsw/host/app/cli_options.hpp"
#include "lsw/host/platform/single_instance.hpp"
#include "lsw/host/services/host_services.hpp"

#include <QApplication>

#include <memory>

namespace lsw::host::mcp { class McpServer; }
namespace lsw::host::manager { class DocumentOpener; class MainWindow; class GuestAppLaunchController; }
namespace lsw::host::wizard { class Wizard; }

namespace lsw::host::app {

class UnattendedRunner;

class App : public QApplication {
    Q_OBJECT
public:
    App(int& argc, char** argv);
    ~App() override;

    void set_cli_options(CliOptions opts) { cli_ = std::move(opts); }

    [[nodiscard]] int run();

private Q_SLOTS:
    void on_wizard_complete();

    void on_raise_request();

    void on_launch_app_request(const QString& app_id, const QString& activation_token);

    void on_open_documents_request(const QString& app_id, const QStringList& files,
                                   const QString& activation_token);

    void on_reinstall_request();

private:
    [[nodiscard]] bool forward_to_primary();

    static void log_artifacts();

    void show_main_window();
    void show_wizard();
    [[nodiscard]] int capture_screens(const QString& dir);

    void start_mcp_server();

    void spawn_manager_and_quit();
    [[nodiscard]] bool hand_over(const QStringList& args);

    [[nodiscard]] manager::GuestAppLaunchController& launch_controller();
    [[nodiscard]] manager::DocumentOpener& document_opener();
    [[nodiscard]] manager::MainWindow& main_window();

    CliOptions cli_;
    platform::SingleInstance single_instance_;
    services::HostServices services_;

    std::unique_ptr<mcp::McpServer> mcp_server_;
    std::unique_ptr<UnattendedRunner> unattended_;
    std::unique_ptr<manager::GuestAppLaunchController> launch_controller_;
    std::unique_ptr<manager::DocumentOpener> document_opener_;
    manager::MainWindow* main_window_ = nullptr;
    wizard::Wizard* wizard_ = nullptr;
};

}
