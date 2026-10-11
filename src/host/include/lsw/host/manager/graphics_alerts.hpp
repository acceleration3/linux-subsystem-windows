#pragma once

#include "lsw/host/manager/guest_driver_installer.hpp"

#include <QObject>
#include <QString>
#include <QVector>

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

// What is wrong with the guest's graphics, worded, from the status hub's
// snapshot — plus what the user can do about it from here: install the
// driver for adapters Windows has none for.
//
// The Graphics page shows every warning; the overview the two that break
// the guest; the rail a dot in the worst one's colour. One list, so the
// three agree.
class GraphicsAlerts : public QObject {
    Q_OBJECT
public:
    struct Warning {
        enum class Kind {
            NoDirect3d,
            DisplayNotRunning,
            // The display adapter with no driver at all (problem code 28):
            // the machine's GPU was switched since the install, and the
            // adapter wears the new GPU's PCI ID.
            DisplayNoDriver,
            NoDriver,
            Installing,
            InstallFinished,
            InstallFailed,
        };
        Kind kind = Kind::NoDirect3d;
        bool critical = false;
        QString headline;
        QString detail;
    };

    enum class Severity { None, Amber, Red };

    GraphicsAlerts(services::StatusHub& status, link::AgentRpcClient& rpc,
                   QObject* parent = nullptr);

    [[nodiscard]] QVector<Warning> warnings() const;
    [[nodiscard]] Severity worst() const;

    void install_driver();

Q_SIGNALS:
    void changed();

private:
    services::StatusHub& status_;
    GuestDriverInstaller installer_;
    QString install_progress_;
    enum class InstallResult { None, Done, DoneRestart, Failed };
    InstallResult install_result_ = InstallResult::None;
    QString install_error_;
    // The machine as the last reading had it, to act on its transitions.
    bool running_ = false;
    bool agent_online_ = false;
};

}  // namespace lsw::host::manager
