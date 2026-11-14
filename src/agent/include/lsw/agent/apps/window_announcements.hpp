#pragma once

#include "lsw/agent/rpc/event_source.hpp"
#include "lsw/agent/win/discovered_window.hpp"

#include <mutex>
#include <vector>

namespace lsw::agent::apps {
class WindowAnnouncements final : public rpc::EventSource {
public:
    static constexpr std::size_t kMostPending = 64;

    void appeared(const win::DiscoveredWindow& window);

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    std::mutex mu_;
    std::vector<win::DiscoveredWindow> pending_;
};

}
