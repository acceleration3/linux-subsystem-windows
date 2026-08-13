#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/rpc/session.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

struct SubscribeWindowHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("subscribe_window: missing hwnd");
        }
        // Per-connection state — lives on the Session, not a global.
        ctx.session.subscribed_windows.insert(hwnd);
        return {{"ok", true}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SubscribeWindowHandler, ::lsw::linkproto::cmd::kSubscribeWindow)
