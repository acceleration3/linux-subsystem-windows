#include "lsw/host/link/event_subscription.hpp"

#include "lsw/host/link/event_bus.hpp"

#include <utility>

namespace lsw::host::link {

EventSubscription::~EventSubscription() { reset(); }

EventSubscription::EventSubscription(EventSubscription&& other) noexcept
    : bus_(std::exchange(other.bus_, nullptr)), id_(std::exchange(other.id_, 0)) {}

EventSubscription& EventSubscription::operator=(EventSubscription&& other) noexcept {
    if (this != &other) {
        reset();
        bus_ = std::exchange(other.bus_, nullptr);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void EventSubscription::reset() {
    if (auto* bus = std::exchange(bus_, nullptr)) {
        bus->unsubscribe(std::exchange(id_, 0));
    }
}

}  // namespace lsw::host::link
