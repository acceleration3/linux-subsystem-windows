#pragma once

#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

// Generic unsubscribe: works for any sub_id returned by
// ``uia_subscribe_lifecycle`` / ``_focus`` / ``_properties``.
//
// In a header because two commands are served by it —
// ``uia_unsubscribe`` and the older ``uia_unsubscribe_lifecycle``, kept
// for the host code wired in before the generic one existed — and the
// one-registration-per-TU rule gives each its own TU.
struct UiaUnsubscribeHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const auto id = params.value("sub_id", std::uint64_t{0});
        ctx.services.uia().unsubscribe(id);
        return {{"ok", true}};
    }
};

}  // namespace lsw::agent::rpc::handlers
