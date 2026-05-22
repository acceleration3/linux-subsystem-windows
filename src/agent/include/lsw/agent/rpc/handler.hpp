#pragma once

#include "lsw/agent/rpc/context.hpp"

#include <nlohmann/json.hpp>

namespace lsw::agent::rpc {

// Base for every RPC handler. One subclass per command, one TU per
// subclass, registered at static-init via ``LSW_REGISTER_RPC``.
//
// Handlers are instantiated ONCE at process startup and called from
// the single dispatch thread of each connection — they must be safe
// to call repeatedly but can assume no concurrent reentry on a single
// instance. Any per-command persistent state belongs on the relevant
// service (``Installer``, ``Console``, ``Uia``), not the handler.
//
// Throwing is the error path: handlers throw ``std::invalid_argument``
// for bad params, ``std::runtime_error`` for runtime failures. The
// transport layer catches ``std::exception`` and converts to a JSON
// ``{"error": "..."}`` reply.
class RpcHandler {
public:
    virtual ~RpcHandler() = default;
    [[nodiscard]] virtual nlohmann::json handle(const nlohmann::json& params,
                                                 RpcContext& ctx) = 0;
};

}  // namespace lsw::agent::rpc
