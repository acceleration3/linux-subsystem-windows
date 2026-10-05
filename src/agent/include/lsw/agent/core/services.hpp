#pragma once

#include "lsw/agent/apps/upload_store.hpp"
#include "lsw/agent/console/console.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/win/com.hpp"

#include <memory>

namespace lsw::agent::uia {
class Uia;
}

namespace lsw::agent::session {
class SessionManager;
}

namespace lsw::agent::core {

// Container for the agent's long-lived stateful services. Owned by the
// process for its lifetime; passed by reference into the transport
// layer and into every handler via ``RpcContext``.
//
// The same executable runs in two roles, and each owns only its half:
//   * Service — the LocalSystem service in session 0: installer jobs,
//     consoles, and the SessionManager that keeps a helper in each user
//     session and routes session-bound commands to it;
//   * SessionHelper — one per user session: UI Automation, whose queries
//     and event subscriptions only work from inside the session.
// Asking a role for the other's service is a coding error (a handler
// whose Placement is wrong) and throws std::logic_error.
//
// Member ORDER is load-bearing:
//   1. ``apartment_`` — initialises COM MTA on the constructing thread.
//   2. ``uia_``       — creates ``IUIAutomation`` (needs COM live).
// ``shutdown()`` releases UIA's COM refs BEFORE the apartment goes away.
class AgentServices {
public:
    enum class Role { Service, SessionHelper };

    explicit AgentServices(Role role);
    ~AgentServices();

    AgentServices(const AgentServices&) = delete;
    AgentServices& operator=(const AgentServices&) = delete;

    [[nodiscard]] Role role() const noexcept { return role_; }

    [[nodiscard]] install::Installer& installer() noexcept { return installer_; }

    [[nodiscard]] console::Console& console() noexcept { return console_; }
    [[nodiscard]] apps::UploadStore& uploads() noexcept { return uploads_; }

    // SessionHelper only.
    [[nodiscard]] uia::Uia& uia();

    // Service only.
    [[nodiscard]] session::SessionManager& sessions();

    // Attach the session manager (Service role), which the composition
    // root constructs because it alone knows whether it is a service.
    void attach_sessions(std::unique_ptr<session::SessionManager> sessions);

    // Explicit pre-destructor teardown: stops the session helpers, and
    // releases COM-held objects while ``apartment_`` is still live.
    void shutdown();

private:
    Role role_;
    install::Installer installer_;
    console::Console console_;
    apps::UploadStore uploads_;
    win::Apartment apartment_;  // must initialise BEFORE uia_
    std::unique_ptr<uia::Uia> uia_;
    std::unique_ptr<session::SessionManager> sessions_;
};

}  // namespace lsw::agent::core
