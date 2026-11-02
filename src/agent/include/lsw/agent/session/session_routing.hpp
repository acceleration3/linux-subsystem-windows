#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace lsw::agent::session {
class SessionRouting {
public:
    SessionRouting() = delete;

    static constexpr std::string_view kSessionParam = "session";

    [[nodiscard]] static std::optional<std::uint32_t> requested(const nlohmann::json& params,
                                                                std::string_view cmd);

    [[nodiscard]] static bool elevated(const nlohmann::json& params);

    [[nodiscard]] static std::uint64_t global_sub_id(std::uint32_t session, std::uint64_t local);
    [[nodiscard]] static std::uint32_t session_of_sub_id(std::uint64_t global);
    [[nodiscard]] static std::uint64_t local_sub_id(std::uint64_t global);

    [[nodiscard]] static std::optional<std::uint32_t> target(const nlohmann::json& params,
                                                             std::string_view cmd);

    [[nodiscard]] static nlohmann::json params_for_helper(const nlohmann::json& params);

    static void globalize_reply(nlohmann::json& reply, std::uint32_t session);
};

}
