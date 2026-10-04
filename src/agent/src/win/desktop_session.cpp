#include "lsw/agent/win/desktop_session.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/win/file_system.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <cstdio>
#include <iterator>
#include <string>
#include <thread>
#include <windows.h>

namespace lsw::agent::win {

namespace {

// Written once the gate has legitimately opened, so later boots can skip
// the first-boot overlay wait. Its absence is what "first boot" means.

// A window counts as screen-filling if it spans the primary monitor,
// compared with a little slack so an off-by-a-pixel window still counts.
constexpr int kCoverSlackPx = 2;

struct OverlayScan {
    HWND shell = nullptr;
    DWORD shell_pid = 0;
    bool found = false;
    wchar_t klass[128] = {};
    wchar_t title[128] = {};
};

BOOL CALLBACK overlay_proc(HWND window, LPARAM param) {
    auto* scan = reinterpret_cast<OverlayScan*>(param);
    if (window == scan->shell || ::IsWindowVisible(window) == FALSE) {
        return TRUE;
    }
    // Progman is screen-sized and always there; so is the taskbar's
    // parent chain. Anything the shell owns is the desktop, not a cover.
    DWORD pid = 0;
    ::GetWindowThreadProcessId(window, &pid);
    if (pid == scan->shell_pid) {
        return TRUE;
    }
    RECT rect{};
    if (::GetWindowRect(window, &rect) == FALSE) {
        return TRUE;
    }
    if (rect.right - rect.left < ::GetSystemMetrics(SM_CXSCREEN) - kCoverSlackPx ||
        rect.bottom - rect.top < ::GetSystemMetrics(SM_CYSCREEN) - kCoverSlackPx) {
        return TRUE;
    }
    scan->found = true;
    ::GetClassNameW(window, scan->klass, static_cast<int>(std::size(scan->klass)));
    ::GetWindowTextW(window, scan->title, static_cast<int>(std::size(scan->title)));
    return FALSE;  // one is enough
}

[[nodiscard]] OverlayScan scan_for_overlay() {
    OverlayScan scan;
    scan.shell = ::GetShellWindow();
    ::GetWindowThreadProcessId(scan.shell, &scan.shell_pid);
    ::EnumWindows(&overlay_proc, reinterpret_cast<LPARAM>(&scan));
    return scan;
}

void write_flag() {
    // Created and closed at once: the file existing is the whole flag.
    const UniqueHandle file{::CreateFileW(core::Paths::desktop_ready_flag().c_str(), GENERIC_WRITE,
                                          0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                          nullptr)};
}

}  // namespace

bool DesktopSession::shell_ready() {
    return ::GetShellWindow() != nullptr;
}

bool DesktopSession::overlay_present() {
    return scan_for_overlay().found;
}

std::string DesktopSession::diagnostics() {
    const OverlayScan scan = scan_for_overlay();
    std::string out = std::string{"progman="} + (shell_ready() ? "yes" : "no") +
                      " overlay=" + (scan.found ? "yes" : "no");
    if (scan.found) {
        char buf[320] = {};
        std::snprintf(buf, sizeof(buf), " overlay_cls=%ls overlay_title=\"%ls\"", scan.klass,
                      scan.title);
        out += buf;
    }
    return out;
}

bool DesktopSession::wait_for_desktop(std::chrono::seconds timeout,
                                      std::chrono::milliseconds poll_interval,
                                      std::chrono::milliseconds stable_for,
                                      std::chrono::seconds first_boot_grace) {
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + timeout;

    // On a first boot the overlay and this agent both start at logon, so
    // "no overlay yet" is indistinguishable from "overlay already gone"
    // until we have actually seen one. Later boots have the flag and
    // skip this entirely.
    const bool first_boot = !FileSystem::exists(core::Paths::desktop_ready_flag());
    bool overlay_seen = false;
    const auto grace_ends = started + first_boot_grace;

    constexpr auto kTraceEvery = std::chrono::seconds(5);
    auto next_trace = started;

    std::chrono::steady_clock::time_point usable_since{};
    bool usable = false;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        const bool covered = overlay_present();
        if (covered) {
            overlay_seen = true;
        }
        // Still inside the first-boot grace and no overlay has appeared:
        // do not trust its absence yet.
        const bool awaiting_first_overlay = first_boot && !overlay_seen && now < grace_ends;
        const bool ready = shell_ready() && !covered && !awaiting_first_overlay;

        if (now >= next_trace) {
            next_trace = now + kTraceEvery;
            LSW_AGENT_INFO("desktop: {} {} first_boot={} overlay_seen={}",
                           ready ? "ready" : "waiting", diagnostics(), first_boot ? "yes" : "no",
                           overlay_seen ? "yes" : "no");
        }

        if (ready) {
            if (!usable) {
                usable = true;
                usable_since = now;
            }
            if (now - usable_since >= stable_for) {
                const auto waited = std::chrono::duration_cast<std::chrono::seconds>(now - started);
                LSW_AGENT_INFO("desktop: ready after {}s — {}", waited.count(), diagnostics());
                write_flag();
                return true;
            }
        } else {
            // The shell restarts as OOBE finishes; restart the stability
            // window rather than counting time from before the restart.
            usable = false;
        }
        std::this_thread::sleep_for(poll_interval);
    }
    LSW_AGENT_WARN("desktop: gave up waiting — {}", diagnostics());
    return false;
}

}  // namespace lsw::agent::win
