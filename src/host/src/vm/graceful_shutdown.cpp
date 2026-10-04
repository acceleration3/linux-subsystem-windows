#include "lsw/host/vm/graceful_shutdown.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/vm/lifecycle.hpp"

namespace lsw::host::vm {

GracefulShutdown::GracefulShutdown(Lifecycle& lifecycle, QUuid uuid, qint64 deadline_ms,
                                   QObject* parent)
    : QObject(parent), lifecycle_(lifecycle), uuid_(uuid), deadline_ms_(deadline_ms) {
    timer_.setInterval(kPollMs);
    connect(&timer_, &QTimer::timeout, this, &GracefulShutdown::poll);
}

void GracefulShutdown::start() {
    clock_.start();
    ticks_ = 0;
    forced_ = false;
    if (lifecycle_.is_running(uuid_) && lifecycle_.shutdown(uuid_) != 0) {
        // The poll presses again and, at the deadline, pulls the plug;
        // one refused request is not the end of it.
        LSW_WARN("shutdown: the first request was refused; retrying");
    }
    timer_.start();
    QTimer::singleShot(0, this, &GracefulShutdown::poll);
}

void GracefulShutdown::poll() {
    if (!timer_.isActive()) {
        return;
    }
    if (!lifecycle_.is_running(uuid_)) {
        timer_.stop();
        Q_EMIT finished(forced_);
        return;
    }
    if (clock_.elapsed() >= deadline_ms_) {
        LSW_WARN("shutdown: the guest did not stop within {} s; forcing it off",
                 deadline_ms_ / 1000);
        forced_ = true;
        (void)lifecycle_.force_off(uuid_);
        return;  // the next tick sees it stopped
    }
    if (++ticks_ % kRepressTicks == 0) {
        (void)lifecycle_.shutdown(uuid_);
    }
}

}  // namespace lsw::host::vm
