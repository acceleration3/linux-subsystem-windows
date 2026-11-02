#pragma once

#include "lsw/agent/session/session_info.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace lsw::agent::win {
class WtsSessions {
public:
    WtsSessions() = delete;

    [[nodiscard]] static std::vector<session::SessionInfo> list();

    [[nodiscard]] static std::optional<std::uint32_t> active_console();

    [[nodiscard]] static std::optional<std::uint32_t> of_process(std::uint32_t pid);

    [[nodiscard]] static std::uint32_t current();
};

}
