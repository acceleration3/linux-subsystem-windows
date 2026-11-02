#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <optional>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace lsw::host::manager {
class LaunchSplash : public QWidget {
    Q_OBJECT
public:
    enum class Phase : std::uint8_t {
        MachineStarted,
        GuestAnswering,
        ApplicationRunning,
        Count,
    };

    LaunchSplash(QString application, QIcon icon, std::optional<qint64> last_boot_ms,
                 QWidget* parent = nullptr);

    void reach(Phase phase);
    void stall(const QString& reason);
    void resume();
    void fail(const QString& reason);
    void say(const QString& what);

Q_SIGNALS:
    void cancelled();
    void log_requested();
    void restart_requested();
    void retry_requested();

private:
    void tick();
    void show_phases();

    QString application_;
    int expected_seconds_ = 0;
    QElapsedTimer clock_;
    QTimer ticker_;
    int reached_ = -1;

    QLabel* heading_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QWidget* progress_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QLabel* timing_ = nullptr;
    QLabel* steps_[static_cast<int>(Phase::Count)]{};
    QWidget* footer_ = nullptr;
    QWidget* stalled_footer_ = nullptr;
    QWidget* failed_footer_ = nullptr;
    QLabel* stall_text_ = nullptr;
};

}
