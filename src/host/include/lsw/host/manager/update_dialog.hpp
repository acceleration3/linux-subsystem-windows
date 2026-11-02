#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

// A run of LSW's update of Windows (services::GuestUpdate), as StatusHub's
// update snapshot tells it: which versions, which of the host's steps the
// run is on, and when it ends how it went, with the guest's own steps.
//
// It opens when a run starts, whoever asked for it (GuestUpkeep at boot, or
// the Settings button), and not for a check that finds Windows current,
// which never runs. While the run goes it has no way out: the agent is
// being replaced, so nothing in the app can use Windows, and a window that
// could be dismissed would suggest otherwise. Close at the end does not
// bring it back until the next run.
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    UpdateDialog(services::StatusHub& status, QWidget* parent = nullptr);

Q_SIGNALS:
    // "Try again" after a failed run.
    void retry_requested();

protected:
    // Escape and the window's close do nothing while a run is going.
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private:
    void refresh();
    void rebuild_host_steps();
    void rebuild_guest_steps();

    services::StatusHub& status_;
    bool was_running_ = false;
    // Where the run has got to in the host's steps, kept for the failure
    // that has to say which one it stopped on. -1 before the first.
    int reached_ = -1;
    // Restarting Windows is only a step of runs that restart.
    bool restart_seen_ = false;

    QLabel* line_ = nullptr;
    QWidget* host_steps_ = nullptr;
    QWidget* guest_box_ = nullptr;
    QWidget* guest_steps_ = nullptr;
    QLabel* failure_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* retry_ = nullptr;
    QPushButton* close_ = nullptr;
};

}  // namespace lsw::host::manager
