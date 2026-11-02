#pragma once

#include <chrono>
#include <string>

namespace lsw::agent::win {
class DesktopSession {
public:
    DesktopSession() = delete;

    [[nodiscard]] static bool shell_ready();

    [[nodiscard]] static bool overlay_present();

    [[nodiscard]] static bool wait_for_desktop(std::chrono::seconds timeout,
                                               std::chrono::milliseconds poll_interval,
                                               std::chrono::milliseconds stable_for,
                                               std::chrono::seconds first_boot_grace);

    [[nodiscard]] static std::string diagnostics();
};

}
