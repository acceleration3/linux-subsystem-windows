#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/shell_link.hpp"
#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {
struct ResolveShortcutHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ) override {
        const auto path = Params{params, linkproto::cmd::kResolveShortcut}.require_string("path");

        nlohmann::json reply = {{"target", ""}, {"args", ""}, {"workdir", ""}};
        const auto link = win::ShellLink::resolve(util::TextEncoding::to_wide(path),
                                                  win::ShellLink::PathMode::UncPriority);
        if (link) {
            reply["target"] = util::TextEncoding::to_utf8(link->path);
            reply["args"] = util::TextEncoding::to_utf8(link->args);
            reply["workdir"] = util::TextEncoding::to_utf8(link->working_dir);
        }
        return reply;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        const auto target = reply.value("target", std::string{});
        return target.empty() ? "unresolved" : target;
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ResolveShortcutHandler,
                 ::lsw::linkproto::cmd::kResolveShortcut)
