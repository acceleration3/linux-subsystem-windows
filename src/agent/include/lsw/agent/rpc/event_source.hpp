#pragma once

#include "lsw/agent/rpc/event.hpp"

#include <cstddef>
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

    // The connection died partway through delivering the batch that
    // ``collect_pending`` just returned: events from ``first_undelivered``
    // onwards never reached the manager.
    //
    // Most sources let them go (a missed progress tick is not worth
    // keeping). Sources with terminal events that a peer is *waiting* for
    // — the installer's ``setup_done``, which the wizard blocks on —
    // override this to put them back for the next connection.
    virtual void undeliverable(std::size_t first_undelivered) {
        (void)first_undelivered;
    }
};

}  // namespace lsw::agent::rpc
