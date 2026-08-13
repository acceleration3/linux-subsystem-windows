#include "lsw/agent/install/download_extract_job.hpp"

#include "lsw/agent/install/directory_maker.hpp"
#include "lsw/agent/install/http_downloader.hpp"
#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/install/script_file.hpp"
#include "lsw/agent/install/url_file_name.hpp"
#include "lsw/agent/install/work_dir.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <windows.h>

#include <utility>

namespace lsw::agent::install {

DownloadExtractJob::DownloadExtractJob(std::string id, std::string name,
                                       NotificationQueue& queue, std::string url,
                                       std::string extract_to, std::string script)
    : InstallJob(std::move(id), std::move(name), queue), url_(std::move(url)),
      extract_to_(std::move(extract_to)), script_(std::move(script)) {}

void DownloadExtractJob::run() {
    const std::wstring wurl = util::TextEncoding::to_wide(url_);
    const std::wstring archive = WorkDir::staged_path(id_, L"-" + UrlFileName::of(wurl));

    // Empty target = a job-scoped directory we own and clean up. A
    // caller-supplied path is taken at face value: created if missing,
    // never deleted.
    const bool target_is_temp = extract_to_.empty();
    const std::wstring extract_dir =
        target_is_temp ? WorkDir::staged_path(id_, L"-out")
                       : util::TextEncoding::to_wide(extract_to_);

    report_status("download", "starting download of " + url_);

    std::string error;
    const bool downloaded = HttpDownloader::fetch(
        wurl, archive,
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

    DirectoryMaker::make_path(extract_dir);
    report_status("extract", "extracting to " + util::TextEncoding::to_utf8(extract_dir));

    // tar.exe is the built-in BSD libarchive (Win10 1803+): it groks
    // .zip / .tar / .tar.gz / .tar.xz with nothing else installed.
    const std::wstring tar_cmd =
        L"tar.exe -xf \"" + archive + L"\" -C \"" + extract_dir + L"\"";
    const auto extract = ProcessRunner::run(tar_cmd);
    if (!extract.completed || extract.exit_code != 0) {
        LSW_AGENT_ERROR(std::string{"installer: extract failed job="} + id_
                        + " err=" + extract.error
                        + " exit=" + std::to_string(extract.exit_code));
        ::DeleteFileW(archive.c_str());
        report_done("extract", false, extract.exit_code,
                    extract.error.empty()
                        ? "tar exit code " + std::to_string(extract.exit_code)
                        : extract.error);
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
    LSW_AGENT_INFO(std::string{"installer: done job="} + id_ + " ok=true");
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
        LSW_AGENT_ERROR(std::string{"installer: wrapper write failed job="} + id_
                        + " err=" + file.error());
        report_done("script", false, 0, file.error());
        return false;
    }

    const auto outcome = ProcessRunner::run(file.command_line());
    if (!outcome.completed || outcome.exit_code != 0) {
        LSW_AGENT_ERROR(std::string{"installer: script failed job="} + id_
                        + " exit=" + std::to_string(outcome.exit_code)
                        + " err=" + outcome.error);
        report_done("script", false, outcome.exit_code,
                    outcome.error.empty()
                        ? "script exit code " + std::to_string(outcome.exit_code)
                        : outcome.error);
        return false;
    }
    return true;
}

}  // namespace lsw::agent::install
