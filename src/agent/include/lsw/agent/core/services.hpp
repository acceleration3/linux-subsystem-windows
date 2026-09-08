#pragma once

#include "lsw/agent/console/console.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/agent/win/com.hpp"

namespace lsw::agent::core {

// Container for the agent's long-lived stateful services. Owned by
// ``AgentService`` for the process lifetime; passed by reference into
// the transport layer and into every handler via ``RpcContext``.
//
// Member ORDER is load-bearing:
//   1. ``apartment_`` — initialises COM MTA on the main thread.
//   2. ``uia_``       — creates ``IUIAutomation`` (needs COM live).
// Destructors run in reverse, but we don't rely on that: ``shutdown()``
// explicitly releases UIA's COM refs BEFORE the apartment goes away
// (called from ``AgentService::run`` before the destructor chain
// fires at static-destruction time, which is too late once the COM
// runtime has already been torn down by other code).
//
// ``Installer`` and ``Console`` are plain members: they have no COM
// dependency, so they need none of the ordering dance above. They used
// to be singletons; they are not, because a handler that reaches for a
// global is a handler that cannot be tested.
class AgentServices {
public:
    AgentServices() = default;
    AgentServices(const AgentServices&) = delete;
    AgentServices& operator=(const AgentServices&) = delete;

    [[nodiscard]] install::Installer& installer() noexcept { return installer_; }
    [[nodiscard]] console::Console& console() noexcept { return console_; }
    [[nodiscard]] uia::Uia& uia() noexcept { return uia_; }

    // Explicit pre-destructor teardown. Called from
    // ``AgentService::run`` before its scope ends. Releases COM-held
    // objects while ``apartment_`` is still live so the destructor
    // chain doesn't race the COM runtime teardown. The
    // destructor handles its own teardown.
    void shutdown() { uia_.shutdown(); }

private:
    install::Installer installer_;
    console::Console console_;
    win::Apartment apartment_;  // must initialise BEFORE uia_
    uia::Uia uia_;
};

}  // namespace lsw::agent::core
