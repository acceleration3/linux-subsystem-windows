#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/win/input.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

struct InjectInputHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("inject_input: missing hwnd");
        }
        const auto type = params.value("type", std::string{});
        win::InputEvent ev;
        if      (type == "move")     ev.kind = win::InputEvent::Kind::MouseMove;
        else if (type == "down")     ev.kind = win::InputEvent::Kind::MouseDown;
        else if (type == "up")       ev.kind = win::InputEvent::Kind::MouseUp;
        else if (type == "dblclick") ev.kind = win::InputEvent::Kind::MouseDblClk;
        else if (type == "wheel")    ev.kind = win::InputEvent::Kind::Wheel;
        else if (type == "key_down") ev.kind = win::InputEvent::Kind::KeyDown;
        else if (type == "key_up")   ev.kind = win::InputEvent::Kind::KeyUp;
        else if (type == "char")     ev.kind = win::InputEvent::Kind::Char;
        else {
            throw std::invalid_argument("inject_input: bad type '" + type + "'");
        }
        ev.x = params.value("x", 0);
        ev.y = params.value("y", 0);
        ev.button = params.value("button", 0);
        ev.wheel = params.value("wheel", 0);
        ev.hwheel = params.value("hwheel", false);
        ev.vk = params.value("vk", 0u);
        ev.scan = params.value("scan", 0u);
        ev.codepoint = params.value("cp", 0u);
        ev.mods = params.value("mods", 0u);
        return {{"ok", win::Input::inject(static_cast<std::uintptr_t>(hwnd), ev)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::InjectInputHandler, "inject_input")
