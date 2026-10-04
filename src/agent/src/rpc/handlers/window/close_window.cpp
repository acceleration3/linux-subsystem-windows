#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/win/window.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

struct CloseWindowHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        // No refusal for a missing handle: closing nothing is a plain
        // ``ok: false``.
        const auto hwnd = Params{params, linkproto::cmd::kCloseWindow}.hwnd();
        return {{"ok", hwnd != 0 && win::Window::close_window(hwnd)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::CloseWindowHandler, ::lsw::linkproto::cmd::kCloseWindow)
