#pragma once

#include "lsw/agent/install/install_job.hpp"

namespace lsw::agent::install {

// Download an archive, unpack it, optionally run a script over the
// result.
//
// Extraction uses the built-in tar.exe (BSD libarchive, Win10 1803+),
// which handles .zip / .tar / .tar.gz / .tar.xz with no extra tooling in
// the guest. The optional follow-up script is how "unpack this tool and
// put its files somewhere" gets expressed without teaching the agent
// about the tool: it runs with ``LSW_EXTRACT_DIR`` and
// ``LSW_DOWNLOAD_PATH`` in the environment.
class DownloadExtractJob final : public InstallJob {
public:
    DownloadExtractJob(std::string id, std::string name, NotificationQueue& queue,
                       std::string url, std::string extract_to, std::string script,
                       win::RunAs as);

    void run() override;

private:
    // Runs the follow-up script over the extracted tree. Returns false
    // if it failed (the terminal notification has already been posted).
    [[nodiscard]] bool run_follow_up_script(const std::wstring& extract_dir,
                                            const std::wstring& archive_path);

    std::string url_;
    std::string extract_to_;  // empty = a job-scoped temp dir we clean up
    std::string script_;
};

}  // namespace lsw::agent::install
