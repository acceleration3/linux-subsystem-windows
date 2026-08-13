#pragma once

#include "lsw/host/mcp/agent_reply.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {

// Issues one RPC to the guest agent and normalises the answer.
//
// Every tool talks to the guest through this and never touches
// AgentRpcClient directly, so "no reply", "unparseable reply" and
// "{error: ...}" are handled identically in exactly one place.
class AgentCommand {
public:
    explicit AgentCommand(link::AgentRpcClient& rpc) noexcept : rpc_(rpc) {}

    // Whether there is a VM to talk to at all. The router checks this
    // before dispatching a tool that needs the agent, so tools never
    // have to produce their own "no VM targeted" message.
    [[nodiscard]] bool agent_available() const;

    // Blocking. Called on an HTTP worker thread, never on the GUI thread.
    [[nodiscard]] AgentReply invoke(const std::string& cmd,
                                    const nlohmann::json& params = nlohmann::json::object());

private:
    link::AgentRpcClient& rpc_;
};

}  // namespace lsw::host::mcp
