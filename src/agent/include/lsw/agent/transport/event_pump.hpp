#pragma once

#include "lsw/agent/rpc/event_source.hpp"

#include <functional>
#include <vector>

namespace lsw::agent::transport {
class EventPump {
public:
    using Sender = std::function<bool(const rpc::Event&)>;

    void add(rpc::EventSource& source) { sources_.push_back(&source); }

    [[nodiscard]] bool drain(const Sender& send);

private:
    std::vector<rpc::EventSource*> sources_;
};

}
