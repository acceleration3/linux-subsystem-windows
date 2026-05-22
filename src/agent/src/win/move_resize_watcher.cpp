#include "lsw/agent/win/move_resize_watcher.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <utility>

namespace lsw::agent::win {

namespace {

// The hook callback runs on the watcher's worker thread (since we
// used WINEVENT_OUTOFCONTEXT — events arrive via the calling
// thread's message queue). Stash the owning watcher in a TLS slot
// so the static callback can reach back in without a global.
thread_local MoveResizeWatcher* tls_owner = nullptr;

void CALLBACK on_event(HWINEVENTHOOK /*hook*/, DWORD event, HWND hwnd,
                        LONG /*idObject*/, LONG /*idChild*/,
                        DWORD /*thread*/, DWORD /*time*/) {
    if (event != EVENT_SYSTEM_MOVESIZESTART) return;
    if (hwnd == nullptr) return;
    MoveResizeWatcher* owner = tls_owner;
    if (owner == nullptr) return;
    // Abort DefWindowProc's caption-drag modal loop before the
    // guest window has a chance to move. ``WM_CANCELMODE`` is the
    // documented way to break out of any modal-tracking state; the
    // wndproc's SC_MOVE loop honours it on the next message
    // dispatch (≤ a few µs after it began).
    ::PostMessageW(hwnd, WM_CANCELMODE, 0, 0);
    owner->post_event({"caption_drag_started",
                       {{"hwnd", static_cast<std::uint64_t>(
                                    reinterpret_cast<std::uintptr_t>(hwnd))}}});
}

}  // namespace

MoveResizeWatcher::MoveResizeWatcher() {
    start();
}

MoveResizeWatcher::~MoveResizeWatcher() {
    stop();
}

void MoveResizeWatcher::start() {
    if (worker_.joinable()) return;
    stop_ = false;
    worker_ = std::thread([this] {
        tls_owner = this;
        thread_id_.store(::GetCurrentThreadId());
        // WINEVENT_OUTOFCONTEXT: events arrive via this thread's
        // message queue (no DLL injection into target processes).
        // WINEVENT_SKIPOWNPROCESS: don't fire for windows owned by
        // the agent process itself.
        HWINEVENTHOOK hook = ::SetWinEventHook(
            EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART, nullptr,
            &on_event, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook == nullptr) {
            LSW_AGENT_WARN("win::MoveResizeWatcher: SetWinEventHook failed");
            tls_owner = nullptr;
            thread_id_.store(0);
            return;
        }
        LSW_AGENT_INFO("win::MoveResizeWatcher: running");
        MSG msg;
        for (;;) {
            const BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
            if (got == 0 || got == -1) break;  // WM_QUIT or error
            if (stop_.load()) break;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::UnhookWinEvent(hook);
        tls_owner = nullptr;
        thread_id_.store(0);
    });
}

void MoveResizeWatcher::stop() {
    if (!worker_.joinable()) return;
    stop_.store(true);
    const auto tid = thread_id_.load();
    if (tid != 0) {
        ::PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }
    worker_.join();
}

void MoveResizeWatcher::post_event(rpc::Event ev) {
    std::lock_guard<std::mutex> g(mu_);
    // Cap so a runaway notifier can't grow the process unboundedly.
    constexpr size_t kCap = 1024;
    if (pending_.size() >= kCap) {
        pending_.pop_front();
    }
    pending_.push_back(std::move(ev));
}

std::vector<rpc::Event> MoveResizeWatcher::collect_pending() {
    std::vector<rpc::Event> out;
    std::lock_guard<std::mutex> g(mu_);
    if (pending_.empty()) {
        return out;
    }
    out.reserve(pending_.size());
    for (auto& e : pending_) {
        out.push_back(std::move(e));
    }
    pending_.clear();
    return out;
}

}  // namespace lsw::agent::win
