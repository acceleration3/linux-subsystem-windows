#pragma once

#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/manager/nav_rail.hpp"

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <optional>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace lsw::host::services { class HostServices; }

namespace lsw::host::vm { class GracefulShutdown; }

namespace lsw::host::manager {

class ConsolePanel;
class GraphicsAlerts;
class GraphicsPage;
class LogPanel;
class SettingsPage;
class VmDetailPanel;
class WarningStrip;

// Manager shell: a navigation rail down the left, one page at a time
// beside it, and a status bar carrying the facts that used to live only
// in a log.
//
// The window used to be a single panel with four buttons across its
// top. Those buttons could not say where you were, two of them were
// always disabled, and everything they opened became a separate
// top-level window. Now the pages are pages, the rail marks the current
// one, and Start and Stop are one control that says what it does.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(services::HostServices& services, QWidget* parent = nullptr);

    // Bring a specific page up, used by the pages themselves when one
    // hands off to another.
    void show_page(NavRail::Page page);

Q_SIGNALS:
    // There is no guest, and the only thing that leads anywhere is the
    // wizard. The application owns that flow, not this window.
    void wizard_requested();

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
    void setup_tray();
    void refresh_identity();
    [[nodiscard]] QString page_detail(NavRail::Page page) const;
    void refresh_status_bar();
    // The overview's strip and the rail's dot, from the Graphics page's
    // warnings.
    void refresh_warning();
    // The resolutions, to the running device (a qom-set; no agent
    // needed), and the DPI, to the agent (read at the next logon).
    void push_display_modes();
    void push_dpi();
    // The monitor this window is on, for "match the monitor".
    [[nodiscard]] std::optional<config::DisplayMode> current_output() const;
    // Rewrite the definition, then stop the guest — waiting until it is
    // off, forcing it at a deadline — and start it on the new one.
    void restart_guest();
    void on_graphics_changed();

    services::HostServices& services_;

    NavRail* rail_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* content_title_ = nullptr;
    QLabel* content_detail_ = nullptr;
    QPushButton* pop_out_button_ = nullptr;
    // Per-page actions in the content bar. Shown for the page they
    // belong to and hidden for the rest, rather than each page growing
    // a toolbar of its own.
    QPushButton* preview_button_ = nullptr;
    QPushButton* clear_button_ = nullptr;
    QPushButton* config_folder_button_ = nullptr;
    QPushButton* change_settings_button_ = nullptr;
    // The two Graphics warnings that break the guest, repeated on the
    // overview with a way to the page that says more.
    WarningStrip* warn_strip_ = nullptr;
    QLabel* status_left_ = nullptr;
    // Something this window did that the status bar says ahead of the
    // facts until it is over: a restart in progress, a resolution the
    // device refused. The facts themselves are redrawn from the status
    // hub on every reading, so a message written straight into the bar
    // would be gone two seconds later.
    QString notice_;

    // Overview is two pages: the machine, and the state where there is
    // no machine yet. A manager that opens with dead buttons and blank
    // gauges is the one genuinely confusing moment in the application.
    QStackedWidget* overview_stack_ = nullptr;
    VmDetailPanel* overview_ = nullptr;
    ConsolePanel* console_ = nullptr;
    LogPanel* log_ = nullptr;
    GraphicsAlerts* alerts_ = nullptr;
    GraphicsPage* graphics_page_ = nullptr;
    SettingsPage* settings_page_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    // A restart in progress: the guest is being stopped so it can be
    // started on a new definition.
    vm::GracefulShutdown* restart_ = nullptr;
};

}  // namespace lsw::host::manager
