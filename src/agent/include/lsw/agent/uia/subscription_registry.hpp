#pragma once

#include "lsw/agent/uia/subscription.hpp"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace lsw::agent::uia {

class UiaEventQueue;

class SubscriptionRegistry {
public:
    SubscriptionRegistry(IUIAutomation* root, UiaEventQueue& queue) noexcept
        : root_(root), queue_(queue) {}

    [[nodiscard]] std::uint64_t subscribe_pid_lifecycle(std::uint32_t pid);

    [[nodiscard]] std::uint64_t subscribe_pid_focus(std::uint32_t pid);

    [[nodiscard]] std::uint64_t subscribe_window_properties(std::uintptr_t hwnd);

    void unsubscribe(std::uint64_t id);

    void clear();

private:
    [[nodiscard]] std::uint64_t store(Subscription sub);

    void detach(Subscription& sub);

    IUIAutomation* root_;
    UiaEventQueue& queue_;

    std::mutex mu_;
    std::unordered_map<std::uint64_t, Subscription> subs_;
    std::uint64_t next_id_ = 1;
};

}
