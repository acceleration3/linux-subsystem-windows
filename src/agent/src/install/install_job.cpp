#include "lsw/agent/install/install_job.hpp"

#include "lsw/agent/install/http_downloader.hpp"
#include "lsw/agent/install/notification_queue.hpp"
#include "lsw/agent/install/url_file_name.hpp"
#include "lsw/agent/install/work_dir.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <utility>

namespace lsw::agent::install {

InstallJob::InstallJob(std::string id, std::string name, NotificationQueue& queue)
    : id_(std::move(id)), name_(std::move(name)), queue_(queue) {}

void InstallJob::report_status(const std::string& phase, const std::string& message) {
    JobNotification n;
    n.kind = JobNotificationKind::Progress;
    n.job_id = id_;
    n.name = name_;
    n.phase = phase;
    n.message = message;
    queue_.post(std::move(n));
}

void InstallJob::report_bytes(const std::string& phase, std::uint64_t received,
                              std::uint64_t total) {
    JobNotification n;
    n.kind = JobNotificationKind::Progress;
    n.job_id = id_;
    n.name = name_;
    n.phase = phase;
    n.bytes = received;
    n.total_bytes = total;
    queue_.post(std::move(n));
}

void InstallJob::report_done(const std::string& phase, bool ok, int exit_code,
                             const std::string& message) {
    JobNotification n;
    n.kind = JobNotificationKind::Done;
    n.job_id = id_;
    n.name = name_;
    n.phase = phase;
    n.ok = ok;
    n.exit_code = exit_code;
    n.message = message;
    queue_.post(std::move(n));
}

std::optional<std::wstring> InstallJob::download(const std::string& url) {
    const std::wstring wurl = util::TextEncoding::to_wide(url);
    std::wstring dest = WorkDir::staged_path(id_, L"-" + UrlFileName::of(wurl));

    report_status("download", "starting download of " + url);

    std::string error;
    const bool downloaded = HttpDownloader::fetch(
        wurl, dest,
        [this](std::uint64_t received, std::uint64_t total) {
            report_bytes("download", received, total);
        },
        error);
    if (!downloaded) {
        LSW_AGENT_ERROR("installer: download failed job={} url={} err={}", id_, url, error);
        report_done("download", false, 0, error);
        return std::nullopt;
    }
    return dest;
}

std::string InstallJob::exit_message(const ProcessOutcome& outcome, const char* what) {
    if (!outcome.error.empty()) {
        return outcome.error;
    }
    return std::string{what} + " exit code " + std::to_string(outcome.exit_code);
}

}  // namespace lsw::agent::install
