#pragma once

#include "lsw/host/link/event_subscription.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::link {
class EventBus {
public:
    using Handler = std::function<void(const nlohmann::json& frame)>;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    [[nodiscard]] EventSubscription subscribe(std::string_view topic, Handler handler);

    void dispatch(const nlohmann::json& frame);

private:
    friend class EventSubscription;

    struct Entry {
        std::uint64_t id;
        Handler handler;
        std::recursive_mutex call_mu;
        bool live = true;
    };

    void unsubscribe(std::uint64_t id);

    std::mutex mu_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Entry>>> by_topic_;
    std::atomic<std::uint64_t> next_id_{1};
};

}
