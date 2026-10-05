#include "lsw/host/mcp/tools/redeploy_agent_tool.hpp"

#include "lsw/host/mcp/agent_redeployer.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::mcp {

std::string RedeployAgentTool::name() const { return "redeploy_agent"; }

std::string RedeployAgentTool::description() const {
    return "Hot-swap the running guest agent with the locally-built "
           "``lsw-guest-agent.exe`` (found via ``$LSW_AGENT_EXE`` or the "
           "build-tree default), and the window mirror's guest half "
           "``remotewin-guest.exe`` with the local one (``$LSW_REMOTEWIN_GUEST`` "
           "or the build-tree default) when there is one. The manager spins up "
           "a one-shot HTTP server the guest reaches at ``10.0.2.2:<port>``, "
           "sends a swap script (curl download, the mirror's guest half renamed "
           "aside and replaced, the agent service stopped, its binary moved in, "
           "the service started), then polls ``system_info`` until the "
           "``agent_version`` field changes to confirm the new agent is running, "
           "and checks the size of the guest's remotewin-guest.exe against the "
           "one served. Running mirrors keep the old guest half; new ones start "
           "the new one. Blocks for up to ~45 s.";
}

nlohmann::json RedeployAgentTool::input_schema() const { return JsonSchema::no_args(); }

nlohmann::json RedeployAgentTool::call(const nlohmann::json&) {
    AgentRedeployer redeployer{agent_};
    const auto outcome = redeployer.run();
    return outcome.ok ? ToolResult::text(outcome.message)
                      : ToolResult::error(outcome.message);
}

}  // namespace lsw::host::mcp
