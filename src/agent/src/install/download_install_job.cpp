#include "lsw/agent/install/download_install_job.hpp"

#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/process.hpp"

#include <utility>
#include <windows.h>

namespace lsw::agent::install {

namespace {

// 0 = success, 3010 = success-but-reboot-required. That pair is the
// standard Windows installer convention; anything else is a failure.
[[nodiscard]] bool is_installer_success(int exit_code) {
    return exit_code == 0 || exit_code == 3010;
}

}  // namespace

DownloadInstallJob::DownloadInstallJob(std::string id, std::string name, NotificationQueue& queue,
                                       std::string url, std::string args, win::RunAs as)
    : InstallJob(std::move(id), std::move(name), queue, std::move(as)), url_(std::move(url)),
      args_(std::move(args)) {}

void DownloadInstallJob::run() {
    const auto downloaded = download(url_);
    if (!downloaded) {
        return;  // terminal notification already posted
    }
    const std::wstring& dest = *downloaded;

    report_status("install", "launching installer");

    // ``"<path>" <args>`` — quoting the path handles spaces.
    std::wstring command_line = win::Process::quote(dest);
    if (!args_.empty()) {
        command_line += L" " + util::TextEncoding::to_wide(args_);
    }
    const auto outcome = ProcessRunner::run(command_line, as_);
    ::DeleteFileW(dest.c_str());

    if (!outcome.completed) {
        LSW_AGENT_ERROR("installer: spawn failed job={} err={}", id_, outcome.error);
        report_done("install", false, outcome.exit_code, outcome.error);
        return;
    }

    const bool ok = is_installer_success(outcome.exit_code);
    LSW_AGENT_INFO("installer: done job={} ok={} exit={}", id_, ok, outcome.exit_code);
    report_done("install", ok, outcome.exit_code,
                ok ? std::string{} : exit_message(outcome, "installer"));
}

}  // namespace lsw::agent::install
