#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {
struct UiaSubscribeLifecycleHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const auto pid = Params{params, linkproto::cmd::kUiaSubscribeLifecycle}.require_pid();
        const auto id = ctx.services.uia().subscribe_pid_lifecycle(pid);
        return {{"ok", id != 0}, {"sub_id", id}};
    }

    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaSubscribeLifecycleHandler,
                 ::lsw::linkproto::cmd::kUiaSubscribeLifecycle)
