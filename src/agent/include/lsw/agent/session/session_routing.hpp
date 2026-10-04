#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace lsw::agent::session {

// The request-side half of sending a command to a session: which session
// a request names, and the parameter and reply rewriting that lets
// several helpers share one id space towards the manager.
//
// Which commands are session-bound at all is the handler's to say
// (``RpcHandler::placement``); this is only what happens to the JSON on
// the way through.
class SessionRouting {
public:
    SessionRouting() = delete;

    // The parameter every session-aware command takes.
    static constexpr std::string_view kSessionParam = "session";

    // ``params.session`` when present. Throws std::invalid_argument for a
    // value that is not a non-negative integer that fits a session id:
    // a typo should fail the request, not quietly pick the default.
    [[nodiscard]] static std::optional<std::uint32_t> requested(const nlohmann::json& params,
                                                                std::string_view cmd);

    // ``params.elevated``, default true: launched processes get the
    // user's full token unless the request asks otherwise.
    [[nodiscard]] static bool elevated(const nlohmann::json& params);

    // UI Automation subscription ids are allocated by each helper, so two
    // helpers hand out the same small numbers. Towards the manager they
    // carry the session in their top 32 bits, which also lets
    // ``uia_unsubscribe`` find its helper with no ``session`` given.
    [[nodiscard]] static std::uint64_t global_sub_id(std::uint32_t session, std::uint64_t local);
    [[nodiscard]] static std::uint32_t session_of_sub_id(std::uint64_t global);
    [[nodiscard]] static std::uint64_t local_sub_id(std::uint64_t global);

    // The session a request is for: ``session`` when given, else the one
    // encoded in its ``sub_id``, else nullopt (the caller's default).
    [[nodiscard]] static std::optional<std::uint32_t> target(const nlohmann::json& params,
                                                             std::string_view cmd);

    // ``params`` as the helper should see them: no ``session``, and a
    // ``sub_id`` back in the helper's own numbering.
    [[nodiscard]] static nlohmann::json params_for_helper(const nlohmann::json& params);

    // A helper's reply as the manager should see it: a non-zero
    // ``sub_id`` moved into the global numbering.
    static void globalize_reply(nlohmann::json& reply, std::uint32_t session);
};

}  // namespace lsw::agent::session
