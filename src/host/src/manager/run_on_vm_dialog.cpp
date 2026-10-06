#include "lsw/host/manager/run_on_vm_dialog.hpp"

#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/guest_app_launcher.hpp"
#include "lsw/host/apps/guest_file_transfer.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/linkproto/app_package.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

RunOnVmDialog::RunOnVmDialog(link::AgentRpcClient& rpc, apps::GuestAppLauncher& launcher,
                             QWidget* parent)
    : QDialog(parent), launcher_(launcher) {
    setWindowTitle(QStringLiteral("Run on VM"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(600, 260);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(QStringLiteral(
        "Choose a Windows program or installer from this computer. LSW copies it into "
        "Windows and opens its windows here. MSIX packages open App Installer in its own "
        "mirrored window. Only run files you trust."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* form = new QFormLayout;
    auto* file_row = new QHBoxLayout;
    path_ = new QLineEdit;
    path_->setReadOnly(true);
    path_->setPlaceholderText(QStringLiteral("Choose a local file…"));
    browse_ = new QPushButton(QStringLiteral("Browse…"));
    file_row->addWidget(path_, 1);
    file_row->addWidget(browse_);
    form->addRow(QStringLiteral("File"), file_row);
    args_ = new QLineEdit;
    args_->setPlaceholderText(QStringLiteral("Optional Windows command-line arguments"));
    form->addRow(QStringLiteral("Arguments"), args_);
    layout->addLayout(form);

    status_ = new QLabel(QStringLiteral(
        "Files are kept in C:\\ProgramData\\LSW\\uploads. Only the selected file is copied; "
        "programs needing companion files must be copied with their folder separately."));
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    // What went wrong, apart from the progress line: the design's red
    // callout, until the next attempt.
    error_ = new QLabel;
    error_->setObjectName(QString::fromLatin1(ui::Theme::Role::kCalloutCritical));
    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    error_->hide();
    layout->addWidget(error_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->hide();
    layout->addWidget(progress_);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    close_ = new QPushButton(QStringLiteral("Close"));
    run_ = new QPushButton(QStringLiteral("Run on VM"));
    run_->setDefault(true);
    run_->setEnabled(false);
    buttons->addWidget(close_);
    buttons->addWidget(run_);
    layout->addLayout(buttons);

    transfer_ = new apps::GuestFileTransfer(rpc, this);
    connect(browse_, &QPushButton::clicked, this, &RunOnVmDialog::browse);
    connect(run_, &QPushButton::clicked, this, &RunOnVmDialog::run);
    connect(close_, &QPushButton::clicked, this, &RunOnVmDialog::reject);
    connect(transfer_, &apps::GuestFileTransfer::progress, this, [this](qint64 sent, qint64 total) {
        progress_->setValue(static_cast<int>(sent * 100 / total));
        status_->setText(QStringLiteral("Copying to Windows: %1 / %2")
                             .arg(ui::Format::bytes(static_cast<quint64>(sent)),
                                  ui::Format::bytes(static_cast<quint64>(total))));
    });
    connect(transfer_, &apps::GuestFileTransfer::failed, this, [this](const QString& error) {
        if (closing_)
            QDialog::reject();
        else
            show_error(error);
    });
    connect(transfer_, &apps::GuestFileTransfer::cancelled, this, [this] { QDialog::reject(); });
    connect(transfer_, &apps::GuestFileTransfer::transferred, this, [this](const QString& path) {
        guest_path_ = path;
        close_->setText(QStringLiteral("Close"));
        progress_->setRange(0, 0);
        status_->setText(QStringLiteral("Copied to %1. Opening in Windows…").arg(path));
        apps::AppConfig app;
        app.path = path;
        app.name = QFileInfo(path_->text()).fileName();
        app.cmdline = args_->text();
        launcher_.launch(app);
    });
    connect(&launcher_, &apps::GuestAppLauncher::launched, this,
            [this](const QString& path, quint32) {
                if (!busy_ || path != guest_path_)
                    return;
                set_busy(false);
                progress_->setRange(0, 100);
                progress_->setValue(100);
                status_->setText(
                    linkproto::is_app_package(path.toStdString())
                        ? QStringLiteral("App Installer is open in its own window. Review the "
                                         "package and click Install there. File saved at %1")
                              .arg(path)
                        : QStringLiteral("Running in Windows. File saved at %1").arg(path));
            });
    connect(&launcher_, &apps::GuestAppLauncher::failed, this,
            [this](const QString& path, const QString& error) {
                if (busy_ && path == guest_path_)
                    show_error(error);
            });
}

void RunOnVmDialog::set_agent_online(bool online) {
    online_ = online;
    run_->setEnabled(online_ && !busy_ && !path_->text().isEmpty());
    run_->setToolTip(online ? QString{} : QStringLiteral("Start Windows and wait for its agent"));
}

void RunOnVmDialog::browse() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose a Windows program or installer"), path_->text(),
        QStringLiteral(
            "Windows programs and installers "
            "(*.exe *.EXE *.msi *.MSI *.msp *.MSP *.com *.COM *.scr *.SCR "
            "*.msix *.MSIX *.msixbundle *.MSIXBUNDLE *.appx *.APPX *.appxbundle *.APPXBUNDLE "
            "*.bat *.BAT *.cmd *.CMD *.ps1 *.PS1 *.vbs *.VBS *.js *.JS *.wsf *.WSF);;"
            "All files (*)"));
    if (path.isEmpty())
        return;
    path_->setText(path);
    const bool package = linkproto::is_app_package(path.toStdString());
    if (package)
        args_->clear();
    args_->setEnabled(!package);
    args_->setPlaceholderText(package ? QStringLiteral("Not used for App Installer packages")
                                      : QStringLiteral("Optional Windows command-line arguments"));
    run_->setText(package ? QStringLiteral("Open App Installer") : QStringLiteral("Run on VM"));
    set_agent_online(online_);
}

void RunOnVmDialog::set_busy(bool busy) {
    busy_ = busy;
    browse_->setEnabled(!busy);
    args_->setEnabled(!busy && !linkproto::is_app_package(path_->text().toStdString()));
    close_->setText(transfer_->busy() ? QStringLiteral("Cancel") : QStringLiteral("Close"));
    set_agent_online(online_);
}

void RunOnVmDialog::run() {
    if (busy_ || !online_ || path_->text().isEmpty())
        return;
    guest_path_.clear();
    closing_ = false;
    error_->hide();
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->show();
    set_busy(true);
    transfer_->start(path_->text());
    if (transfer_->busy())
        close_->setText(QStringLiteral("Cancel"));
}

void RunOnVmDialog::show_error(const QString& error) {
    set_busy(false);
    progress_->setRange(0, 100);
    progress_->hide();
    status_->setText(guest_path_.isEmpty() ? QStringLiteral("Nothing was run.")
                                           : QStringLiteral("The copied file remains at %1.").arg(guest_path_));
    error_->setText(QStringLiteral("Could not run on VM: %1").arg(error));
    error_->show();
}

void RunOnVmDialog::reject() {
    if (transfer_->busy()) {
        closing_ = true;
        close_->setEnabled(false);
        status_->setText(QStringLiteral("Cancelling the transfer…"));
        transfer_->cancel();
        return;
    }
    QDialog::reject();
}

}  // namespace lsw::host::manager
