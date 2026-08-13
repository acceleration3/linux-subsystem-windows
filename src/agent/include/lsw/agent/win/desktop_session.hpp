#pragma once

#include <chrono>
#include <string>

namespace lsw::agent::win {

// Answers "can the user actually see and use their desktop yet?".
//
// Derived from a full session trace of a real first boot (DesktopProbe),
// after several plausible-sounding guesses each proved wrong:
//
//   * The task's LogonTrigger fires when LSA mints the session token,
//     long before anything is on screen.
//   * A sentinel written by a first-logon script is no better:
//     FirstLogonCommands is part of OOBE and runs while "Preparing
//     Windows" is displayed.
//   * The shell being up means nothing here. Explorer, Progman and the
//     taskbar were ALL present in the trace's first sample, 84 seconds
//     before the desktop appeared: Windows starts the shell early and
//     the overlay covers it. Progman was up in 1012 of 1014 samples, so
//     it cannot gate anything on its own.
//   * The input desktop stayed "Default" throughout — it never becomes
//     Winlogon, and the Winlogon desktop cannot even be opened from here
//     (ACCESS_DENIED), so nothing about it is observable.
//
// What the overlay actually is, per the trace: visible, top-level,
// exactly screen-sized, WS_EX_TOPMOST windows titled "Preparing
// Windows", owned by FirstLogonAnim.exe. So that is what we look for —
// by shape rather than by name, since the hosting process is an
// implementation detail that changes between Windows releases.
class DesktopSession {
public:
    DesktopSession() = delete;

    // Explorer owns a desktop (Progman exists). Necessary, never
    // sufficient — see above.
    [[nodiscard]] static bool shell_ready();

    // A visible, screen-filling, top-level window owned by something
    // other than the shell is covering the desktop.
    //
    // Shell-owned windows are excluded because Progman itself is
    // screen-sized and always present.
    [[nodiscard]] static bool overlay_present();

    // Shell up and nothing covering it.
    [[nodiscard]] static bool desktop_usable();

    // Block until the desktop is usable and has stayed that way for
    // ``stable_for``, or until ``timeout``. Returns false on timeout;
    // callers should proceed anyway, because an agent that never starts
    // cannot be diagnosed remotely.
    //
    // On the very first boot this additionally waits to SEE an overlay
    // before accepting its absence. Both the overlay and this agent
    // start at logon, so without that the agent can simply win the race,
    // observe no overlay yet, and release exactly as early as if there
    // were no gate at all. ``first_boot_grace`` bounds that wait, so a
    // machine that shows no overlay still starts promptly.
    [[nodiscard]] static bool wait_for_desktop(std::chrono::seconds timeout,
                                               std::chrono::milliseconds poll_interval,
                                               std::chrono::milliseconds stable_for,
                                               std::chrono::seconds first_boot_grace);

    // Compact state summary for the log.
    [[nodiscard]] static std::string diagnostics();
};

}  // namespace lsw::agent::win
