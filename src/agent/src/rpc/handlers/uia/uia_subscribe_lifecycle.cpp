#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/rpc/session.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

// ``uia_subscribe_lifecycle``: watch ``pid`` for window-opened /
// window-closed events. Reply carries the subscription id the host
// later passes to ``uia_unsubscribe_lifecycle``. The agent's
// per-session ``Session`` doesn't yet auto-clean these on disconnect
// (Phase 9 first cut); the host is responsible for unsubscribing
// when it tears down its mirrors. Multi-session fanout is the
// out-of-scope item from the plan.
struct UiaSubscribeLifecycleHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto pid = params.value("pid", std::uint64_t{0});
        if (pid == 0) {
            throw std::invalid_argument("uia_subscribe_lifecycle: missing pid");
        }
        const auto id = ctx.services.uia().subscribe_pid_lifecycle(
            static_cast<std::uint32_t>(pid));
        return {{"ok", id != 0}, {"sub_id", id}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaSubscribeLifecycleHandler, ::lsw::linkproto::cmd::kUiaSubscribeLifecycle)
