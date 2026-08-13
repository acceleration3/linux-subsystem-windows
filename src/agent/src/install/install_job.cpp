#include "lsw/agent/install/install_job.hpp"

#include "lsw/agent/install/notification_queue.hpp"

#include <utility>

namespace lsw::agent::install {

InstallJob::InstallJob(std::string id, std::string name, NotificationQueue& queue)
    : id_(std::move(id)), name_(std::move(name)), queue_(queue) {}

void InstallJob::report_status(const std::string& phase, const std::string& message) {
    JobNotification n;
    n.kind = JobNotificationKind::Progress;
    n.job_id = id_;
    n.name = name_;
    n.phase = phase;
    n.message = message;
    queue_.post(std::move(n));
}

void InstallJob::report_bytes(const std::string& phase, std::uint64_t received,
                              std::uint64_t total) {
    JobNotification n;
    n.kind = JobNotificationKind::Progress;
    n.job_id = id_;
    n.name = name_;
    n.phase = phase;
    n.bytes = received;
    n.total_bytes = total;
    queue_.post(std::move(n));
}

void InstallJob::report_done(const std::string& phase, bool ok, int exit_code,
                             const std::string& message) {
    JobNotification n;
    n.kind = JobNotificationKind::Done;
    n.job_id = id_;
    n.name = name_;
    n.phase = phase;
    n.ok = ok;
    n.exit_code = exit_code;
    n.message = message;
    queue_.post(std::move(n));
}

}  // namespace lsw::agent::install
