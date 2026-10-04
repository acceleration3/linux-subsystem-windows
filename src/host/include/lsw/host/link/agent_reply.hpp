#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace lsw::host::link {

// The outcome of one RPC to the guest agent, decoded once, here, rather
// than at every caller.
//
// The agent says "no" two ways and callers used to check for one or the
// other: an ``error`` frame (unknown command, a handler that threw) and a
// ``result`` that carries ``ok: false`` or an ``error`` of its own (a
// launch the guest refused, a folder Windows would not open). A transport
// failure — no target, a dropped connection, a timeout — is a third. All
// three arrive as ``ok == false`` with a sentence in ``error`` worth
// showing a person.
struct AgentReply {
    bool ok = false;

    // The ``result`` object. Kept on an ``ok: false`` result too: a
    // refusal still carries its other fields.
    nlohmann::json body;

    std::string error;

    [[nodiscard]] static AgentReply failure(std::string why);

    // Decode one reply frame: ``{id, result}`` or ``{id, error}``. Never
    // throws, whatever the agent sent.
    [[nodiscard]] static AgentReply from_frame(const nlohmann::json& frame);
};

}  // namespace lsw::host::link
