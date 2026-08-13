#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace lsw::host::mcp {

// Outcome of one RPC to the guest agent, with the two failure modes the
// tools care about already collapsed into one: a transport failure and
// an agent-reported error both arrive as ``ok == false`` with a message
// worth showing the model.
struct AgentReply {
    bool ok = false;
    nlohmann::json body;
    std::string error;
};

}  // namespace lsw::host::mcp
