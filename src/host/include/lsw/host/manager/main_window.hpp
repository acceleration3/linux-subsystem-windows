#pragma once

#include "lsw/host/manager/nav_rail.hpp"

#include <QMainWindow>
#include <QSystemTrayIcon>

class QLabel;
class QStackedWidget;

namespace lsw::host::services { class HostServices; }

namespace lsw::host::manager {

class ConsolePanel;
class LogPanel;
class RenderPanel;
class SettingsPage;
class VmDetailPanel;

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

private Q_SLOTS:
    void on_quit();
    void on_tray_activated(QSystemTrayIcon::ActivationReason reason);

private:
    void build_pages();
    QWidget* build_empty_state();
    void setup_tray();
    void refresh_identity();
    void refresh_status_bar();
    void refresh_warning();
    void push_display_settings();
    void restart_guest();

    services::HostServices& services_;

    NavRail* rail_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* content_title_ = nullptr;
    QLabel* content_detail_ = nullptr;
    QWidget* warn_strip_ = nullptr;
    QLabel* warn_text_ = nullptr;
    QLabel* status_left_ = nullptr;

    // Overview is two pages: the machine, and the state where there is
    // no machine yet. A manager that opens with dead buttons and blank
    // gauges is the one genuinely confusing moment in the application.
    QStackedWidget* overview_stack_ = nullptr;
    VmDetailPanel* overview_ = nullptr;
    ConsolePanel* console_ = nullptr;
    LogPanel* log_ = nullptr;
    RenderPanel* render_ = nullptr;
    SettingsPage* settings_page_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;

    bool warning_dismissed_ = false;
};

}  // namespace lsw::host::manager
