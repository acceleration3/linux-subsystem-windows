#pragma once

#include "lsw/agent/install/job_notification.hpp"

#include <cstddef>
#include <mutex>
#include <vector>

namespace lsw::agent::install {
class NotificationQueue {
public:
    static constexpr std::size_t kMaxPendingProgress = 4096;

    void post(JobNotification note);

    [[nodiscard]] std::vector<JobNotification> collect();

    void requeue_front(std::vector<JobNotification> notes);

private:
    std::mutex mu_;
    std::vector<JobNotification> pending_;
};

}
