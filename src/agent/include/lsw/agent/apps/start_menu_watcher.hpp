#pragma once

#include "lsw/agent/rpc/event_source.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace lsw::agent::apps {

// Notices an application being installed or removed, and says so to the
// manager as an ``apps_changed`` event, so its list updates without anyone
// pressing Refresh.
//
// Watches the Start menu's Programs folders - the machine's, and every
// user profile's - recursively, since an installer's shortcut is what puts
// an application in the list. An installer writes many files, so a burst
// of changes is one event, sent about two seconds after the last of them.
// Profiles that appear after the watch began are picked up within a few
// seconds.
//
// An event source: the connection's pump drains it. A change that happens
// with no manager connected stays pending and is delivered to the next
// one, which at worst refreshes a list that was already current.
class StartMenuWatcher : public rpc::EventSource {
public:
    StartMenuWatcher() = default;
    ~StartMenuWatcher() override;

    StartMenuWatcher(const StartMenuWatcher&) = delete;
    StartMenuWatcher& operator=(const StartMenuWatcher&) = delete;

    // Begins watching; later calls, from any thread, do nothing.
    void start();

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    void run();

    std::once_flag started_;
    std::thread thread_;
    void* stop_event_ = nullptr;  // a Win32 event; void* to keep windows.h out of the header
    std::atomic<bool> changed_{false};
};

}  // namespace lsw::agent::apps
