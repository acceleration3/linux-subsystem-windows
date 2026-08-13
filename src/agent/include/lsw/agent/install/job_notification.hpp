#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::install {

// ``Progress`` may fire many times per job; ``Done`` fires exactly once
// and is the terminal frame.
enum class JobNotificationKind { Progress, Done };

// One status update from a running install job, on its way to the
// manager.
//
// Fire-and-forget: the manager loses these if it is not connected when
// they are posted. That is acceptable for progress, which is why the
// queue keeps every ``Done`` but caps ``Progress`` — a reconnecting
// manager that missed the middle of a download can still be told how it
// ended.
struct JobNotification {
    JobNotificationKind kind = JobNotificationKind::Progress;
    std::string job_id;
    std::string name;               // human-readable step label
    std::string phase;              // "download" | "install" | "extract" | "script"
    std::uint64_t bytes = 0;        // bytes downloaded so far
    std::uint64_t total_bytes = 0;  // 0 if unknown (chunked encoding)
    int exit_code = 0;              // installer/script exit code (Done only)
    bool ok = false;                // job succeeded (Done only)
    std::string message;            // free-form status / error text
};

}  // namespace lsw::agent::install
