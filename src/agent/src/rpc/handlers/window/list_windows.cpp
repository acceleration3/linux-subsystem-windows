#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>
#include <string>

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

    // Polled by every window watcher at ~45 Hz; a REQ/REP pair each
    // would leave room for nothing else in the log ring.
    [[nodiscard]] bool silent() const override { return true; }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("windows", nlohmann::json::array()).size())
               + " windows";
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListWindowsHandler, ::lsw::linkproto::cmd::kListWindows)
