#pragma once

#include "lsw/agent/install/job_notification.hpp"
#include "lsw/agent/install/process_outcome.hpp"
#include "lsw/agent/win/run_as.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace lsw::agent::install {

class NotificationQueue;

class InstallJob {
public:
    InstallJob(std::string id, std::string name, NotificationQueue& queue, win::RunAs as);
    virtual ~InstallJob() = default;

    InstallJob(const InstallJob&) = delete;
    InstallJob& operator=(const InstallJob&) = delete;

    virtual void run() = 0;

    [[nodiscard]] const std::string& id() const noexcept { return id_; }

protected:
    void report_status(const std::string& phase, const std::string& message);

    void report_bytes(const std::string& phase, std::uint64_t received, std::uint64_t total);

    void report_done(const std::string& phase, bool ok, int exit_code, const std::string& message);

    [[nodiscard]] std::optional<std::wstring> download(const std::string& url);

    [[nodiscard]] static std::string exit_message(const ProcessOutcome& outcome, const char* what);

    std::string id_;
    std::string name_;
    win::RunAs as_;

private:
    NotificationQueue& queue_;
};

}
