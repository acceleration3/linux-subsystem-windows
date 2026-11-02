#pragma once

#include "lsw/agent/win/discovered_window.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace lsw::agent::apps {
class AppInstallerWindow {
public:
    explicit AppInstallerWindow(std::vector<win::DiscoveredWindow> before);

    [[nodiscard]] std::optional<std::uint32_t>
    select(const std::vector<win::DiscoveredWindow>& windows, std::uintptr_t foreground,
           bool allow_existing) const;

private:
    [[nodiscard]] static bool is_installer(const win::DiscoveredWindow& window);
    std::vector<win::DiscoveredWindow> before_;
};

}
