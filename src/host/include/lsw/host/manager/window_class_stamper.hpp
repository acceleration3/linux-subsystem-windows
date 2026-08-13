#pragma once

#include <QString>

class QWindow;

namespace lsw::host::manager {

// Gives each mirror its own X11 WM_CLASS identity.
//
// Qt writes WM_CLASS once at native-window creation from the
// process-wide application name, so every mirror is born identical and
// the taskbar groups them all under "lsw-manager". Overwriting it
// per-window afterwards is what makes each guest window its own entry
// and Alt-Tab slot.
//
// X11 only. On Wayland the equivalent is ``xdg_toplevel.app_id``, which
// is immutable after creation — WindowView handles that by toggling the
// process-wide desktop file name around the first show instead.
class WindowClassStamper {
public:
    WindowClassStamper() = delete;

    // No-op on Wayland, offscreen, or a build without xcb.
    static void apply(QWindow* window, const QString& key);
};

}  // namespace lsw::host::manager
