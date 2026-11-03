#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/win/clipboard_watcher.hpp"
#include "lsw/linkproto/commands.hpp"

#include <stdexcept>

namespace lsw::agent::rpc::handlers {
struct ClipboardGetHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        (void)params;
        auto* clipboard = ctx.services.clipboard();
        if (clipboard == nullptr) {
            throw std::runtime_error("the clipboard is not available in this session");
        }
        return clipboard->read();
    }

    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }

    [[nodiscard]] log::Level request_level() const override { return log::Level::Trace; }

    [[nodiscard]] bool log_request_body() const override { return false; }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        (void)reply;
        return "clipboard";
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ClipboardGetHandler,
                 ::lsw::linkproto::cmd::kClipboardGet)
