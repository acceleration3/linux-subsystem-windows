#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

struct WindowPropsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("window_props: missing hwnd");
        }
        const auto p =
            win::Window::query_props(static_cast<std::uintptr_t>(hwnd));
        // No caption-button rects. They used to come from UIA, and the
        // host used them to catch a click on minimize / maximize /
        // close before forwarding it — which required the application
        // to expose its title bar as UIA elements with truthful bounds.
        // Photoshop reports its buttons 4 pixels tall; a WinUI app that
        // hit-tests its title bar in XAML reports nothing useful
        // either. So the host no longer tries to recognise the press:
        // it forwards it, the application's own button handles it, and
        // the resulting show state arrives with the next enumeration.
        return {
            {"valid", p.valid},
            {"x", p.x}, {"y", p.y}, {"w", p.w}, {"h", p.h},
            {"resizable", p.resizable},
            {"minimizable", p.minimizable},
            {"maximizable", p.maximizable},
            {"closable", p.closable},
            {"min_w", p.min_w}, {"min_h", p.min_h},
            {"max_w", p.max_w}, {"max_h", p.max_h},
            {"screen_w", p.screen_w}, {"screen_h", p.screen_h},
        };
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::WindowPropsHandler, ::lsw::linkproto::cmd::kWindowProps)
