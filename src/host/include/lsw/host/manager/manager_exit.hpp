#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <functional>

namespace lsw::host::config { class ManagerSettings; }

namespace lsw::host::manager {

class ManagerExit : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<bool()> tray_available;
        std::function<bool()> guest_running;
        std::function<void()> shut_guest_down;
    };

    static constexpr std::chrono::milliseconds kGuestWait{30000};
    static constexpr std::chrono::milliseconds kGuestPoll{500};

    ManagerExit(config::ManagerSettings& settings, Host host, QObject* parent = nullptr,
                std::chrono::milliseconds guest_wait = kGuestWait,
                std::chrono::milliseconds guest_poll = kGuestPoll);

    [[nodiscard]] bool stays_in_tray() const;
    [[nodiscard]] bool leaving() const noexcept { return state_ != State::Running; }
    [[nodiscard]] bool finished() const noexcept { return state_ == State::Finished; }

    void window_closed();
    void quit();

Q_SIGNALS:
    void tray_wanted(bool wanted);
    void hide_requested();
    void left();
    void quit_requested();

private:
    enum class State { Running, WaitingForGuest, Finished };

    void poll();
    void finish();

    config::ManagerSettings& settings_;
    Host host_;
    std::chrono::milliseconds guest_wait_;
    QTimer poll_;
    QElapsedTimer clock_;
    State state_ = State::Running;
};

}
