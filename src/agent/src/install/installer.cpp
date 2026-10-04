#include "lsw/agent/install/installer.hpp"

#include "lsw/agent/install/download_extract_job.hpp"
#include "lsw/agent/install/download_install_job.hpp"
#include "lsw/agent/install/run_script_job.hpp"
#include "lsw/agent/log.hpp"

#include <thread>
#include <utility>

namespace lsw::agent::install {

void Installer::spawn(std::unique_ptr<InstallJob> job) {
    // The thread owns the job for its lifetime and is detached: setup
    // steps outlive the RPC that started them, and the manager tracks
    // completion by job id, not by joining anything.
    std::thread t([owned = std::move(job)]() mutable { owned->run(); });
    t.detach();
}

std::string Installer::start_download_install(const std::string& name, const std::string& url,
                                              const std::string& args, const win::RunAs& as) {
    const std::string job_id = ids_.next();
    LSW_AGENT_INFO("installer: start download_install job={} name={} url={} as {}", job_id, name,
                   url, as.describe());
    spawn(std::make_unique<DownloadInstallJob>(job_id, name, queue_, url, args, as));
    return job_id;
}

std::string Installer::start_run_script(const std::string& name, const std::string& script,
                                        const win::RunAs& as) {
    const std::string job_id = ids_.next();
    LSW_AGENT_INFO("installer: start run_script job={} name={} bytes={} as {}", job_id, name,
                   script.size(), as.describe());
    spawn(std::make_unique<RunScriptJob>(job_id, name, queue_, script, as));
    return job_id;
}

std::string Installer::start_download_extract(const std::string& name, const std::string& url,
                                              const std::string& extract_to,
                                              const std::string& script, const win::RunAs& as) {
    const std::string job_id = ids_.next();
    LSW_AGENT_INFO("installer: start download_extract job={} name={} url={} extract_to={} "
                   "script_bytes={} as {}",
                   job_id, name, url, extract_to, script.size(), as.describe());
    spawn(std::make_unique<DownloadExtractJob>(job_id, name, queue_, url, extract_to, script,
                                               as));
    return job_id;
}

std::vector<JobNotification> Installer::collect_pending() {
    return queue_.collect();
}

void Installer::requeue_front(std::vector<JobNotification> notes) {
    queue_.requeue_front(std::move(notes));
}

}  // namespace lsw::agent::install
