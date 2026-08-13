#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace lsw::agent::win {

// Dumps the complete visible state of the session on a timer: every
// window station desktop, every process, every top-level window and
// every child window beneath it.
//
// This exists because the moment that matters — the first-logon
// "Preparing Windows" phase — happens exactly once per install and
// cannot be reproduced by rebooting into it. There is no opportunity to
// attach and look around: by the time a command can be typed it is over.
//
// So nothing here is filtered or interpreted. Earlier attempts picked a
// signal up front (shell window, taskbar, foreground geometry, input
// desktop) and each was wrong in a way only visible afterwards. A full
// snapshot removes the guess: capture everything once, then decide from
// the trace which condition actually tracks the desktop appearing.
//
// Writes to its own file (``C:\ProgramData\LSW\desktop-probe.log``)
// rather than the agent log, which would otherwise be unreadable — a
// snapshot is hundreds of lines and they arrive every second.
class DesktopProbe {
public:
    DesktopProbe() = default;
    ~DesktopProbe();

    DesktopProbe(const DesktopProbe&) = delete;
    DesktopProbe& operator=(const DesktopProbe&) = delete;

    // Begin snapshotting on a background thread until ``duration``
    // elapses or ``stop()`` is called.
    void start(std::chrono::seconds duration, std::chrono::milliseconds interval);

    void stop();

    // One complete snapshot. Public so the gate can stamp the exact
    // state at the instant it decided to open, into the same file.
    [[nodiscard]] static std::string snapshot(const char* reason);

    // Append a block to the probe log.
    static void write(const std::string& text);

private:
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace lsw::agent::win
