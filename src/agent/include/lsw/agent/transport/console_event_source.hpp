#pragma once

#include "lsw/agent/rpc/event_source.hpp"

namespace lsw::agent::console {
class Console;
}

namespace lsw::agent::transport {
class ConsoleEventSource : public rpc::EventSource {
public:
    explicit ConsoleEventSource(console::Console& console) : console_(&console) {}

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    console::Console* console_;
};

}
