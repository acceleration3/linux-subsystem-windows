#include "lsw/agent/capture/wgc.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/rpc/session.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

struct UnsubscribeWindowHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        ctx.session.subscribed_windows.erase(hwnd);
        capture::Wgc::release(static_cast<std::uintptr_t>(hwnd));
        return {{"ok", true}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UnsubscribeWindowHandler, ::lsw::linkproto::cmd::kUnsubscribeWindow)
