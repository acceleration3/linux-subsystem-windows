#pragma once

#include "lsw/agent/install/install_job.hpp"

namespace lsw::agent::install {
class DownloadExtractJob final : public InstallJob {
public:
    DownloadExtractJob(std::string id, std::string name, NotificationQueue& queue,
                       std::string url, std::string extract_to, std::string script,
                       win::RunAs as);

    void run() override;

private:
    [[nodiscard]] bool run_follow_up_script(const std::wstring& extract_dir,
                                            const std::wstring& archive_path);

    std::string url_;
    std::string extract_to_;
    std::string script_;
};

}
