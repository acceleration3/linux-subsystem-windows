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

// Agent-pushed events, fanned out by topic to whoever subscribed.
//
// Handlers for a topic run in subscription order, on whichever thread
// calls ``dispatch`` — the channel's reader thread, in practice — and
// outside the bus's own lock, so a handler may subscribe or unsubscribe,
// itself included.
class EventBus {
public:
    // Receives the whole frame, ``event`` field included.
    using Handler = std::function<void(const nlohmann::json& frame)>;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    [[nodiscard]] EventSubscription subscribe(std::string_view topic, Handler handler);

    // Hand ``frame`` to every handler of its ``event`` topic. A frame with
    // no string topic is ignored.
    void dispatch(const nlohmann::json& frame);

private:
    friend class EventSubscription;

    // One registration. ``call_mu`` is held for the length of every call
    // to ``handler``, which is what lets ``unsubscribe`` wait one out.
    // Recursive so a handler can unsubscribe itself.
    struct Entry {
        std::uint64_t id;
        Handler handler;
        std::recursive_mutex call_mu;
        bool live = true;  // guarded by call_mu
    };

    void unsubscribe(std::uint64_t id);

    std::mutex mu_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Entry>>> by_topic_;
    std::atomic<std::uint64_t> next_id_{1};
};

}  // namespace lsw::host::link
