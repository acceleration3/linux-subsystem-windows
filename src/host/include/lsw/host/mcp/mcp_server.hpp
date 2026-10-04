#pragma once

#include "lsw/host/mcp/console_session_buffer.hpp"
#include "lsw/host/mcp/http_server.hpp"
#include "lsw/host/mcp/json_rpc_router.hpp"
#include "lsw/host/mcp/tool_registry.hpp"

#include <QObject>

#include <cstdint>

namespace lsw::host::apps { class LogStore; }
namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {

// The manager's external automation surface: MCP (Model Context
// Protocol) over localhost HTTP, letting an LLM client drive the guest
// for diagnosis — open a cmd.exe, type into it, list windows or
// installed apps, fire arbitrary RPCs, hot-swap the agent.
//
// This class is composition and nothing else. The transport is
// HttpServer, the protocol is JsonRpcRouter, and every capability is an
// IMcpTool in the registry. Adding a tool means adding a file and one
// line in ``register_tools`` — nothing here grows.
//
// No TLS and no auth: bound to loopback only, single user.
class McpServer : public QObject {
    Q_OBJECT
public:
    McpServer(link::AgentRpcClient& rpc, apps::LogStore& log_store,
              QObject* parent = nullptr);
    ~McpServer() override;

    // Start the listener. False (logged) if the bind fails.
    [[nodiscard]] bool listen(std::uint16_t port = 5105);

    [[nodiscard]] std::uint16_t port() const noexcept;

private:
    void register_tools(apps::LogStore& log_store);

    // Order is load-bearing: the router holds references to the registry,
    // and the tools hold references to the console buffer.
    link::AgentRpcClient& agent_;
    ConsoleSessionBuffer consoles_;
    ToolRegistry tools_;
    JsonRpcRouter router_;
    HttpServer http_;
};

}  // namespace lsw::host::mcp
