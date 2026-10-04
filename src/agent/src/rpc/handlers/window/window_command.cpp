#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/win/window.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

// Minimize / maximize / restore a window on the host's behalf.
//
// Sent when the host's own UI drives the guest window — its taskbar
// entry, a keyboard shortcut, the user minimizing the mirror. The
// reverse direction (the user clicking the buttons the application
// itself draws, inside the mirror) needs none of this: that click is
// forwarded as an ordinary click and the application acts on it, and
// the host learns the outcome from the show state on the next window
// enumeration.
struct WindowCommandHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
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

    // Window handles, UI Automation and HKCU belong to the user's
    // session: served by the session helper there.
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::WindowCommandHandler,
                 ::lsw::linkproto::cmd::kWindowCommand)
