#include "lsw/host/manager/manager_exit.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"

#include <utility>

namespace lsw::host::manager {

namespace {

using K = config::ManagerSettings::Key;

}

ManagerExit::ManagerExit(config::ManagerSettings& settings, Host host, QObject* parent,
                         std::chrono::milliseconds guest_wait, std::chrono::milliseconds guest_poll)
    : QObject(parent), settings_(settings), host_(std::move(host)), guest_wait_(guest_wait) {
    poll_.setInterval(guest_poll);
    connect(&poll_, &QTimer::timeout, this, &ManagerExit::poll);
    connect(&settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kCloseToTray && !leaving()) {
            Q_EMIT tray_wanted(stays_in_tray());
        }
    });
}

bool ManagerExit::stays_in_tray() const {
    return settings_.bool_value(K::kCloseToTray) && host_.tray_available();
}

void ManagerExit::window_closed() {
    if (leaving()) {
        return;
    }
    if (stays_in_tray()) {
        Q_EMIT hide_requested();
        return;
    }
    quit();
}

void ManagerExit::quit() {
    if (leaving()) {
        return;
    }
    if (!settings_.bool_value(K::kShutGuestOnQuit) || !host_.guest_running()) {
        finish();
        return;
    }
    LSW_INFO("quit: asking the guest to shut down, waiting up to {} s for it",
             guest_wait_.count() / 1000);
    state_ = State::WaitingForGuest;
    Q_EMIT left();
    host_.shut_guest_down();
    clock_.start();
    poll_.start();
}

void ManagerExit::hand_over() {
    if (leaving()) {
        return;
    }
    state_ = State::Finished;
    Q_EMIT left();
}

void ManagerExit::poll() {
    if (!host_.guest_running()) {
        finish();
        return;
    }
    if (clock_.elapsed() >= guest_wait_.count()) {
        LSW_WARN("quit: the guest is still shutting down after {} s; leaving it to finish",
                 guest_wait_.count() / 1000);
        finish();
    }
}

void ManagerExit::finish() {
    poll_.stop();
    const bool announced = state_ == State::WaitingForGuest;
    state_ = State::Finished;
    if (!announced) {
        Q_EMIT left();
    }
    Q_EMIT quit_requested();
}

}  // namespace lsw::host::manager
