#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>

namespace lsw::manager {
class App;
}

namespace lsw::manager::ui::manager {

class VmDetailPanel;

// Manager shell. Single-VM model — there's no list to pick from, so
// the central widget is just the VmDetailPanel pinned to the lone
// ``lsw`` VM. Closing the window minimizes to the system tray.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(App& app, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;

private Q_SLOTS:
    void on_quit();
    void on_tray_activated(QSystemTrayIcon::ActivationReason reason);

private:
    void setup_tray();

    App& app_;
    VmDetailPanel* detail_panel_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
};

}  // namespace lsw::manager::ui::manager
