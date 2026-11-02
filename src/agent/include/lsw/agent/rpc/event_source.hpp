#pragma once

#include "lsw/agent/rpc/event.hpp"

#include <cstddef>
#include <vector>

namespace lsw::agent::rpc {
class EventSource {
public:
    virtual ~EventSource() = default;
    [[nodiscard]] virtual std::vector<Event> collect_pending() = 0;

    virtual void undeliverable(std::size_t first_undelivered) {
        (void)first_undelivered;
    }
};

}
