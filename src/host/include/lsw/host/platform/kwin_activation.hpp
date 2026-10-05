#pragma once

#include <QtGlobal>

namespace lsw::host::platform {

// Brings another process's windows to the front, on KWin.
//
// A Wayland client cannot raise a window of its own that is not already
// active, let alone another process's: what it can ask for is attention.
// KWin's scripting, on the session bus, can do it, and it is what the
// mirror processes the manager spawns place their windows through
// already. A script that activates every normal window of the process -
// un-minimising it first - is loaded, run, and unloaded.
class KWinActivation {
public:
    KWinActivation() = delete;

    // False when this desktop is not KWin, or KWin would not run the script.
    [[nodiscard]] static bool activate(qint64 pid);
};

}  // namespace lsw::host::platform
