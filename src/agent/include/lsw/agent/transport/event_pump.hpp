#pragma once

#include "lsw/agent/rpc/event_source.hpp"

#include <functional>
#include <vector>

namespace lsw::agent::transport {

// Drains every registered ``rpc::EventSource`` onto the connection.
//
// The connection loop used to hold one bespoke lambda per source — logs,
// installer, console, UIA, move/resize — each with its own copy of
// "collect, wrap as JSON, send, count". Registering a source here means
// a new kind of agent-pushed event costs one ``add`` call and no change
// to the loop at all, which is the same bargain ``LSW_REGISTER_RPC``
// makes for commands.
class EventPump {
public:
    // Sends one event; returns false once the connection is dead.
    using Sender = std::function<bool(const rpc::Event&)>;

    // Sources are borrowed, not owned — they outlive the connection
    // (they hang off AgentServices) or are members of the caller.
    void add(rpc::EventSource& source) { sources_.push_back(&source); }

    // Drain every source in registration order. Returns false as soon as
    // a send fails, having told the source in question which of its
    // events never made it out.
    [[nodiscard]] bool drain(const Sender& send);

private:
    std::vector<rpc::EventSource*> sources_;
};

}  // namespace lsw::agent::transport
