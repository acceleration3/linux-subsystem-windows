#pragma once

#include "lsw/agent/install/job_id_generator.hpp"
#include "lsw/agent/install/job_notification.hpp"
#include "lsw/agent/install/notification_queue.hpp"
#include "lsw/agent/win/run_as.hpp"

#include <memory>
#include <string>
#include <vector>

namespace lsw::agent::install {

class InstallJob;

// Starts setup jobs and owns the queue their notifications land in.
//
// Composition only: it allocates an id, builds the right InstallJob,
// detaches a thread for it, and hands the transport whatever the jobs
// have said since the last tick. What a job kind actually *does* lives
// in that job's class.
//
// Not a singleton — owned by the agent's service registry and passed to
// handlers by reference, so a test can run one with its own queue.
class Installer {
public:
    Installer() = default;

    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;

    // Every job's processes run as ``as``: SYSTEM in session 0 unless
    // the request named a session, in which case its user.

    // Download ``url`` and run it with ``args``. Returns the new job id.
    [[nodiscard]] std::string start_download_install(const std::string& name,
                                                     const std::string& url,
                                                     const std::string& args,
                                                     const win::RunAs& as);

    // Write ``script`` to a temp .cmd and run it via cmd.exe.
    [[nodiscard]] std::string start_run_script(const std::string& name,
                                               const std::string& script,
                                               const win::RunAs& as);

    // Download the archive at ``url``, extract into ``extract_to`` (or a
    // temp dir when empty), then run ``script`` if non-empty.
    [[nodiscard]] std::string start_download_extract(const std::string& name,
                                                     const std::string& url,
                                                     const std::string& extract_to,
                                                     const std::string& script,
                                                     const win::RunAs& as);

    // Drain everything the running jobs have reported. Called by the
    // transport on its select tick.
    [[nodiscard]] std::vector<JobNotification> collect_pending();

    // Return undelivered notifications to the front of the queue after a
    // failed send. See NotificationQueue::requeue_front.
    void requeue_front(std::vector<JobNotification> notes);

private:
    // Detach a thread that owns and runs ``job`` to completion.
    static void spawn(std::unique_ptr<InstallJob> job);

    JobIdGenerator ids_;
    NotificationQueue queue_;
};

}  // namespace lsw::agent::install
