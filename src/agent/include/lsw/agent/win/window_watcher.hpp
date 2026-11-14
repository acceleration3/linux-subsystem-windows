#pragma once

#include "lsw/agent/apps/window_announcements.hpp"

#include <atomic>
#include <thread>

namespace lsw::agent::win {
class WindowWatcher {
public:
    WindowWatcher();
    ~WindowWatcher();

    WindowWatcher(const WindowWatcher&) = delete;
    WindowWatcher& operator=(const WindowWatcher&) = delete;

    [[nodiscard]] apps::WindowAnnouncements& announcements() noexcept { return announcements_; }

private:
    static void __stdcall on_event(void* hook, unsigned long event, void* hwnd, long object, long child,
                                   unsigned long thread, unsigned long time);

    void run();

    apps::WindowAnnouncements announcements_;
    std::atomic<unsigned long> thread_id_{0};
    std::thread thread_;
};

}
