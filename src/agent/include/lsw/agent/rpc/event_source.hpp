#pragma once

#include "lsw/agent/rpc/event.hpp"

#include <vector>

namespace lsw::agent::rpc {

// A push-event source — anything inside the agent that has events to
// deliver to the connected manager on each transport tick implements
// this. Implementations today: ``install::Installer``,
// ``console::Console``, the log adapter, and (Phase 9) ``uia::Uia``.
//
// ``collect_pending`` is expected to be cheap and non-blocking: it
// drains an internal queue protected by the source's own mutex and
// returns immediately. The transport calls every registered source
// on its 33 ms tick.
//
// NOTE on multi-session: today only one manager connects at a time, so
// "drain into a single consumer" is fine. The interface is single-
// consumer — sources that hold globally-queued events (Installer,
// Console) are not safe to drain from multiple connections in
// parallel. Multi-session fanout is a future change.
class EventSource {
public:
    virtual ~EventSource() = default;
    [[nodiscard]] virtual std::vector<Event> collect_pending() = 0;
};

}  // namespace lsw::agent::rpc
