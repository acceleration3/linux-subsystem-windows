#pragma once

#include "lsw/agent/install/job_notification.hpp"
#include "lsw/agent/install/process_outcome.hpp"

#include <cstdint>
#include <optional>
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
    void report_done(const std::string& phase, bool ok, int exit_code, const std::string& message);

    // Fetch ``url`` into this job's staging area, reporting progress as
    // phase "download". Returns the downloaded file's path, or nullopt
    // after logging and posting the terminal failure — the caller only
    // has to return.
    [[nodiscard]] std::optional<std::wstring> download(const std::string& url);

    // What a failed child process reports as its message: the runner's
    // own error when it has one (spawn failed, timed out), else
    // "<what> exit code N".
    [[nodiscard]] static std::string exit_message(const ProcessOutcome& outcome, const char* what);

    std::string id_;
    std::string name_;

private:
    NotificationQueue& queue_;
};

}  // namespace lsw::agent::install
