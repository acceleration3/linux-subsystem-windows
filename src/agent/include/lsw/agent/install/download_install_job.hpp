#pragma once

#include "lsw/agent/install/install_job.hpp"

namespace lsw::agent::install {
class DownloadInstallJob final : public InstallJob {
public:
    DownloadInstallJob(std::string id, std::string name, NotificationQueue& queue,
                       std::string url, std::string args, win::RunAs as);

    void run() override;

private:
    std::string url_;
    std::string args_;
};

}
