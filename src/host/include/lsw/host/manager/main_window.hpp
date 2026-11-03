#pragma once

#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/manager/guest_restart.hpp"
#include "lsw/host/manager/nav_rail.hpp"
#include "lsw/host/services/app_errors.hpp"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>
#include <optional>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace lsw::host::services { class HostServices; }

namespace lsw::host::vm { class GracefulShutdown; }

namespace lsw::host::mirror { class WindowMirror; }

namespace lsw::host::manager {

class ApplyBar;
class ConsolePanel;
class DisplayPage;
class FilesPage;
class GraphicsAlerts;
class GraphicsPage;
class LogPanel;
class MaintenancePage;
class RunOnVmDialog;
class AppErrorDialog;
class UsbPage;
class SettingsPage;
class UpdateDialog;
class VmDetailPanel;
class NoticeBoard;
class WarningStrip;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(services::HostServices& services, QWidget* parent = nullptr);

    void show_page(NavRail::Page page);

    RestartResult restart_guest(QString* reason = nullptr);

Q_SIGNALS:
    void wizard_requested();
    void launch_requested(const QString& app_id);
    void reinstall_requested();

protected:
    void closeEvent(QCloseEvent* e) override;
    bool event(QEvent* e) override;

private Q_SLOTS:
    void on_quit();
    void on_tray_activated(QSystemTrayIcon::ActivationReason reason);

private:
    void build_pages();
    QWidget* build_empty_state();
    void pop_out(NavRail::Page page);
    void collect_logs();
    void clear_logs();
    void refresh_log_buttons();
    void show_cleared();
    void open_desktop();
    void open_windows_files();
    void start_guest();
    void follow_agent_for_desktop(bool online);
    void setup_tray();
    void show_app_error(const services::AppErrorReport& report);
    void refresh_identity();
    void refresh_pending_counts();
    void refresh_detail();
    [[nodiscard]] QString page_detail(NavRail::Page page) const;
    void refresh_status_bar();
    void refresh_warning();
    void push_display_modes();
    void push_display_refresh();
    void push_dpi();
    [[nodiscard]] std::optional<config::DisplayMode> current_output() const;
    [[nodiscard]] std::optional<quint32> current_refresh_millihz() const;
    void follow_monitor_refresh();
    void on_graphics_changed();
    [[nodiscard]] bool redefine_now(const config::VmRecord& primary);

    services::HostServices& services_;

    NavRail* rail_ = nullptr;
    ApplyBar* apply_bar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* content_title_ = nullptr;
    QLabel* content_detail_ = nullptr;
    QPushButton* pop_out_button_ = nullptr;
    QPushButton* desktop_button_ = nullptr;
    QPushButton* run_on_vm_button_ = nullptr;
    QPointer<RunOnVmDialog> run_on_vm_dialog_;
    QHash<QString, QPointer<AppErrorDialog>> app_error_dialogs_;
    QPushButton* clear_button_ = nullptr;
    QPushButton* config_folder_button_ = nullptr;
    QPushButton* windows_files_button_ = nullptr;
    WarningStrip* warn_strip_ = nullptr;
    NoticeBoard* notices_ = nullptr;
    QLabel* status_left_ = nullptr;
    QString notice_;
    bool redefine_again_ = false;
    void notice_start_refused(const QString& headline);
    void follow_launches();
    void follow_upkeep();

    QStackedWidget* overview_stack_ = nullptr;
    VmDetailPanel* overview_ = nullptr;
    ConsolePanel* console_ = nullptr;
    LogPanel* log_ = nullptr;
    GraphicsAlerts* alerts_ = nullptr;
    DisplayPage* display_page_ = nullptr;
    FilesPage* files_page_ = nullptr;
    UsbPage* usb_page_ = nullptr;
    MaintenancePage* maintenance_page_ = nullptr;
    GraphicsPage* graphics_page_ = nullptr;
    SettingsPage* settings_page_ = nullptr;
    UpdateDialog* update_dialog_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    vm::GracefulShutdown* restart_ = nullptr;
    QPointer<mirror::WindowMirror> desktop_;
};

}
