#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::win {
struct DiscoveredWindow {
    std::uintptr_t hwnd;
    std::uintptr_t owner;
    std::uint32_t pid;
    std::string title;
    std::string exe_path;
    int client_w;
    int client_h;
    int screen_x;
    int screen_y;
    bool minimized;
    bool maximized;
    bool has_caption;
};

}
