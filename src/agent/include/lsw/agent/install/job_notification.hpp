#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::install {
enum class JobNotificationKind { Progress, Done };

struct JobNotification {
    JobNotificationKind kind = JobNotificationKind::Progress;
    std::string job_id;
    std::string name;
    std::string phase;
    std::uint64_t bytes = 0;
    std::uint64_t total_bytes = 0;
    int exit_code = 0;
    bool ok = false;
    std::string message;
};

}
