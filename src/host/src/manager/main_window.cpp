#include "lsw/host/manager/main_window.hpp"

#include "lsw/host/services/host_services.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/manager/vm_detail_panel.hpp"

#include <QCloseEvent>
#include <QMenu>

namespace lsw::host::manager {

MainWindow::MainWindow(services::HostServices& services, QWidget* parent) : QMainWindow(parent), services_(services) {
    setWindowTitle("LSW");
    resize(960, 600);

    detail_panel_ = new VmDetailPanel(services_.vms(), services_.lifecycle(), services_.monitor(),
                                       services_.app_configs(), services_.rpc(),
                                       services_.windows(), services_.log_store(), this);
    setCentralWidget(detail_panel_);

    // Single-VM model — pin the panel to whatever record App
    // populated for the lone ``lsw`` domain on startup.
    if (!services_.vms().records().isEmpty()) {
        detail_panel_->set_target(services_.vms().records().first().uuid);
    }

    setup_tray();
}

void MainWindow::setup_tray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    tray_->setToolTip("LSW");
    auto* menu = new QMenu(this);
    menu->addAction("Show", this, [this]() {
        show();
        raise();
        activateWindow();
    });
    menu->addAction("Quit", this, &MainWindow::on_quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, &MainWindow::on_tray_activated);
    tray_->show();
}

void MainWindow::on_tray_activated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible()) {
            hide();
        } else {
            show();
            raise();
            activateWindow();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (tray_ != nullptr) {
        hide();
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
}

void MainWindow::on_quit() {
    if (tray_ != nullptr) {
        tray_->hide();
    }
    qApp->quit();
}

}  // namespace lsw::host::manager
