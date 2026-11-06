#include "lsw/agent/apps/launch_continuation.hpp"

namespace lsw::agent::apps {

namespace {
constexpr std::uint64_t kWindowTicks = 15ULL * 10'000'000ULL;
constexpr unsigned long kWaitMs = 3000;
}

bool LaunchContinuation::worth_following(std::uint64_t now) const {
    return root_.runs_own_executable && !root_.image_lc.empty() && root_.started != 0 &&
           now >= root_.started && now - root_.started <= kWindowTicks;
}

bool LaunchContinuation::keep_polling(unsigned long elapsed_ms) {
    return elapsed_ms <= kWaitMs;
}

std::optional<unsigned long>
LaunchContinuation::pick(const std::vector<Candidate>& candidates) const {
    const Candidate* best = nullptr;
    for (const Candidate& c : candidates) {
        if (c.pid == root_.pid || c.in_job || c.owned_by_other_launch || rejected_.contains(c.pid) ||
            c.session != root_.session || c.image_lc != root_.image_lc ||
            c.created < root_.started || c.created - root_.started > kWindowTicks) {
            continue;
        }
        const bool parent_ok = c.parent_is_explorer || c.parent_in_launch || c.parent == root_.pid ||
                               chain_.contains(c.parent);
        if (!parent_ok) {
            continue;
        }
        bool newer = false;
        for (const std::uint64_t other : root_.newer_launch_starts) {
            if (c.created >= other) {
                newer = true;
                break;
            }
        }
        if (newer) {
            continue;
        }
        if (best == nullptr || c.created < best->created) {
            best = &c;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return best->pid;
}

}
