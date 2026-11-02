#include "lsw/agent/core/services.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/linkproto/commands.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lsw::agent::rpc::handlers {

namespace {

constexpr std::array<std::string_view, 5> kLevels = {"trace", "debug", "info", "warn", "error"};

std::string checked(const nlohmann::json& params, const char* key) {
    const auto& value = params.at(key);
    if (!value.is_string()) {
        throw std::invalid_argument(std::string{"set_log_level: "} + key + " is not a level name");
    }
    std::string name = value.get<std::string>();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (std::find(kLevels.begin(), kLevels.end(), name) == kLevels.end()) {
        throw std::invalid_argument("set_log_level: " + name + " is not a level (trace, debug, "
                                    "info, warn, error)");
    }
    return name;
}

}

struct SetLogLevelHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        std::string agent;
        std::string mirrors;
        if (params.contains("agent")) {
            agent = checked(params, "agent");
        }
        if (params.contains("guest_mirrors")) {
            mirrors = checked(params, "guest_mirrors");
        }
        const std::string was_agent = log::level_name(log::level());
        const std::string was_mirrors = ctx.services.mirror_log_level();
        if (!agent.empty()) {
            log::set_level(log::level_from_name(agent));
            ctx.services.sessions().sync_log_level();
        }
        if (!mirrors.empty()) {
            ctx.services.set_mirror_log_level(mirrors);
        }
        if ((!agent.empty() && agent != was_agent) || (!mirrors.empty() && mirrors != was_mirrors)) {
            LSW_AGENT_INFO("set_log_level: agent {} (was {}), window mirrors {} (was {})",
                           log::level_name(log::level()), was_agent, ctx.services.mirror_log_level(),
                           was_mirrors);
        }
        return {{"agent", log::level_name(log::level())},
                {"guest_mirrors", ctx.services.mirror_log_level()}};
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SetLogLevelHandler, ::lsw::linkproto::cmd::kSetLogLevel)
