#pragma once

#include "lsw/agent/rpc/event.hpp"
#include "lsw/agent/rpc/event_source.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace lsw::agent::win {

// Watches the desktop for ``EVENT_SYSTEM_MOVESIZESTART`` — fired by
// ``DefWindowProc`` the instant it enters its caption-drag (or
// resize) modal loop on a top-level window. On every such event we:
//
//   1. ``PostMessageW(hwnd, WM_CANCELMODE, 0, 0)`` — aborts the modal
//      loop before the guest window has actually moved a pixel. The
//      loop processes WM_CANCELMODE on its next message-pump
//      iteration, releases capture and exits.
//
//   2. Push a ``caption_drag_started`` event onto the queue the
//      transport drains every tick. The host receives it, looks up
//      its mirror for that hwnd, and calls
//      ``QWindow::startSystemMove`` — the host window now follows
//      the user's drag while the guest window stays put.
//
// The hook is global (``SetWinEventHook(... 0, 0, WINEVENT_OUTOFCONTEXT)``)
// rather than per-PID: this is a Win32 user32 hook, not a UIA event
// source, and the cost of filtering on the host side is trivial.
// Owns its own thread because WinEventHook needs a message pump in
// the calling thread for ``WINEVENT_OUTOFCONTEXT`` callbacks.
//
// Lives in ``AgentServices`` as a sibling to ``uia::Uia``; the
// transport iterates over ``EventSource`` implementations to drain
// pushed events.
class MoveResizeWatcher : public rpc::EventSource {
public:
    MoveResizeWatcher();
    ~MoveResizeWatcher() override;
    MoveResizeWatcher(const MoveResizeWatcher&) = delete;
    MoveResizeWatcher& operator=(const MoveResizeWatcher&) = delete;

    // Drain accumulated push events for the next transport tick.
    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

    // Used by the static WinEventHook callback to publish into the
    // queue — public so the callback (a free function with C linkage)
    // can reach in without befriending it.
    void post_event(rpc::Event ev);

private:
    void start();
    void stop();

    std::mutex mu_;
    std::deque<rpc::Event> pending_;

    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<unsigned long> thread_id_{0};  // for PostThreadMessage WM_QUIT
};

}  // namespace lsw::agent::win
