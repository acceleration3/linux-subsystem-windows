#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lsw/agent/win/discovered_window.hpp"

namespace lsw::agent::win {
class Window {
public:
    [[nodiscard]] static std::vector<DiscoveredWindow> enumerate();

    [[nodiscard]] static bool close_window(std::uintptr_t hwnd);

    [[nodiscard]] static bool system_command(std::uintptr_t hwnd,
                                             const std::string& command);
};

}
