#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

struct WindowPropsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("window_props: missing hwnd");
        }
        const auto p =
            win::Window::query_props(static_cast<std::uintptr_t>(hwnd));
        // Title-bar rect + button rects come from UIA (Phase 7), not
        // WM_GETTITLEBARINFOEX. UIA reports rects in screen coords;
        // we translate to EFB-relative using ``p.x, p.y`` (EFB top-
        // left). For apps with no UIA TitleBar element (custom-chrome
        // / full-frame extension), ``layout.present`` is false and
        // every rect stays {0,0,0,0} — host treats as "no title bar
        // chrome to reroute, forward every click to the guest".
        const auto btns = ctx.services.uia().get_caption_buttons(
            static_cast<std::uintptr_t>(hwnd), p.x, p.y);
        // Buttons only. The title-bar / caption-drag region is resolved
        // per-press on the host via ``nchittest`` so tab strips, address
        // bars, etc. — which share the caption row but aren't caption —
        // don't get wrongly claimed as draggable.
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
            {"btn_min",   {btns.min_x, btns.min_y, btns.min_w, btns.min_h}},
            {"btn_max",   {btns.max_x, btns.max_y, btns.max_w, btns.max_h}},
            {"btn_close", {btns.close_x, btns.close_y,
                            btns.close_w, btns.close_h}},
            {"btn_help",  {btns.help_x, btns.help_y,
                            btns.help_w, btns.help_h}},
        };
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::WindowPropsHandler, "window_props")
