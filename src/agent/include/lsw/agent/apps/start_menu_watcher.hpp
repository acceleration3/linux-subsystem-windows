#pragma once

#include "lsw/agent/rpc/event_source.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace lsw::agent::apps {
class StartMenuWatcher : public rpc::EventSource {
public:
    StartMenuWatcher() = default;
    ~StartMenuWatcher() override;

    StartMenuWatcher(const StartMenuWatcher&) = delete;
    StartMenuWatcher& operator=(const StartMenuWatcher&) = delete;

    void start();

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    void run();

    std::once_flag started_;
    std::thread thread_;
    void* stop_event_ = nullptr;
    std::atomic<bool> changed_{false};
};

}
