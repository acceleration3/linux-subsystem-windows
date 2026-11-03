#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/agent/win/clipboard_watcher.hpp"
#include "lsw/agent/win/image_codec.hpp"
#include "lsw/linkproto/commands.hpp"

#include <stdexcept>

namespace lsw::agent::rpc::handlers {
struct ClipboardSetHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        auto* clipboard = ctx.services.clipboard();
        if (clipboard == nullptr) {
            throw std::runtime_error("the clipboard is not available in this session");
        }
        win::ClipboardWatcher::Content content;
        content.clear = params.value("clear", false);
        content.secret = params.value("secret", false);
        if (params.contains("text") && params["text"].is_string()) {
            content.text = params["text"].get<std::string>();
        }
        if (params.contains("png") && params["png"].is_string()) {
            content.png = util::Base64::decode(params["png"].get<std::string>());
            auto dib = win::ImageCodec::png_to_dib(*content.png);
            if (!dib.empty()) {
                content.dib = std::move(dib);
            }
        }
        clipboard->write(content);
        return {{"ok", true}};
    }

    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }

    [[nodiscard]] log::Level request_level() const override { return log::Level::Trace; }

    [[nodiscard]] bool log_request_body() const override { return false; }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ClipboardSetHandler,
                 ::lsw::linkproto::cmd::kClipboardSet)
