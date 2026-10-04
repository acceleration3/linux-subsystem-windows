#pragma once

#include <cstdint>

namespace lsw::host::link {

class EventBus;

// One handler's registration on an ``EventBus``, removed when this is
// destroyed or reset.
//
// Removal waits for a call to the handler already in progress on the
// reader thread, so once the subscription is gone nothing the handler
// captured is touched again. That makes the usual member layout safe —
// the subscription declared after whatever its handler uses, so it is
// destroyed first — and is why a handler must never block on the thread
// that owns its subscription.
//
// Must not outlive the bus it came from.
class EventSubscription {
public:
    EventSubscription() noexcept = default;
    ~EventSubscription();

    EventSubscription(EventSubscription&& other) noexcept;
    EventSubscription& operator=(EventSubscription&& other) noexcept;
    EventSubscription(const EventSubscription&) = delete;
    EventSubscription& operator=(const EventSubscription&) = delete;

    void reset();

    [[nodiscard]] explicit operator bool() const noexcept { return bus_ != nullptr; }

private:
    friend class EventBus;
    EventSubscription(EventBus* bus, std::uint64_t id) noexcept : bus_(bus), id_(id) {}

    EventBus* bus_ = nullptr;
    std::uint64_t id_ = 0;
};

}  // namespace lsw::host::link
