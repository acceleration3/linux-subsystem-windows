#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

struct ListWindowsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& /*params*/,
                          RpcContext& /*ctx*/) override {
        const auto wins = win::Window::enumerate();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& w : wins) {
            arr.push_back({
                {"hwnd", static_cast<std::uint64_t>(w.hwnd)},
                {"owner", static_cast<std::uint64_t>(w.owner)},
                {"pid", static_cast<std::uint64_t>(w.pid)},
                {"title", w.title},
                {"exe_path", w.exe_path},
                {"client_w", w.client_w},
                {"client_h", w.client_h},
                {"screen_x", w.screen_x},
                {"screen_y", w.screen_y},
            });
        }
        return {{"windows", std::move(arr)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListWindowsHandler, "list_windows")
