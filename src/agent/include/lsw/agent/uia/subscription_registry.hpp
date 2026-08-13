#pragma once

#include "lsw/agent/uia/subscription.hpp"

#include <cstdint>
#include <mutex>
#include <unordered_map>

struct IUIAutomation;

namespace lsw::agent::uia {

class UiaEventQueue;

// Owns every live UIA event subscription and the handles needed to tear
// each one down.
//
// Subscriptions leak COM references if their detach path is wrong, and
// the detach path differs per event kind — so all three live here
// together rather than beside their respective handlers, where the
// asymmetry would be easy to miss.
//
// Thread-safe: ids are handed out and looked up from the RPC dispatcher
// thread while UIA's own threads are firing events.
class SubscriptionRegistry {
public:
    SubscriptionRegistry(IUIAutomation* root, UiaEventQueue& queue) noexcept
        : root_(root), queue_(queue) {}

    // Watch ``pid`` for windows opening and closing. Returns a non-zero
    // id, or 0 on failure.
    [[nodiscard]] std::uint64_t subscribe_pid_lifecycle(std::uint32_t pid);

    // Watch focus changes within ``pid``'s windows.
    [[nodiscard]] std::uint64_t subscribe_pid_focus(std::uint32_t pid);

    // Watch one window's visual state / title / modality.
    [[nodiscard]] std::uint64_t subscribe_window_properties(std::uintptr_t hwnd);

    // Detach and release. No-op for unknown ids.
    void unsubscribe(std::uint64_t id);

private:
    // Insert under a fresh id.
    [[nodiscard]] std::uint64_t store(Subscription sub);

    IUIAutomation* root_;  // borrowed from UiaRoot
    UiaEventQueue& queue_;

    std::mutex mu_;
    std::unordered_map<std::uint64_t, Subscription> subs_;
    std::uint64_t next_id_ = 1;
};

}  // namespace lsw::agent::uia
