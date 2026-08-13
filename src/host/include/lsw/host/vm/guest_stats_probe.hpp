#pragma once

#include "lsw/host/vm/guest_stats_sample.hpp"

namespace lsw::host::vm {

// How Monitor gets numbers that only exist inside the guest.
//
// This interface is why lsw_host_vm does not depend on lsw_host_link:
// the VM layer declares what it needs, and the agent-channel layer —
// which already depends on vm — supplies the implementation. Tests
// substitute a canned probe with no VM and no socket anywhere in sight.
class IGuestStatsProbe {
public:
    IGuestStatsProbe() = default;
    virtual ~IGuestStatsProbe() = default;
    IGuestStatsProbe(const IGuestStatsProbe&) = delete;
    IGuestStatsProbe& operator=(const IGuestStatsProbe&) = delete;

    // False when there is nothing to ask (no channel, no target). Monitor
    // skips the tick entirely rather than paying for a doomed round-trip.
    [[nodiscard]] virtual bool available() const = 0;

    // Blocking. Called on a worker thread, never on the GUI thread.
    [[nodiscard]] virtual GuestStatsSample sample() = 0;
};

}  // namespace lsw::host::vm
