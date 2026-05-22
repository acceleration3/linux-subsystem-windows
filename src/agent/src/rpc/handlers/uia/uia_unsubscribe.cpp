#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

// Generic unsubscribe: works for any sub_id returned by
// ``uia_subscribe_lifecycle`` / ``_focus`` / ``_properties``.
// ``uia_unsubscribe_lifecycle`` is kept as an alias for backwards
// compatibility with the host code wired in earlier.
struct UiaUnsubscribeHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto id = params.value("sub_id", std::uint64_t{0});
        ctx.services.uia().unsubscribe(id);
        return {{"ok", true}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaUnsubscribeHandler,
                 "uia_unsubscribe")
