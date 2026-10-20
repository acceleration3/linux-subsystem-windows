#pragma once

#include "host/loop/event_loop.hpp"

#include <functional>
#include <memory>

namespace lsw::host::manager {

// remotewin's guest link run on this process's Qt event loop: a watched
// descriptor is a QSocketNotifier, a timer a QTimer. What the manager
// embeds of remotewin (the desktop preview's link) waits on Qt's loop as
// everything else here does.
class QtEventLoop final : public rwin::host::EventLoop {
public:
    [[nodiscard]] std::unique_ptr<Watch> watch(int fd, short events, Ready ready) override;
    [[nodiscard]] std::unique_ptr<Timer> timer(std::function<void()> fired) override;
};

}  // namespace lsw::host::manager
