#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/apps/launch_group.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

struct ListWindowsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& ,
                          RpcContext& ) override {
        const auto wins = win::Window::enumerate();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& w : wins) {
            arr.push_back({
                {"hwnd", static_cast<std::uint64_t>(w.hwnd)},
                {"owner", static_cast<std::uint64_t>(w.owner)},
                {"pid", static_cast<std::uint64_t>(w.pid)},
                {"root_pid", std::uint64_t{0}},
                {"title", w.title},
                {"exe_path", w.exe_path},
                {"client_w", w.client_w},
                {"client_h", w.client_h},
                {"screen_x", w.screen_x},
                {"screen_y", w.screen_y},
                {"minimized", w.minimized},
                {"maximized", w.maximized},
                {"has_caption", w.has_caption},
            });
        }
        return {{"windows", std::move(arr)}};
    }

    void complete_in_service(nlohmann::json& reply) const override {
        auto it = reply.find("windows");
        if (it == reply.end() || !it->is_array()) {
            return;
        }
        for (auto& w : *it) {
            const auto pid = static_cast<unsigned long>(w.value("pid", std::uint64_t{0}));
            w["root_pid"] = static_cast<std::uint64_t>(apps::LaunchGroup::root_of(pid));
        }
    }

    [[nodiscard]] log::Level request_level() const override { return log::Level::Trace; }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("windows", nlohmann::json::array()).size())
               + " windows";
    }

    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListWindowsHandler, ::lsw::linkproto::cmd::kListWindows)
