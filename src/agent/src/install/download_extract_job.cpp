#include "lsw/agent/install/download_extract_job.hpp"

#include "lsw/agent/install/directory_maker.hpp"
#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/install/script_file.hpp"
#include "lsw/agent/install/work_dir.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <utility>
#include <windows.h>

namespace lsw::agent::install {

DownloadExtractJob::DownloadExtractJob(std::string id, std::string name, NotificationQueue& queue,
                                       std::string url, std::string extract_to, std::string script)
    : InstallJob(std::move(id), std::move(name), queue), url_(std::move(url)),
      extract_to_(std::move(extract_to)), script_(std::move(script)) {}

void DownloadExtractJob::run() {
    // Empty target = a job-scoped directory we own and clean up. A
    // caller-supplied path is taken at face value: created if missing,
    // never deleted.
    const bool target_is_temp = extract_to_.empty();
    const std::wstring extract_dir = target_is_temp ? WorkDir::staged_path(id_, L"-out")
                                                    : util::TextEncoding::to_wide(extract_to_);

    const auto downloaded = download(url_);
    if (!downloaded) {
        return;  // terminal notification already posted
    }
    const std::wstring& archive = *downloaded;

    DirectoryMaker::make_path(extract_dir);
    report_status("extract", "extracting to " + util::TextEncoding::to_utf8(extract_dir));

    // tar.exe is the built-in BSD libarchive (Win10 1803+): it groks
    // .zip / .tar / .tar.gz / .tar.xz with nothing else installed.
    const std::wstring tar_cmd = L"tar.exe -xf \"" + archive + L"\" -C \"" + extract_dir + L"\"";
    const auto extract = ProcessRunner::run(tar_cmd);
    if (!extract.completed || extract.exit_code != 0) {
        LSW_AGENT_ERROR("installer: extract failed job={} err={} exit={}", id_, extract.error,
                        extract.exit_code);
        ::DeleteFileW(archive.c_str());
        report_done("extract", false, extract.exit_code, exit_message(extract, "tar"));
        return;
    }
    // The extracted contents are what matters now.
    ::DeleteFileW(archive.c_str());

    if (!script_.empty() && !run_follow_up_script(extract_dir, archive)) {
        if (target_is_temp) {
            DirectoryMaker::remove_tree(extract_dir);
        }
        return;  // terminal notification already posted
    }

    if (target_is_temp) {
        DirectoryMaker::remove_tree(extract_dir);
    }
    LSW_AGENT_INFO("installer: done job={} ok=true", id_);
    report_done(script_.empty() ? "extract" : "script", true, 0, {});
}

bool DownloadExtractJob::run_follow_up_script(const std::wstring& extract_dir,
                                              const std::wstring& archive_path) {
    report_status("script", "running follow-up script");

    // Prepend the two env vars so the script can move things into place
    // without hard-coding the staged paths.
    std::string wrapper = "@echo off\r\n";
    wrapper += "set LSW_EXTRACT_DIR=" + util::TextEncoding::to_utf8(extract_dir) + "\r\n";
    wrapper += "set LSW_DOWNLOAD_PATH=" + util::TextEncoding::to_utf8(archive_path) + "\r\n";
    wrapper += script_;

    const ScriptFile file{id_, L"-post.cmd", wrapper};
    if (!file.ok()) {
        LSW_AGENT_ERROR("installer: wrapper write failed job={} err={}", id_, file.error());
        report_done("script", false, 0, file.error());
        return false;
    }

    const auto outcome = ProcessRunner::run(file.command_line());
    if (!outcome.completed || outcome.exit_code != 0) {
        LSW_AGENT_ERROR("installer: script failed job={} exit={} err={}", id_, outcome.exit_code,
                        outcome.error);
        report_done("script", false, outcome.exit_code, exit_message(outcome, "script"));
        return false;
    }
    return true;
}

}  // namespace lsw::agent::install
