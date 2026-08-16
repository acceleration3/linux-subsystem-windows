#pragma once

namespace lsw::host::manager {

// What the guest reports about a window before its mirror is shown.
//
// The mirror must open at the right size with the right constraints: if
// Qt's default geometry is allowed to reach the guest as a resize, the
// guest window is shrunk to an arbitrary rectangle the moment the mirror
// appears.
struct GuestWindowProps {
    bool valid = false;
    int width = 800;
    int height = 600;
    bool resizable = true;
    int min_width = 0;
    int min_height = 0;
    // Guest's primary screen size. The host widget may grow past it, but
    // the guest is never asked to be bigger than its own display.
    int screen_width = 0;
    int screen_height = 0;
};

}  // namespace lsw::host::manager
