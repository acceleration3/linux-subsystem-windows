#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/shell_link.hpp"
#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {

// What a .lnk points at.
//
// Configuring an application by picking its Start menu shortcut is the
// obvious thing to do, and storing the shortcut itself is the wrong
// answer: an installer's uninstaller deletes it and the entry stops
// working for a reason nobody can see. So the target is resolved here,
// once, and the entry keeps its arguments and working folder too.
struct ResolveShortcutHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
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

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ResolveShortcutHandler,
                 ::lsw::linkproto::cmd::kResolveShortcut)
