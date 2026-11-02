#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/win/window.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {
struct WindowCommandHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ) override {
        const Params p{params, linkproto::cmd::kWindowCommand};
        const auto hwnd = p.require_hwnd();
        const auto command = p.get("command", std::string{});
        const bool ok = win::Window::system_command(hwnd, command);
        nlohmann::json r = {{"ok", ok}};
        if (!ok) {
            r["error"] = "unknown or unsupported command: " + command;
        }
        return r;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::string{"ok="} + (reply.value("ok", false) ? "true" : "false");
    }

    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::WindowCommandHandler,
                 ::lsw::linkproto::cmd::kWindowCommand)
