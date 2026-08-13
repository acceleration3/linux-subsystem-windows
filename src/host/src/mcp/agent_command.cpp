#include "lsw/host/mcp/agent_command.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"

namespace lsw::host::mcp {

bool AgentCommand::agent_available() const { return rpc_.has_target(); }

AgentReply AgentCommand::invoke(const std::string& cmd, const nlohmann::json& params) {
    nlohmann::json request = {{"cmd", cmd}};
    if (!params.empty()) {
        request["params"] = params;
    }
    const auto reply = rpc_.call(request);
    if (reply.isEmpty()) {
        return {false, {}, "agent unreachable (no reply)"};
    }
    try {
        const auto j = nlohmann::json::parse(reply.constData(),
                                             reply.constData() + reply.size());
        if (j.is_object() && j.contains("error")) {
            return {false, {}, j["error"].get<std::string>()};
        }
        return {true, j, {}};
    } catch (const std::exception& e) {
        return {false, {}, std::string{"parse failed: "} + e.what()};
    }
}

}  // namespace lsw::host::mcp
