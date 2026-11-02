#pragma once

#include "lsw/agent/rpc/event.hpp"
#include "lsw/agent/rpc/event_source.hpp"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace lsw::agent::uia {
class UiaEventQueue final : public rpc::EventSource {
public:
    static constexpr std::size_t kCapacity = 4096;

    void post(rpc::Event event);

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    std::mutex mu_;
    std::deque<rpc::Event> pending_;
};

}
