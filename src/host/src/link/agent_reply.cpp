#include "lsw/host/link/agent_reply.hpp"

#include <utility>

namespace lsw::host::link {

namespace {

[[nodiscard]] std::string text_of(const nlohmann::json& value) {
    return value.is_string() ? value.get<std::string>() : value.dump();
}

}  // namespace

AgentReply AgentReply::failure(std::string why) {
    AgentReply reply;
    reply.error = std::move(why);
    return reply;
}

AgentReply AgentReply::from_frame(const nlohmann::json& frame) {
    if (!frame.is_object()) {
        return failure("malformed reply from the agent");
    }
    if (const auto it = frame.find("error"); it != frame.end()) {
        return failure(text_of(*it));
    }
    const auto it = frame.find("result");
    if (it == frame.end()) {
        return failure("reply from the agent carried neither result nor error");
    }
    AgentReply reply;
    reply.body = *it;
    reply.ok = true;
    if (!reply.body.is_object()) {
        return reply;
    }
    const auto ok = reply.body.find("ok");
    const auto detail = reply.body.find("error");
    const bool refused = ok != reply.body.end() && ok->is_boolean() && !ok->get<bool>();
    const bool has_detail = detail != reply.body.end() && !detail->is_null()
                            && !(detail->is_string() && detail->get_ref<const std::string&>().empty());
    if (refused || has_detail) {
        reply.ok = false;
        reply.error = has_detail ? text_of(*detail) : "the agent reported failure with no detail";
    }
    return reply;
}

}  // namespace lsw::host::link
