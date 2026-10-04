#include "lsw/agent/session/session_routing.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace lsw::agent::session {

namespace {

constexpr const char* kSubId = "sub_id";
constexpr std::uint64_t kLocalMask = 0xFFFFFFFFULL;

// A sub_id as a number, whichever way the JSON holds it (a value built in
// code is signed, one parsed off the wire is unsigned).
std::optional<std::uint64_t> sub_id_of(const nlohmann::json& v) {
    if (v.is_number_unsigned()) {
        return v.get<std::uint64_t>();
    }
    if (v.is_number_integer() && v.get<std::int64_t>() >= 0) {
        return static_cast<std::uint64_t>(v.get<std::int64_t>());
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::uint32_t> SessionRouting::requested(const nlohmann::json& params,
                                                       std::string_view cmd) {
    if (!params.is_object()) {
        return std::nullopt;
    }
    const auto it = params.find(std::string{kSessionParam});
    if (it == params.end() || it->is_null()) {
        return std::nullopt;
    }
    const auto refuse = [&] {
        throw std::invalid_argument(std::string{cmd} +
                                    ": session must be a non-negative integer session id");
    };
    if (it->is_number_unsigned()) {
        const auto v = it->get<std::uint64_t>();
        if (v > std::numeric_limits<std::uint32_t>::max()) {
            refuse();
        }
        return static_cast<std::uint32_t>(v);
    }
    if (it->is_number_integer()) {
        const auto v = it->get<std::int64_t>();
        if (v < 0 || v > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            refuse();
        }
        return static_cast<std::uint32_t>(v);
    }
    refuse();
    return std::nullopt;  // unreachable
}

bool SessionRouting::elevated(const nlohmann::json& params) {
    if (!params.is_object()) {
        return true;
    }
    const auto it = params.find("elevated");
    return it == params.end() || !it->is_boolean() || it->get<bool>();
}

std::uint64_t SessionRouting::global_sub_id(std::uint32_t session, std::uint64_t local) {
    if (local == 0) {
        return 0;  // "no subscription" stays that
    }
    return (static_cast<std::uint64_t>(session) << 32) | (local & kLocalMask);
}

std::uint32_t SessionRouting::session_of_sub_id(std::uint64_t global) {
    return static_cast<std::uint32_t>(global >> 32);
}

std::uint64_t SessionRouting::local_sub_id(std::uint64_t global) {
    return global & kLocalMask;
}

std::optional<std::uint32_t> SessionRouting::target(const nlohmann::json& params,
                                                    std::string_view cmd) {
    if (auto explicit_session = requested(params, cmd)) {
        return explicit_session;
    }
    if (params.is_object()) {
        const auto it = params.find(kSubId);
        if (it != params.end()) {
            // An id with no session in it goes wherever the caller's
            // default goes.
            if (const auto id = sub_id_of(*it); id && session_of_sub_id(*id) != 0) {
                return session_of_sub_id(*id);
            }
        }
    }
    return std::nullopt;
}

nlohmann::json SessionRouting::params_for_helper(const nlohmann::json& params) {
    nlohmann::json out = params.is_object() ? params : nlohmann::json::object();
    out.erase(std::string{kSessionParam});
    if (const auto it = out.find(kSubId); it != out.end()) {
        if (const auto id = sub_id_of(*it)) {
            *it = local_sub_id(*id);
        }
    }
    return out;
}

void SessionRouting::globalize_reply(nlohmann::json& reply, std::uint32_t session) {
    if (!reply.is_object()) {
        return;
    }
    if (const auto it = reply.find(kSubId); it != reply.end()) {
        if (const auto id = sub_id_of(*it)) {
            *it = global_sub_id(session, *id);
        }
    }
}

}  // namespace lsw::agent::session
