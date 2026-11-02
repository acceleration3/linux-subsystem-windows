#pragma once

#include <cstdint>

namespace lsw::host::link {

class EventBus;

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

}
