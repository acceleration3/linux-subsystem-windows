#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

struct UiaSubscribeFocusHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto pid = params.value("pid", std::uint64_t{0});
        if (pid == 0) {
            throw std::invalid_argument("uia_subscribe_focus: missing pid");
        }
        const auto id = ctx.services.uia().subscribe_pid_focus(
            static_cast<std::uint32_t>(pid));
        return {{"ok", id != 0}, {"sub_id", id}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaSubscribeFocusHandler, ::lsw::linkproto::cmd::kUiaSubscribeFocus)
