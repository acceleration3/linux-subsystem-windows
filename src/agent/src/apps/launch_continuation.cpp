#include "lsw/agent/apps/launch_continuation.hpp"

namespace lsw::agent::apps {

namespace {
constexpr std::uint64_t kWindowTicks = 15ULL * 10'000'000ULL;
}

std::optional<unsigned long>
LaunchContinuation::pick(const std::vector<Candidate>& candidates) const {
    if (image_lc_.empty() || started_ == 0) {
        return std::nullopt;
    }
    const Candidate* best = nullptr;
    for (const Candidate& c : candidates) {
        if (c.pid == root_ || c.owned_by_other_launch || c.session != session_ ||
            c.image_lc != image_lc_ || c.created < started_ || c.created - started_ > kWindowTicks) {
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
