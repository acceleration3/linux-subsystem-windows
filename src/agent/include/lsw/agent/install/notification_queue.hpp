#pragma once

#include "lsw/agent/install/job_notification.hpp"

#include <cstddef>
#include <mutex>
#include <vector>

namespace lsw::agent::install {

// Thread-safe hand-off from job worker threads to the transport tick
// that ships notifications to the manager.
//
// The cap is the whole point: a disconnected manager must not let a slow
// download grow this without bound. Progress notes are dropped past the
// cap; ``Done`` notes never are, because terminal state has to survive
// until someone reconnects to hear it.
class NotificationQueue {
public:
    static constexpr std::size_t kMaxPendingProgress = 4096;

    // Append. Progress notes past the cap are silently dropped.
    void post(JobNotification note);

    // Take everything pending, oldest first, leaving the queue empty.
    [[nodiscard]] std::vector<JobNotification> collect();

    // Put undelivered notes back at the FRONT, preserving order, so the
    // next connection redelivers them.
    //
    // Needed because a drain can fail mid-send — installing
    // virtio-win-guest-tools resets the NIC the RPC channel rides on.
    // Without this a drained-but-unsent ``setup_done`` is simply lost
    // and the wizard waits forever.
    void requeue_front(std::vector<JobNotification> notes);

private:
    std::mutex mu_;
    std::vector<JobNotification> pending_;
};

}  // namespace lsw::agent::install
