#pragma once

#include "lsw/agent/rpc/event.hpp"
#include "lsw/agent/rpc/event_source.hpp"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace lsw::agent::uia {

// Buffers events that UIA raises on its own COM threads until the
// transport's tick drains them.
//
// Separate from everything that produces events because it is the only
// piece that is genuinely concurrent: COM worker threads push, the
// transport thread drains, and the cap protects against a notifier gone
// wild (an installer flashing thousands of child popups) growing the
// process without bound.
class UiaEventQueue final : public rpc::EventSource {
public:
    // Roughly a few seconds of pathological activity. Oldest first out.
    static constexpr std::size_t kCapacity = 4096;

    // Called from UIA's thread pool. Cheap by construction: lock,
    // append, return.
    void post(rpc::Event event);

    // EventSource: drain everything accumulated since the last call.
    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    std::mutex mu_;
    std::deque<rpc::Event> pending_;
};

}  // namespace lsw::agent::uia
