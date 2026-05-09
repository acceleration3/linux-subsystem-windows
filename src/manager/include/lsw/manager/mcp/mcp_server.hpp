#pragma once

#include "lsw/manager/vm/agent_rpc_client.hpp"

#include <QHash>
#include <QObject>
#include <QString>

#include <cstdint>
#include <mutex>

#include <nlohmann/json_fwd.hpp>

class QTcpServer;
class QTcpSocket;

namespace lsw::manager::apps { class LogStore; }

namespace lsw::manager::mcp {

// Embedded MCP (Model Context Protocol) server. Lets an external LLM
// client — Claude Code, Claude Desktop, anything that speaks the
// 2024-11-05 MCP HTTP transport — connect to the running manager and
// drive the guest agent for diagnosis: open a cmd.exe, type into it,
// snapshot the screen, list windows / installed apps, fire arbitrary
// RPCs.
//
// Transport: HTTP/1.1 POST on ``localhost:5105`` (no TLS, no auth —
// localhost-only, single user). Each POST body is one JSON-RPC 2.0
// frame; the response body is the matching reply. Stateless: no
// session header needed for tool calls.
//
// All non-trivial tool work runs on a QtConcurrent worker so the
// GUI event loop never blocks on an RPC. The response is written
// back to the socket via a queued connection.
class McpServer : public QObject {
    Q_OBJECT
public:
    McpServer(vm::AgentRpcClient& rpc, apps::LogStore& log_store, QObject* parent = nullptr);
    ~McpServer() override;

    // Start the listener. Returns ``true`` on success; logs and
    // returns ``false`` if the bind fails (already in use, etc.).
    [[nodiscard]] bool listen(std::uint16_t port = 5105);

    [[nodiscard]] std::uint16_t port() const noexcept;

private Q_SLOTS:
    void on_new_connection();

private:
    // Parse one inbound HTTP request from ``sock`` and dispatch it.
    // The socket is owned by QTcpServer; we writeback + close it
    // after the response is ready.
    void handle_socket(QTcpSocket* sock);

    // JSON-RPC dispatcher — returns the reply frame to send.
    [[nodiscard]] nlohmann::json dispatch_jsonrpc(const nlohmann::json& req);

    // MCP method handlers. Each returns the ``result`` body (the
    // dispatcher wraps with id + jsonrpc).
    [[nodiscard]] nlohmann::json on_initialize(const nlohmann::json& params);
    [[nodiscard]] nlohmann::json on_tools_list();
    [[nodiscard]] nlohmann::json on_tools_call(const nlohmann::json& params);

    // Per-tool implementations. Each returns a ``result`` body
    // matching MCP's tool-call shape: ``{"content":[{"type":"text",
    // "text":"..."}], "isError":false}``.
    [[nodiscard]] nlohmann::json tool_system_info(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_list_apps(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_list_windows(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_launch(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_console_open(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_console_input(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_console_read(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_console_close(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_recent_logs(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_rpc(const nlohmann::json& args);
    [[nodiscard]] nlohmann::json tool_redeploy_agent(const nlohmann::json& args);

    // Send an HTTP/1.1 response and close the socket. Body is JSON.
    static void send_http_response(QTcpSocket* sock, int status,
                                    const std::string& body);

    vm::AgentRpcClient& rpc_;
    apps::LogStore& log_store_;
    QTcpServer* server_ = nullptr;

    // Per-cmd-session output ring, fed by the on_event("console_output")
    // handler. ``console_read`` drains and returns; the LLM polls
    // between input lines. Bounded by ``kSessionBufferCap`` to keep a
    // chatty cmd.exe from blowing the manager's heap.
    static constexpr std::size_t kSessionBufferCap = 256 * 1024;
    mutable std::mutex sessions_mu_;
    QHash<QString, QByteArray> sessions_;

    // Handles for the console event subscriptions, retained so the
    // destructor can unregister cleanly.
    vm::AgentRpcClient::EventToken output_token_ = 0;
    vm::AgentRpcClient::EventToken closed_token_ = 0;
};

}  // namespace lsw::manager::mcp
