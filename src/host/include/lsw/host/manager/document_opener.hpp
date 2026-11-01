#pragma once

#include "lsw/host/manager/guest_restart.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace lsw::host::services {
class HostServices;
}

namespace lsw::host::manager {

class GuestAppLaunchController;

// Opens files from this machine in Windows: double-click report.xlsx in
// the file manager and Excel opens that document in its mirrored window.
//
// A file in a folder shared with the guest is opened where it is, so
// saving writes the host's file. One in no shared folder cannot be seen by
// Windows, and the person is asked (OpenDocumentDialog) between sharing
// the folder, which takes a restart of Windows, and opening a copy, which
// stays in Windows. The launch itself, with the boot wait it may need,
// belongs to GuestAppLaunchController.
class DocumentOpener : public QObject {
    Q_OBJECT
public:
    // Restarts the guest on the settings' definition: the manager window's
    // (MainWindow::restart_guest).
    using Restarter = std::function<RestartResult(QString* reason)>;

    DocumentOpener(services::HostServices& services, GuestAppLaunchController& controller,
                   QObject* parent = nullptr);

    void set_restarter(Restarter restarter) { restarter_ = std::move(restarter); }

    // ``files`` are absolute host paths. ``app_id`` is the application to
    // open them with; empty for Windows' own program for each type.
    void open(const QString& app_id, const QStringList& files, const QString& activation_token);

private:
    // What a worker works out: the files' real paths, the machine's shares
    // as it has them now (the live definition while it runs, the stored one
    // otherwise), and each file's guest path where it has one.
    struct Plan {
        QStringList mapped;    // guest paths
        QStringList unmapped;  // host paths Windows cannot see
        QVector<vm::DomainDescription::Share> shares;
    };

    void proceed(const QString& app_id, const Plan& plan, const QString& activation_token);
    void ask(const QString& app_id, const Plan& plan, const QString& activation_token);
    // Shares what the person chose to share, and returns the shares the
    // machine will have; ``gained`` is how many of ``files`` became reachable.
    [[nodiscard]] QStringList share_and_map(const QStringList& shareable, const Plan& plan,
                                            QStringList* uploads);

    services::HostServices& services_;
    GuestAppLaunchController& controller_;
    Restarter restarter_;
};

}  // namespace lsw::host::manager
