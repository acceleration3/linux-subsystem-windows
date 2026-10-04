#include "lsw/host/link/event_bus.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

namespace lsw::host::link {

EventSubscription EventBus::subscribe(std::string_view topic, Handler handler) {
    auto entry = std::make_shared<Entry>();
    entry->id = next_id_.fetch_add(1);
    entry->handler = std::move(handler);
    const auto id = entry->id;
    {
        std::lock_guard<std::mutex> lock(mu_);
        by_topic_[std::string{topic}].push_back(std::move(entry));
    }
    return EventSubscription{this, id};
}

void EventBus::unsubscribe(std::uint64_t id) {
    std::shared_ptr<Entry> removed;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& [topic, entries] : by_topic_) {
            const auto it = std::find_if(entries.begin(), entries.end(),
                                         [id](const auto& e) { return e->id == id; });
            if (it != entries.end()) {
                removed = std::move(*it);
                entries.erase(it);
                break;
            }
        }
    }
    if (removed) {
        // Waits for a call already in progress on the reader thread.
        std::lock_guard<std::recursive_mutex> lock(removed->call_mu);
        removed->live = false;
    }
}

void EventBus::dispatch(const nlohmann::json& frame) {
    if (!frame.is_object()) {
        return;
    }
    const auto topic = frame.find("event");
    if (topic == frame.end() || !topic->is_string()) {
        return;
    }
    // Snapshot under the lock, call outside it: a handler that
    // subscribes or unsubscribes would otherwise deadlock.
    std::vector<std::shared_ptr<Entry>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = by_topic_.find(topic->get_ref<const std::string&>());
        if (it == by_topic_.end()) {
            return;
        }
        snapshot = it->second;
    }
    for (const auto& entry : snapshot) {
        std::lock_guard<std::recursive_mutex> lock(entry->call_mu);
        if (entry->live) {
            entry->handler(frame);
        }
    }
}

}  // namespace lsw::host::link
