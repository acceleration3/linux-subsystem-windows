#pragma once

namespace lsw::agent::core {
class AgentServices;
}

namespace lsw::agent::rpc {

// Per-connection state. Owned by ``handle_connection`` on its stack and
// destroyed when the manager disconnects.
//
// One Session per connection: the agent accepts one at a time, and this
// is what makes a second one a no-op at the dispatch layer rather than a
// rewrite — anything a connection owns for its own duration belongs
// here, not in a global.
class Session {
public:
    explicit Session(core::AgentServices& services) noexcept : services_(&services) {}

    [[nodiscard]] core::AgentServices& services() noexcept { return *services_; }

private:
    core::AgentServices* services_;
};

}  // namespace lsw::agent::rpc
