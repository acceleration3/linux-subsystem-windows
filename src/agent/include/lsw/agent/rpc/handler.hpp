#pragma once

#include "lsw/agent/rpc/context.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace lsw::agent::rpc {

// Base for every RPC handler. One subclass per command, one TU per
// subclass, registered at static-init via ``LSW_REGISTER_RPC``.
//
// Handlers are instantiated ONCE at process startup and called from
// every connection's dispatch thread (and, in a session helper, from its
// worker threads) — they must be safe to call concurrently. Any
// per-command persistent state belongs on the relevant service
// (``Installer``, ``Console``, ``Uia``), not the handler.
//
// Throwing is the error path: handlers throw ``std::invalid_argument``
// for bad params, ``std::runtime_error`` for runtime failures. The
// transport layer catches ``std::exception`` and converts to a JSON
// ``{"error": "..."}`` reply.
class RpcHandler {
public:
    // Which process runs the command. The agent is a LocalSystem service
    // in session 0, and a few things only work from inside the user's
    // own session: enumerating and messaging its windows (window handles
    // do not cross sessions), UI Automation, and anything written to
    // HKCU. Those commands are served by the session helper the service
    // keeps in each user session; the service forwards them there.
    enum class Placement {
        Service,      // runs in the service, whatever session it concerns
        UserSession,  // forwarded to the helper in the target session
    };

    virtual ~RpcHandler() = default;
    [[nodiscard]] virtual nlohmann::json handle(const nlohmann::json& params,
                                                 RpcContext& ctx) = 0;

    // True for commands that fire so often that a REQ/REP line each
    // would churn the bounded log ring into uselessness — input
    // injection at mouse-move rate, window enumeration at the watcher's
    // poll rate. The handler decides, because the handler is what knows
    // its own call frequency; the transport used to keep a hardcoded
    // list of command names for this, which meant adding an RPC could
    // require editing the transport.
    [[nodiscard]] virtual bool silent() const { return false; }

    // One line describing a successful reply, for the REP log entry.
    // The default reports the payload size; handlers with something
    // more useful to say (how many apps, whether the icon came back
    // empty, the pid that was launched) override it.
    [[nodiscard]] virtual std::string summarize(const nlohmann::json& reply) const {
        return std::to_string(reply.dump().size()) + "B json";
    }

    // Where ``handle`` runs. Declared by the handler, like ``silent``,
    // so the dispatcher knows no command names.
    [[nodiscard]] virtual Placement placement() const { return Placement::Service; }

    // For a ``UserSession`` command: adjust the helper's reply in the
    // service before it goes to the manager, with what only the service
    // knows (which launch a process belongs to, say). Default: nothing.
    virtual void complete_in_service(nlohmann::json& reply) const { (void)reply; }
};

}  // namespace lsw::agent::rpc
