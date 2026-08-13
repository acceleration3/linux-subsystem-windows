#pragma once

#include "lsw/agent/install/job_notification.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::install {

class NotificationQueue;

// One unit of setup work, running on its own detached thread.
//
// Subclasses implement ``run`` and describe what happened through the
// report helpers; they never touch the queue, the wire format, or the
// manager. Adding a job kind is adding a subclass.
class InstallJob {
public:
    InstallJob(std::string id, std::string name, NotificationQueue& queue);
    virtual ~InstallJob() = default;

    InstallJob(const InstallJob&) = delete;
    InstallJob& operator=(const InstallJob&) = delete;

    // Called once, on a worker thread. Must post exactly one terminal
    // notification before returning — the manager waits on it.
    virtual void run() = 0;

    [[nodiscard]] const std::string& id() const noexcept { return id_; }

protected:
    // Free-form status line within ``phase``.
    void report_status(const std::string& phase, const std::string& message);

    // Byte counts during a download. ``total`` 0 means unknown.
    void report_bytes(const std::string& phase, std::uint64_t received, std::uint64_t total);

    // Terminal. Exactly one per job.
    void report_done(const std::string& phase, bool ok, int exit_code,
                     const std::string& message);

    std::string id_;
    std::string name_;

private:
    NotificationQueue& queue_;
};

}  // namespace lsw::agent::install
