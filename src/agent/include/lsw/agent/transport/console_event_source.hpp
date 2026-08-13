#pragma once

#include "lsw/agent/rpc/event_source.hpp"

namespace lsw::agent::console {
class Console;
}

namespace lsw::agent::transport {

// Publishes console session output/exit as ``console_output`` /
// ``console_closed`` events.
//
// An adapter rather than a base class on ``Console`` itself: the console
// deals in raw bytes and session ids, and the fact that those reach the
// manager as base64 inside a JSON envelope is a transport decision. This
// is where that decision lives.
class ConsoleEventSource : public rpc::EventSource {
public:
    explicit ConsoleEventSource(console::Console& console) : console_(&console) {}

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    console::Console* console_;
};

}  // namespace lsw::agent::transport
