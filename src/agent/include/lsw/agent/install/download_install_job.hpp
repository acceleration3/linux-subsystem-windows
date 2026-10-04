#pragma once

#include "lsw/agent/install/install_job.hpp"

namespace lsw::agent::install {

// Download an installer and run it silently.
//
// Suitable for the ``vc_redist.x64.exe /install /quiet`` shape: fetch the
// URL to the work directory, launch it with the caller's arguments, wait
// for it to exit, and delete the download either way.
class DownloadInstallJob final : public InstallJob {
public:
    DownloadInstallJob(std::string id, std::string name, NotificationQueue& queue,
                       std::string url, std::string args, win::RunAs as);

    void run() override;

private:
    std::string url_;
    std::string args_;
};

}  // namespace lsw::agent::install
