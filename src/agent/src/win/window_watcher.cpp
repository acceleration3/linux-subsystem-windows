#include "lsw/agent/win/window_watcher.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/window.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <future>

namespace lsw::agent::win {

namespace {
constexpr DWORD kShown = 0x8002;
constexpr DWORD kUncloaked = 0x8018;

std::atomic<WindowWatcher*> g_watcher{nullptr};
}

WindowWatcher::WindowWatcher() {
    std::promise<void> ready;
    auto started = ready.get_future();
    thread_ = std::thread{[this, p = std::move(ready)]() mutable {
        thread_id_.store(::GetCurrentThreadId());
        MSG msg;
        ::PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        p.set_value();
        run();
    }};
    started.wait();
}

WindowWatcher::~WindowWatcher() {
    ::PostThreadMessageW(thread_id_.load(), WM_QUIT, 0, 0);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void __stdcall WindowWatcher::on_event(void*, unsigned long, void* hwnd, long object, long child,
                                       unsigned long, unsigned long) {
    const auto window = static_cast<HWND>(hwnd);
    if (object != OBJID_WINDOW || child != CHILDID_SELF || window == nullptr) {
        return;
    }
    if ((::GetWindowLongW(window, GWL_STYLE) & WS_CAPTION) != WS_CAPTION ||
        ::GetAncestor(window, GA_ROOT) != window || ::GetWindow(window, GW_OWNER) != nullptr) {
        return;
    }
    WindowWatcher* self = g_watcher.load();
    if (self == nullptr) {
        return;
    }
    if (const auto found = Window::describe(reinterpret_cast<std::uintptr_t>(window))) {
        self->announcements_.appeared(*found);
    }
}

void WindowWatcher::run() {
    g_watcher.store(this);
    const auto proc = reinterpret_cast<WINEVENTPROC>(&WindowWatcher::on_event);
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    const HWINEVENTHOOK shown = ::SetWinEventHook(kShown, kShown, nullptr, proc, 0, 0, flags);
    const HWINEVENTHOOK uncloaked = ::SetWinEventHook(kUncloaked, kUncloaked, nullptr, proc, 0, 0, flags);
    if (shown == nullptr || uncloaked == nullptr) {
        LSW_AGENT_WARN("window_watcher: no window event hook ({}); new windows of applications the "
                       "manager did not start are not announced",
                       ErrorText::win32(::GetLastError()));
    }
    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::DispatchMessageW(&msg);
    }
    if (shown != nullptr) {
        ::UnhookWinEvent(shown);
    }
    if (uncloaked != nullptr) {
        ::UnhookWinEvent(uncloaked);
    }
    g_watcher.store(nullptr);
}

}
