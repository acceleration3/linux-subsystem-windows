#include "lsw/host/mcp/mcp_server.hpp"

#include "lsw/host/mcp/tools/console_close_tool.hpp"
#include "lsw/host/mcp/tools/console_input_tool.hpp"
#include "lsw/host/mcp/tools/console_open_tool.hpp"
#include "lsw/host/mcp/tools/console_read_tool.hpp"
#include "lsw/host/mcp/tools/dump_frame_tool.hpp"
#include "lsw/host/mcp/tools/launch_tool.hpp"
#include "lsw/host/mcp/tools/list_apps_tool.hpp"
#include "lsw/host/mcp/tools/list_windows_tool.hpp"
#include "lsw/host/mcp/tools/raw_rpc_tool.hpp"
#include "lsw/host/mcp/tools/recent_logs_tool.hpp"
#include "lsw/host/mcp/tools/redeploy_agent_tool.hpp"
#include "lsw/host/mcp/tools/system_info_tool.hpp"

#include "lsw/host/manager/frame_tap.hpp"
#include "lsw/host/mcp/json_rpc_frame.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::mcp {

McpServer::McpServer(link::AgentRpcClient& rpc, apps::LogStore& log_store, QObject* parent)
    : QObject(parent), agent_(rpc), consoles_(rpc), router_(tools_, agent_), http_(this) {
    register_tools(log_store);
    http_.set_handler([this](const std::string& body) -> std::string {
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(body);
        } catch (const nlohmann::json::exception& e) {
            return JsonRpcFrame::error(nullptr, JsonRpcFrame::kParseError, e.what()).dump();
        }
        return router_.dispatch(request).dump();
    });
}

McpServer::~McpServer() = default;

void McpServer::register_tools(apps::LogStore& log_store) {
    // Build the frame tap here, on the GUI thread, so it has an event
    // loop for ``dump_frame`` to reach it on from an HTTP worker.
    (void)manager::FrameTap::instance();

    // Advertised to the model in this order.
    tools_.add(std::make_unique<SystemInfoTool>(agent_));
    tools_.add(std::make_unique<ListAppsTool>(agent_));
    tools_.add(std::make_unique<ListWindowsTool>(agent_));
    tools_.add(std::make_unique<LaunchTool>(agent_));
    tools_.add(std::make_unique<ConsoleOpenTool>(agent_));
    tools_.add(std::make_unique<ConsoleInputTool>(agent_));
    tools_.add(std::make_unique<ConsoleReadTool>(consoles_));
    tools_.add(std::make_unique<ConsoleCloseTool>(agent_, consoles_));
    tools_.add(std::make_unique<RecentLogsTool>(log_store));
    tools_.add(std::make_unique<DumpFrameTool>());
    tools_.add(std::make_unique<RawRpcTool>(agent_));
    tools_.add(std::make_unique<RedeployAgentTool>(agent_));
}

bool McpServer::listen(std::uint16_t port) { return http_.listen(port); }

std::uint16_t McpServer::port() const noexcept { return http_.port(); }

}  // namespace lsw::host::mcp
