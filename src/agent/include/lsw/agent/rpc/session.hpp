#pragma once

#include <cstdint>
#include <unordered_set>

namespace lsw::agent::core {
class AgentServices;
}

namespace lsw::agent::rpc {

// Per-connection state. Owned by ``handle_connection`` on its stack;
// destroyed when the TCP client disconnects. Phase 1 carries only the
// data the existing dispatcher already had as a parameter
// (``subscribed_windows``); later phases add per-session UIA
// subscription handles + a push-event queue here.
//
// The intent is "one Session per manager TCP connection". The agent
// currently only accepts one connection at a time, but Session makes
// the multi-connection extension a no-op at the dispatch layer — each
// connection gets its own subscribed-windows set, its own UIA hooks,
// its own teardown.
class Session {
public:
    explicit Session(core::AgentServices& services) noexcept
        : services_(&services) {}

    [[nodiscard]] core::AgentServices& services() noexcept { return *services_; }

    // Windows the manager has asked us to push capture frames for.
    // Touched only by the dispatch thread of this connection, so no
    // locking required.
    std::unordered_set<std::uint64_t> subscribed_windows;

private:
    core::AgentServices* services_;
};

}  // namespace lsw::agent::rpc
