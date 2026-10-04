#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/mirror_process.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

// Start the guest half of the window mirror for one application.
//
// The manager already has a mirror process listening; this is the end
// that dials it. Nothing else about mirroring passes through the agent —
// the windows, their pixels and their input travel on the connection
// this starts, not on this channel.
struct StartWindowMirrorHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        const auto port = params.value("port", std::uint32_t{0});
        const auto exe =
            params.value("exe", util::TextEncoding::to_utf8(core::Paths::remotewin_guest_exe()));
        const auto log_path = params.value(
            "log_path", util::TextEncoding::to_utf8(core::Paths::remotewin_guest_log()));

        std::string error;
        const unsigned long pid =
            win::MirrorProcess::start(exe, static_cast<std::uint16_t>(port), log_path, &error);
        nlohmann::json r = {{"ok", pid != 0}, {"pid", static_cast<std::uint64_t>(pid)}};
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

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::StartWindowMirrorHandler,
                 ::lsw::linkproto::cmd::kStartWindowMirror)
