#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>

namespace lsw::host::services { class HostServices; }

namespace lsw::host::manager {

class VmDetailPanel;

// Manager shell. Single-VM model — there's no list to pick from, so
// the central widget is just the VmDetailPanel pinned to the lone
// ``lsw`` VM. Closing the window minimizes to the system tray.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(services::HostServices& services, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;

private Q_SLOTS:
    void on_quit();
    void on_tray_activated(QSystemTrayIcon::ActivationReason reason);

private:
    void setup_tray();

    services::HostServices& services_;
    VmDetailPanel* detail_panel_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
};

}  // namespace lsw::host::manager
