#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

struct UnfocusWindowHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        return {{"ok", hwnd != 0
                          && win::Window::unfocus_window(
                              static_cast<std::uintptr_t>(hwnd))}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UnfocusWindowHandler, ::lsw::linkproto::cmd::kUnfocusWindow)
