#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

struct ResizeWindowHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        const auto w = params.value("width", 0);
        const auto h = params.value("height", 0);
        return {{"ok", hwnd != 0
                          && win::Window::resize_client(
                              static_cast<std::uintptr_t>(hwnd), w, h)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ResizeWindowHandler, ::lsw::linkproto::cmd::kResizeWindow)
