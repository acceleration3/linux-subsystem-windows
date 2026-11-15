#include "lsw/agent/win/discovered_window.hpp"

namespace lsw::agent::win {

nlohmann::json DiscoveredWindow::row() const {
    return {
        {"hwnd", static_cast<std::uint64_t>(hwnd)},
        {"owner", static_cast<std::uint64_t>(owner)},
        {"pid", static_cast<std::uint64_t>(pid)},
        {"root_pid", std::uint64_t{0}},
        {"title", title},
        {"exe_path", exe_path},
        {"client_w", client_w},
        {"client_h", client_h},
        {"screen_x", screen_x},
        {"screen_y", screen_y},
        {"minimized", minimized},
        {"maximized", maximized},
        {"has_caption", has_caption},
        {"task_window", task_window},
        {"on_screen", on_screen},
    };
}

}  // namespace lsw::agent::win
