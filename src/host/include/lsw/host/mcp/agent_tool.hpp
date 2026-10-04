#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {

// A tool that does its work by asking the guest agent. Holds the channel
// so each such tool needs no constructor of its own; ``requires_agent``
// stays true, so the router has already answered for a missing VM by the
// time ``call`` runs.
//
// The constructor is public because an inherited constructor keeps its
// base's access; the class is abstract either way.
class AgentTool : public IMcpTool {
public:
    explicit AgentTool(link::AgentRpcClient& agent) noexcept : agent_(agent) {}

protected:
    link::AgentRpcClient& agent_;
};

}  // namespace lsw::host::mcp
