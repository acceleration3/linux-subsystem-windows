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

class Installer {
public:
    Installer() = default;

    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;

    [[nodiscard]] std::string start_download_install(const std::string& name,
                                                     const std::string& url,
                                                     const std::string& args,
                                                     const win::RunAs& as);

    [[nodiscard]] std::string start_run_script(const std::string& name,
                                               const std::string& script,
                                               const win::RunAs& as);

    [[nodiscard]] std::string start_download_extract(const std::string& name,
                                                     const std::string& url,
                                                     const std::string& extract_to,
                                                     const std::string& script,
                                                     const win::RunAs& as);

    [[nodiscard]] std::vector<JobNotification> collect_pending();

    void requeue_front(std::vector<JobNotification> notes);

private:
    static void spawn(std::unique_ptr<InstallJob> job);

    JobIdGenerator ids_;
    NotificationQueue queue_;
};

}
