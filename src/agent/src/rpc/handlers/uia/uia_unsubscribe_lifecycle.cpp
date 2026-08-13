#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

struct UiaUnsubscribeLifecycleHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto id = params.value("sub_id", std::uint64_t{0});
        ctx.services.uia().unsubscribe(id);
        return {{"ok", true}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaUnsubscribeLifecycleHandler, ::lsw::linkproto::cmd::kUiaUnsubscribeLifecycle)
