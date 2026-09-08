#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lsw/agent/win/discovered_window.hpp"

namespace lsw::agent::win {

// What the agent knows about the guest's windows.
//
// Enumerating them, and the two commands that act on one: closing it,
// and the system command its own chrome would send. A window's pixels
// and its input are not here — they belong to the mirror, which reads
// and writes them in a process of its own.
class Window {
public:
    // Enumerate all top-level visible windows that have a non-empty
    // title and aren't tool windows / our own agent. Per-process exe
    // path comes from QueryFullProcessImageNameW.
    [[nodiscard]] static std::vector<DiscoveredWindow> enumerate();

    // Posts ``WM_CLOSE`` to the window — the standard "user clicked
    // the X" semantics. The app gets a chance to prompt for unsaved
    // changes etc. Returns ``false`` only if ``hwnd`` isn't a valid
    // window handle. Any prompt the app raises in response shows up
    // as a new HWND on the next ``list_windows`` tick, so the host
    // still gets to interact with it.
    [[nodiscard]] static bool close_window(std::uintptr_t hwnd);

    // ``minimize`` / ``maximize`` / ``restore`` as a WM_SYSCOMMAND.
    //
    // The command every window handles the same way whoever drew its
    // chrome, because it arrives at the window procedure rather than at
    // a button: an app with a custom title bar routes its own buttons
    // here too. Returns false for an invalid window or an unknown
    // command name.
    [[nodiscard]] static bool system_command(std::uintptr_t hwnd,
                                             const std::string& command);
};

}  // namespace lsw::agent::win
