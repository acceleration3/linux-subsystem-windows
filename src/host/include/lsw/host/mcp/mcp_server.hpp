#pragma once

#include "lsw/host/mcp/console_session_buffer.hpp"
#include "lsw/host/mcp/http_server.hpp"
#include "lsw/host/mcp/json_rpc_router.hpp"
#include "lsw/host/mcp/tool_registry.hpp"

#include <QObject>

#include <cstdint>

namespace lsw::host::apps { class LogStore; }
namespace lsw::host::link { class AgentRpcClient; }
namespace lsw::host::services {
class GuestUpdate;
class LogCleaner;
class LogCollector;
class StatusHub;
}

namespace lsw::host::mcp {
class McpServer : public QObject {
    Q_OBJECT
public:
    McpServer(link::AgentRpcClient& rpc, apps::LogStore& log_store,
              services::GuestUpdate& update, services::StatusHub& status,
              services::LogCollector& collector, services::LogCleaner& cleaner,
              QObject* parent = nullptr);
    ~McpServer() override;

    [[nodiscard]] bool listen(std::uint16_t port = 5105);

    [[nodiscard]] std::uint16_t port() const noexcept;

private:
    void register_tools(apps::LogStore& log_store, services::GuestUpdate& update,
                        services::StatusHub& status, services::LogCollector& collector,
                        services::LogCleaner& cleaner);

    link::AgentRpcClient& agent_;
    ConsoleSessionBuffer consoles_;
    ToolRegistry tools_;
    JsonRpcRouter router_;
    HttpServer http_;
};

}
