#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/mirror_process.hpp"
#include "lsw/agent/win/run_as.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace lsw::agent::rpc::handlers {
struct StartWindowMirrorHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const auto port = params.value("port", std::uint32_t{0});
        if (port == 0 || port > 65535) {
            return {{"ok", false}, {"error", "port must be between 1 and 65535"}};
        }
        const auto exe =
            params.value("exe", util::TextEncoding::to_utf8(core::Paths::remotewin_guest_exe()));
        const auto log_path = params.value(
            "log_path", util::TextEncoding::to_utf8(params.value("desktop", false)
                                                        ? core::Paths::remotewin_desktop_log()
                                                        : core::Paths::remotewin_guest_log(
                                                              params.value("name", std::string{}))));

        std::optional<std::uint32_t> app_pid;
        if (const auto v = params.value("pid", std::uint64_t{0}); v != 0) {
            app_pid = static_cast<std::uint32_t>(v);
        }
        const win::RunAs as = ctx.services.sessions().run_as_beside(
            params, linkproto::cmd::kStartWindowMirror, app_pid);

        std::string error;
        const unsigned long pid =
            win::MirrorProcess::start(exe, static_cast<std::uint16_t>(port), log_path,
                                      ctx.services.mirror_log_level(), as, &error);
        nlohmann::json r = {
            {"ok", pid != 0}, {"pid", static_cast<std::uint64_t>(pid)}, {"session", as.session()}};
        if (pid == 0) {
            r["error"] = error;
        }
        return r;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        const auto error = reply.value("error", std::string{});
        return std::string{"ok="} + (reply.value("ok", false) ? "true" : "false") +
               " pid=" + std::to_string(reply.value("pid", std::uint64_t{0})) +
               (error.empty() ? "" : " err=" + error);
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::StartWindowMirrorHandler,
                 ::lsw::linkproto::cmd::kStartWindowMirror)
