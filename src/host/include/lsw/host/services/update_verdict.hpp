#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace lsw::host::link { struct AgentReply; }

namespace lsw::host::services {
class UpdateVerdict {
public:
    enum class Kind {
        Waiting,
        Failed,
        RestartPending,
        Succeeded,
    };

    Kind kind = Kind::Waiting;
    std::string message;
    nlohmann::json last;

    [[nodiscard]] static std::string finished_of(const nlohmann::json& body);

    [[nodiscard]] static bool is_mine(const nlohmann::json& last, const std::string& baseline);

    [[nodiscard]] static bool moved_on(const link::AgentReply& info, const std::string& baseline);

    [[nodiscard]] static bool started_after(const link::AgentReply& info, const std::string& finished);

    [[nodiscard]] static UpdateVerdict judge(const link::AgentReply& info, const std::string& baseline,
                                             const std::string& shipped,
                                             const std::string& shipped_config);
};

}
