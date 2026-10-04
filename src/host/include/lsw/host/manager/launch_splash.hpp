#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace lsw::host::manager {

// What you get when you click a Windows application in your launcher
// and the guest is not running.
//
// For most people this is the only LSW window they see on a normal day,
// so it names the application, shows the phases by name, and says how
// long a cold start usually takes. A single spinner would say nothing
// about whether the wait is progressing or stuck, and the slow part
// moves around.
//
// It replaces a QMessageBox that said "Starting Windows…" and offered
// Cancel.
class LaunchSplash : public QWidget {
    Q_OBJECT
public:
    // Named rather than numbered, because a phase that has no signal
    // behind it would be a lie and these are exactly the ones that do.
    enum class Phase : std::uint8_t {
        MachineStarted,
        GuestAnswering,
        ApplicationRunning,
        Count,
    };

    LaunchSplash(QString application, QIcon icon, QWidget* parent = nullptr);

    // Mark everything up to and including ``phase`` as done.
    void reach(Phase phase);
    // The guest has been silent long enough that something is wrong.
    void stall(const QString& reason);

Q_SIGNALS:
    void cancelled();
    void log_requested();
    void restart_requested();

private:
    void tick();

    QString application_;
    QElapsedTimer clock_;
    QTimer ticker_;
    int reached_ = -1;

    QLabel* subtitle_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QLabel* timing_ = nullptr;
    QLabel* steps_[static_cast<int>(Phase::Count)]{};
    QWidget* footer_ = nullptr;
    QWidget* stalled_footer_ = nullptr;
    QLabel* stall_text_ = nullptr;
};

}  // namespace lsw::host::manager
