#include "lsw/agent/install/download_install_job.hpp"

#include "lsw/agent/install/http_downloader.hpp"
#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/install/url_file_name.hpp"
#include "lsw/agent/install/work_dir.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <windows.h>

#include <utility>

namespace lsw::agent::install {

namespace {

// 0 = success, 3010 = success-but-reboot-required. That pair is the
// standard Windows installer convention; anything else is a failure.
[[nodiscard]] bool is_installer_success(int exit_code) {
    return exit_code == 0 || exit_code == 3010;
}

}  // namespace

DownloadInstallJob::DownloadInstallJob(std::string id, std::string name,
                                       NotificationQueue& queue, std::string url,
                                       std::string args)
    : InstallJob(std::move(id), std::move(name), queue),
      url_(std::move(url)), args_(std::move(args)) {}

void DownloadInstallJob::run() {
    const std::wstring wurl = util::TextEncoding::to_wide(url_);
    const std::wstring dest = WorkDir::staged_path(id_, L"-" + UrlFileName::of(wurl));

    report_status("download", "starting download of " + url_);

    std::string error;
    const bool downloaded = HttpDownloader::fetch(
        wurl, dest,
        [this](std::uint64_t received, std::uint64_t total) {
            report_bytes("download", received, total);
        },
        error);
    if (!downloaded) {
        LSW_AGENT_ERROR(std::string{"installer: download failed job="} + id_
                        + " url=" + url_ + " err=" + error);
        report_done("download", false, 0, error);
        return;
    }

    report_status("install", "launching installer");

    // ``"<path>" <args>`` — quoting the path handles spaces.
    std::wstring command_line = L"\"" + dest + L"\"";
    if (!args_.empty()) {
        command_line += L" " + util::TextEncoding::to_wide(args_);
    }
    const auto outcome = ProcessRunner::run(command_line);
    ::DeleteFileW(dest.c_str());

    if (!outcome.completed) {
        LSW_AGENT_ERROR(std::string{"installer: spawn failed job="} + id_
                        + " err=" + outcome.error);
        report_done("install", false, outcome.exit_code, outcome.error);
        return;
    }

    const bool ok = is_installer_success(outcome.exit_code);
    LSW_AGENT_INFO(std::string{"installer: done job="} + id_
                   + " ok=" + (ok ? "true" : "false")
                   + " exit=" + std::to_string(outcome.exit_code));
    report_done("install", ok, outcome.exit_code,
                ok ? std::string{} : "installer exit code "
                                         + std::to_string(outcome.exit_code));
}

}  // namespace lsw::agent::install
