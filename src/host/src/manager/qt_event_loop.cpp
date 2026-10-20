#ifdef LSW_HAVE_REMOTEWIN
#include "lsw/host/manager/qt_event_loop.hpp"

#include <QSocketNotifier>
#include <QTimer>

#include <poll.h>

#include <chrono>
#include <utility>

namespace lsw::host::manager {

namespace {

class QtWatch final : public rwin::host::EventLoop::Watch {
public:
    QtWatch(int fd, short events, rwin::host::EventLoop::Ready ready)
        : ready_(std::move(ready)), reading_(fd, QSocketNotifier::Read), writing_(fd, QSocketNotifier::Write) {
        QObject::connect(&reading_, &QSocketNotifier::activated, [this] { ready_(POLLIN); });
        QObject::connect(&writing_, &QSocketNotifier::activated, [this] { ready_(POLLOUT); });
        want(events);
    }

    void want(short events) override {
        reading_.setEnabled((events & POLLIN) != 0);
        writing_.setEnabled((events & POLLOUT) != 0);
    }

private:
    rwin::host::EventLoop::Ready ready_;
    QSocketNotifier reading_;
    QSocketNotifier writing_;
};

class QtTimer final : public rwin::host::EventLoop::Timer {
public:
    explicit QtTimer(std::function<void()> fired) : fired_(std::move(fired)) {
        timer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer_, &QTimer::timeout, [this] { fired_(); });
    }

    void start(std::chrono::milliseconds after, bool repeat) override {
        timer_.setSingleShot(!repeat);
        timer_.start(after);
    }
    void stop() override { timer_.stop(); }
    [[nodiscard]] bool active() const override { return timer_.isActive(); }

private:
    std::function<void()> fired_;
    QTimer timer_;
};

}  // namespace

std::unique_ptr<rwin::host::EventLoop::Watch> QtEventLoop::watch(int fd, short events, Ready ready) {
    return std::make_unique<QtWatch>(fd, events, std::move(ready));
}

std::unique_ptr<rwin::host::EventLoop::Timer> QtEventLoop::timer(std::function<void()> fired) {
    return std::make_unique<QtTimer>(std::move(fired));
}

}  // namespace lsw::host::manager
#endif
