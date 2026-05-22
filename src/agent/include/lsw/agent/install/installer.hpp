#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace lsw::agent::install {

// Async installer / script-runner used by the wizard's "Setting up the
// VM" phase. Two job kinds:
//
//   * ``download_install`` — HTTP(S) download the URL to a temp file
//     (WinHTTP), then ShellExecuteEx-run it with user-supplied silent
//     install args. Suitable for ``vc_redist.x64.exe /install /quiet``.
//   * ``run_script``       — write the inline batch text to a temp .cmd
//     and run via ``cmd /c``. Stdout+stderr are captured into the job's
//     output buffer.
//
// Jobs run on detached background threads. Progress is reported by
// posting Notifications onto an in-process queue; HostEndpoint drains
// the queue on its select tick and ships each one over the active
// manager connection as an agent-initiated frame.
//
// Notifications are intentionally fire-and-forget — the manager loses
// them if it isn't connected at the moment they're posted. The final
// ``done`` notification carries the terminal state; a manager that
// reconnects mid-job and missed earlier ``progress`` frames can still
// rely on ``done`` arriving once the job finishes (queue is unbounded
// for done events; bounded for progress to avoid memory blowup if the
// manager is gone).
class Installer {
public:
    // Notification kinds. ``progress`` may fire many times per job;
    // ``done`` fires exactly once per job and is the terminal frame.
    enum class Kind { Progress, Done };

    struct Notification {
        Kind kind = Kind::Progress;
        std::string job_id;
        std::string name;              // human-readable step label
        std::string phase;             // "download" | "install" | "script"
        std::uint64_t bytes = 0;       // bytes downloaded so far
        std::uint64_t total_bytes = 0; // 0 if unknown (chunked encoding)
        int exit_code = 0;             // installer/script exit code (done only)
        bool ok = false;               // job succeeded (done only)
        std::string message;           // free-form status / error text
    };

    [[nodiscard]] static Installer& instance();

    // Start a download-then-install job. ``url`` is downloaded to a
    // temp file (extension preserved from the URL path). Once the
    // download is complete the file is launched with ``args`` and the
    // job waits for it to exit. Returns the new job_id.
    [[nodiscard]] std::string start_download_install(const std::string& name,
                                                     const std::string& url,
                                                     const std::string& args);

    // Start a script job. ``script`` is written verbatim to a temp
    // .cmd file (CRLF-converted) and executed via ``cmd /c``. Returns
    // the new job_id. Runs in the agent's context — i.e. as the
    // elevated-admin wizard user, in-session (see autounattend Run key).
    [[nodiscard]] std::string start_run_script(const std::string& name,
                                               const std::string& script);

    // Start a download-then-extract job. The archive at ``url`` is
    // downloaded to a temp file, extracted via the built-in
    // ``tar.exe`` (Win10 1803+ ships it; handles .zip / .tar / .tar.gz
    // out of the box) into ``extract_to`` (or a temp dir if empty),
    // and finally — if ``script`` is non-empty — runs the script with
    // ``LSW_EXTRACT_DIR`` and ``LSW_DOWNLOAD_PATH`` env vars wired up
    // so it can move things into place. Returns the new job_id.
    [[nodiscard]] std::string start_download_extract(const std::string& name,
                                                     const std::string& url,
                                                     const std::string& extract_to,
                                                     const std::string& script);

    // Pop all pending notifications. Called by HostEndpoint on its
    // select-tick. Returns oldest-first.
    [[nodiscard]] std::vector<Notification> collect_pending();

    // Put undelivered notifications back at the FRONT of the queue,
    // preserving order, so the next connection redelivers them. Used
    // when a drain's send() fails mid-flight (e.g. virtio-win-guest-
    // tools resets the NetKVM NIC the RPC channel rides on): without
    // this a drained-but-unsent ``setup_done`` is lost and the wizard
    // waits forever. Progress notes are still capped on re-add.
    void requeue_front(std::vector<Notification> notes);

private:
    Installer() = default;
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;

    // Allocate a fresh job_id, monotonically increasing.
    [[nodiscard]] std::string make_job_id();

    // Append a notification onto the pending queue.
    void post(Notification n);

    // Worker entry points — run on detached threads.
    void run_download_install_job(std::string job_id, std::string name,
                                  std::string url, std::string args);
    void run_script_job(std::string job_id, std::string name, std::string script);
    void run_download_extract_job(std::string job_id, std::string name,
                                  std::string url, std::string extract_to,
                                  std::string script);

    // Helpers used by run_download_install_job.
    [[nodiscard]] bool http_download(const std::wstring& url,
                                     const std::wstring& dest_path,
                                     const std::string& job_id,
                                     const std::string& name,
                                     std::string& error_out);
    // Spawn ``cmd_line`` (as the agent — elevated-admin user,
    // in-session), wait up to ``timeout_ms`` (default: no limit). The
    // child is placed in a (NOT kill-on-close) Job Object so a timeout
    // can TerminateJobObject the whole tree, not just the direct
    // child. On timeout returns false with ``error_out`` set to a
    // "timed out" message so callers can distinguish it from a spawn
    // failure.
    // 0xFFFFFFFF == Win32 INFINITE; spelled literally so this header
    // need not pull in <windows.h>.
    static constexpr unsigned long kNoTimeout = 0xFFFFFFFFUL;
    [[nodiscard]] bool run_process_and_wait(const std::wstring& cmd_line,
                                            int& exit_code_out,
                                            std::string& error_out,
                                            unsigned long timeout_ms = kNoTimeout);

    std::mutex mu_;
    std::vector<Notification> pending_;
    std::uint64_t next_id_ = 1;
};

}  // namespace lsw::agent::install
