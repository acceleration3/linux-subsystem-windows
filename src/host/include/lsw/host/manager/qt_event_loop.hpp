#pragma once

#include "host/loop/event_loop.hpp"

#include <functional>
#include <memory>

namespace lsw::host::manager {
class QtEventLoop final : public rwin::host::EventLoop {
public:
    [[nodiscard]] std::unique_ptr<Watch> watch(int fd, short events, Ready ready) override;
    [[nodiscard]] std::unique_ptr<Timer> timer(std::function<void()> fired) override;
};

}
