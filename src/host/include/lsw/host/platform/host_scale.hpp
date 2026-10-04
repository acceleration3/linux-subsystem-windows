#pragma once

class QWidget;

namespace lsw::host::platform {

// The scale this desktop draws at, as the guest's Windows scaling.
//
// The window mirror draws one guest pixel to one screen pixel, so a
// guest at 100 % on a desktop at 120 % comes out at five sixths of the
// size of the apps beside it. Giving Windows the desktop's own scale
// lays it out at the same size, and it stays sharp: nothing is
// resampled to get there.
class HostScale {
public:
    HostScale() = delete;

    // The scale ``window`` is drawn at, as a percentage: 120 for 1.2.
    // A window's, not a screen's: on Wayland a screen reports its scale
    // rounded up to a whole number (2 for 1.2), and the fractional one
    // reaches a window only once the compositor has mapped it. 100 with
    // no window.
    [[nodiscard]] static int percent(const QWidget* window);

    // What a Scaling setting means: 0 is "match this desktop", which is
    // the scale ``window`` is drawn at; anything else is that percentage.
    [[nodiscard]] static int resolve(int setting, const QWidget* window);
};

}  // namespace lsw::host::platform
