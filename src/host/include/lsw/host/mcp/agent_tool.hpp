#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {
class AgentTool : public IMcpTool {
public:
    explicit AgentTool(link::AgentRpcClient& agent) noexcept : agent_(agent) {}

protected:
    link::AgentRpcClient& agent_;
};

}
