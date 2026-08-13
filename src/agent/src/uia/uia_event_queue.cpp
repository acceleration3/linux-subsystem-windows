#include "lsw/agent/uia/uia_event_queue.hpp"

#include <utility>

namespace lsw::agent::uia {

void UiaEventQueue::post(rpc::Event event) {
    const std::lock_guard<std::mutex> lock(mu_);
    if (pending_.size() >= kCapacity) {
        pending_.pop_front();  // drop oldest
    }
    pending_.push_back(std::move(event));
}

std::vector<rpc::Event> UiaEventQueue::collect_pending() {
    std::vector<rpc::Event> out;
    const std::lock_guard<std::mutex> lock(mu_);
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

}  // namespace lsw::agent::uia
