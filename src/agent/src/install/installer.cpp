#include "lsw/agent/install/installer.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
// clang-format on

#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

namespace lsw::agent::install {

namespace {

constexpr size_t kMaxPendingProgress = 4096;

[[nodiscard]] std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                        static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          out.data(), n);
    return out;
}

[[nodiscard]] std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                        static_cast<int>(w.size()), nullptr, 0,
                                        nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

// Pull the basename out of a URL path so the downloaded temp file
// keeps its real extension (Windows ShellExecute behaviour depends on
// the extension). Falls back to ``download.bin``.
[[nodiscard]] std::wstring url_basename(const std::wstring& url) {
    auto q = url.find_first_of(L"?#");
    std::wstring path = (q == std::wstring::npos) ? url : url.substr(0, q);
    auto slash = path.find_last_of(L'/');
    std::wstring base = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    if (base.empty()) {
        return L"download.bin";
    }
    return base;
}

// Where staged job files live (the temp .cmd, downloaded installers,
// extracted archives, follow-up scripts). NOT %TEMP%: the agent is
// SYSTEM, so GetTempPath returns
// C:\Windows\System32\config\systemprofile\AppData\Local\Temp — a
// directory the wizard user (which we now run setup children as)
// cannot read, so cmd.exe/tar fail with "access denied" / exit 1.
// C:\ProgramData\LSW carries an inheritable Users:Modify ACE (granted
// by the autounattend RunSynchronous), so a "work" subdir under it is
// readable+writable by BOTH SYSTEM and the user. Created best-effort
// every call (idempotent).
[[nodiscard]] std::wstring temp_dir() {
    const wchar_t* base = L"C:\\ProgramData\\LSW";
    ::CreateDirectoryW(base, nullptr);
    const std::wstring work = std::wstring{base} + L"\\work";
    ::CreateDirectoryW(work.c_str(), nullptr);
    return work + L"\\";
}

}  // namespace

Installer& Installer::instance() {
    static Installer inst;
    return inst;
}

std::string Installer::make_job_id() {
    std::lock_guard<std::mutex> lock(mu_);
    return "job-" + std::to_string(next_id_++);
}

void Installer::post(Notification n) {
    std::lock_guard<std::mutex> lock(mu_);
    // Cap progress notifications so a stuck/disconnected manager
    // doesn't grow the queue unboundedly during a slow download. Done
    // events are always preserved — terminal state must reach the
    // manager once it reconnects.
    if (n.kind == Kind::Progress) {
        size_t progress_count = 0;
        for (const auto& p : pending_) {
            if (p.kind == Kind::Progress) {
                ++progress_count;
            }
        }
        if (progress_count >= kMaxPendingProgress) {
            return;
        }
    }
    pending_.push_back(std::move(n));
}

std::vector<Installer::Notification> Installer::collect_pending() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Notification> out;
    out.swap(pending_);
    return out;
}

void Installer::requeue_front(std::vector<Notification> notes) {
    if (notes.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    // Prepend, preserving order: [notes...] + [whatever arrived since].
    // Drop only excess Progress notes if we'd blow the cap — Done
    // events are always kept (terminal state must survive a reconnect).
    notes.reserve(notes.size() + pending_.size());
    for (auto& p : pending_) {
        notes.push_back(std::move(p));
    }
    pending_.clear();
    size_t progress_count = 0;
    for (auto& n : notes) {
        if (n.kind == Kind::Progress) {
            if (progress_count >= kMaxPendingProgress) {
                continue;
            }
            ++progress_count;
        }
        pending_.push_back(std::move(n));
    }
}

std::string Installer::start_download_install(const std::string& name,
                                              const std::string& url,
                                              const std::string& args) {
    const std::string job_id = make_job_id();
    LSW_AGENT_INFO(std::string{"installer: start download_install job="} + job_id
                   + " name=" + name + " url=" + url);
    std::thread t(&Installer::run_download_install_job, this, job_id, name, url, args);
    t.detach();
    return job_id;
}

std::string Installer::start_run_script(const std::string& name,
                                        const std::string& script) {
    const std::string job_id = make_job_id();
    LSW_AGENT_INFO(std::string{"installer: start run_script job="} + job_id
                   + " name=" + name + " bytes=" + std::to_string(script.size()));
    std::thread t(&Installer::run_script_job, this, job_id, name, script);
    t.detach();
    return job_id;
}

std::string Installer::start_download_extract(const std::string& name,
                                              const std::string& url,
                                              const std::string& extract_to,
                                              const std::string& script) {
    const std::string job_id = make_job_id();
    LSW_AGENT_INFO(std::string{"installer: start download_extract job="} + job_id
                   + " name=" + name + " url=" + url + " extract_to=" + extract_to
                   + " script_bytes=" + std::to_string(script.size()));
    std::thread t(&Installer::run_download_extract_job, this, job_id, name, url,
                  extract_to, script);
    t.detach();
    return job_id;
}

bool Installer::http_download(const std::wstring& url, const std::wstring& dest_path,
                              const std::string& job_id, const std::string& name,
                              std::string& error_out) {
    // Crack the URL into host / path / scheme so we can drive WinHTTP
    // ourselves (WinHttpOpen, WinHttpConnect, WinHttpOpenRequest).
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = std::size(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = std::size(path);
    if (!::WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        error_out = "WinHttpCrackUrl failed";
        return false;
    }

    HINTERNET session = ::WinHttpOpen(L"lsw-agent/1.0",
                                       WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        error_out = "WinHttpOpen failed err=" + std::to_string(::GetLastError());
        return false;
    }
    HINTERNET conn = ::WinHttpConnect(session, host, uc.nPort, 0);
    if (conn == nullptr) {
        error_out = "WinHttpConnect failed err=" + std::to_string(::GetLastError());
        ::WinHttpCloseHandle(session);
        return false;
    }
    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = ::WinHttpOpenRequest(conn, L"GET", path, nullptr,
                                          WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (req == nullptr) {
        error_out = "WinHttpOpenRequest failed err=" + std::to_string(::GetLastError());
        ::WinHttpCloseHandle(conn);
        ::WinHttpCloseHandle(session);
        return false;
    }
    if (!::WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !::WinHttpReceiveResponse(req, nullptr)) {
        error_out = "WinHttpSendRequest/ReceiveResponse failed err="
                    + std::to_string(::GetLastError());
        ::WinHttpCloseHandle(req);
        ::WinHttpCloseHandle(conn);
        ::WinHttpCloseHandle(session);
        return false;
    }

    // Status check — fail loudly on 4xx/5xx.
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    ::WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          nullptr, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        error_out = "HTTP status " + std::to_string(status);
        ::WinHttpCloseHandle(req);
        ::WinHttpCloseHandle(conn);
        ::WinHttpCloseHandle(session);
        return false;
    }

    // Content-Length is optional; many CDNs use chunked encoding.
    std::uint64_t total = 0;
    {
        wchar_t len_buf[64] = {};
        DWORD len_size = sizeof(len_buf);
        if (::WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, len_buf,
                                   &len_size, WINHTTP_NO_HEADER_INDEX)) {
            total = ::_wcstoui64(len_buf, nullptr, 10);
        }
    }

    HANDLE out = ::CreateFileW(dest_path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        error_out = "CreateFileW(" + wide_to_utf8(dest_path) + ") failed err="
                    + std::to_string(::GetLastError());
        ::WinHttpCloseHandle(req);
        ::WinHttpCloseHandle(conn);
        ::WinHttpCloseHandle(session);
        return false;
    }

    std::uint64_t received = 0;
    auto last_post = ::GetTickCount64();
    std::vector<char> buf(64 * 1024);
    while (true) {
        DWORD got = 0;
        if (!::WinHttpReadData(req, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
            error_out = "WinHttpReadData failed err=" + std::to_string(::GetLastError());
            ::CloseHandle(out);
            ::DeleteFileW(dest_path.c_str());
            ::WinHttpCloseHandle(req);
            ::WinHttpCloseHandle(conn);
            ::WinHttpCloseHandle(session);
            return false;
        }
        if (got == 0) {
            break;  // EOF
        }
        DWORD wrote = 0;
        if (!::WriteFile(out, buf.data(), got, &wrote, nullptr) || wrote != got) {
            error_out = "WriteFile failed err=" + std::to_string(::GetLastError());
            ::CloseHandle(out);
            ::DeleteFileW(dest_path.c_str());
            ::WinHttpCloseHandle(req);
            ::WinHttpCloseHandle(conn);
            ::WinHttpCloseHandle(session);
            return false;
        }
        received += got;
        // Throttle progress notifications to ~10/sec.
        const auto now = ::GetTickCount64();
        if (now - last_post >= 100) {
            last_post = now;
            Notification n;
            n.kind = Kind::Progress;
            n.job_id = job_id;
            n.name = name;
            n.phase = "download";
            n.bytes = received;
            n.total_bytes = total;
            post(std::move(n));
        }
    }
    ::CloseHandle(out);
    ::WinHttpCloseHandle(req);
    ::WinHttpCloseHandle(conn);
    ::WinHttpCloseHandle(session);

    // Final 100% notification so the UI lands on a stable end state
    // before "install" phase starts.
    Notification n;
    n.kind = Kind::Progress;
    n.job_id = job_id;
    n.name = name;
    n.phase = "download";
    n.bytes = received;
    n.total_bytes = total > 0 ? total : received;
    post(std::move(n));
    return true;
}

bool Installer::run_process_and_wait(const std::wstring& cmd_line, int& exit_code_out,
                                     std::string& error_out,
                                     unsigned long timeout_ms) {
    // The agent runs AS THE USER, in-session, elevated-admin
    // (HKLM\Run autostart) — so a plain CreateProcessW is all we
    // need; the
    // child inherits our elevated token and interactive desktop. No
    // WTSQueryUserToken / CreateProcessAsUserW / session bridging.
    //
    // Job Object purely so a timeout can TerminateJobObject the WHOLE
    // tree (a wedged installer may be a grandchild of our cmd.exe).
    // NOT JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: the agent-redeploy
    // run_script intentionally `taskkill`s the agent mid-script then
    // moves+relaunches; kill-on-close would nuke the still-running
    // swap cmd. The timeout path below TerminateJobObject's explicitly.
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    // CreateProcessW writes back into the command-line buffer, so it
    // must be mutable. Copy into a local vector first.
    std::vector<wchar_t> mutable_cmd(cmd_line.begin(), cmd_line.end());
    mutable_cmd.push_back(L'\0');

    // CREATE_SUSPENDED so we can assign the process to the job BEFORE
    // it runs (and spawns descendants) — otherwise an early grandchild
    // could escape the job.
    if (!::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                          &si, &pi)) {
        error_out = std::string{"CreateProcessW failed err="}
                    + std::to_string(::GetLastError());
        if (job != nullptr) {
            ::CloseHandle(job);
        }
        return false;
    }
    if (job != nullptr) {
        ::AssignProcessToJobObject(job, pi.hProcess);
    }
    ::ResumeThread(pi.hThread);

    LSW_AGENT_INFO(std::string{"installer: spawned pid="}
                   + std::to_string(pi.dwProcessId)
                   + " timeout="
                   + (timeout_ms == kNoTimeout
                          ? std::string{"none"}
                          : (std::to_string(timeout_ms / 1000) + "s"))
                   + " cmd=" + wide_to_utf8(cmd_line));
    const DWORD wait = ::WaitForSingleObject(pi.hProcess, timeout_ms);
    bool ok = true;
    if (wait == WAIT_TIMEOUT) {
        LSW_AGENT_ERROR(std::string{"installer: pid="}
                        + std::to_string(pi.dwProcessId) + " TIMED OUT after "
                        + std::to_string(timeout_ms / 1000)
                        + "s — killing process tree");
        if (job != nullptr) {
            ::TerminateJobObject(job, 1);
        } else {
            ::TerminateProcess(pi.hProcess, 1);
        }
        ::WaitForSingleObject(pi.hProcess, 5000);
        error_out = "timed out after " + std::to_string(timeout_ms / 1000)
                    + "s (process tree killed)";
        ok = false;
    }
    DWORD code = 0;
    ::GetExitCodeProcess(pi.hProcess, &code);
    if (ok) {
        LSW_AGENT_INFO(std::string{"installer: pid="}
                       + std::to_string(pi.dwProcessId) + " exited code="
                       + std::to_string(code));
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    if (job != nullptr) {
        ::CloseHandle(job);
    }
    exit_code_out = static_cast<int>(code);
    return ok;
}

void Installer::run_download_install_job(std::string job_id, std::string name,
                                         std::string url, std::string args) {
    const std::wstring wurl = utf8_to_wide(url);
    const std::wstring base = url_basename(wurl);
    const std::wstring dest = temp_dir() + L"lsw-" + utf8_to_wide(job_id) + L"-" + base;

    {
        Notification n;
        n.kind = Kind::Progress;
        n.job_id = job_id;
        n.name = name;
        n.phase = "download";
        n.message = "starting download of " + url;
        post(std::move(n));
    }

    std::string err;
    if (!http_download(wurl, dest, job_id, name, err)) {
        LSW_AGENT_ERROR(std::string{"installer: download failed job="} + job_id
                        + " url=" + url + " err=" + err);
        Notification d;
        d.kind = Kind::Done;
        d.job_id = job_id;
        d.name = name;
        d.phase = "download";
        d.ok = false;
        d.message = err;
        post(std::move(d));
        return;
    }

    {
        Notification n;
        n.kind = Kind::Progress;
        n.job_id = job_id;
        n.name = name;
        n.phase = "install";
        n.message = "launching installer";
        post(std::move(n));
    }

    // Compose ``"<path>" <args>``. Quoting the path handles spaces.
    std::wstring cmd_line = L"\"";
    cmd_line += dest;
    cmd_line += L"\"";
    if (!args.empty()) {
        cmd_line += L" ";
        cmd_line += utf8_to_wide(args);
    }
    int exit_code = 0;
    if (!run_process_and_wait(cmd_line, exit_code, err)) {
        LSW_AGENT_ERROR(std::string{"installer: spawn failed job="} + job_id
                        + " err=" + err);
        ::DeleteFileW(dest.c_str());
        Notification d;
        d.kind = Kind::Done;
        d.job_id = job_id;
        d.name = name;
        d.phase = "install";
        d.ok = false;
        d.message = err;
        post(std::move(d));
        return;
    }

    ::DeleteFileW(dest.c_str());

    Notification d;
    d.kind = Kind::Done;
    d.job_id = job_id;
    d.name = name;
    d.phase = "install";
    d.exit_code = exit_code;
    // 0 = success, 3010 = success-but-reboot-required (the standard
    // Windows installer convention). Anything else is a failure.
    d.ok = (exit_code == 0 || exit_code == 3010);
    if (!d.ok) {
        d.message = "installer exit code " + std::to_string(exit_code);
    }
    LSW_AGENT_INFO(std::string{"installer: done job="} + job_id
                   + " ok=" + (d.ok ? "true" : "false")
                   + " exit=" + std::to_string(exit_code));
    post(std::move(d));
}

void Installer::run_download_extract_job(std::string job_id, std::string name,
                                         std::string url, std::string extract_to,
                                         std::string script) {
    const std::wstring wurl = utf8_to_wide(url);
    const std::wstring base = url_basename(wurl);
    const std::wstring archive_path =
        temp_dir() + L"lsw-" + utf8_to_wide(job_id) + L"-" + base;

    // Resolve the extraction directory. Empty = create a job-scoped
    // temp dir we own (and clean up at the end). If the user supplies
    // a path it's taken at face value; we mkdir-p it but never delete.
    bool extract_to_is_temp = extract_to.empty();
    std::wstring wextract = extract_to_is_temp
                                ? (temp_dir() + L"lsw-" + utf8_to_wide(job_id) + L"-out")
                                : utf8_to_wide(extract_to);

    {
        Notification n;
        n.kind = Kind::Progress;
        n.job_id = job_id;
        n.name = name;
        n.phase = "download";
        n.message = "starting download of " + url;
        post(std::move(n));
    }

    std::string err;
    if (!http_download(wurl, archive_path, job_id, name, err)) {
        LSW_AGENT_ERROR(std::string{"installer: download failed job="} + job_id
                        + " url=" + url + " err=" + err);
        Notification d;
        d.kind = Kind::Done;
        d.job_id = job_id;
        d.name = name;
        d.phase = "download";
        d.ok = false;
        d.message = err;
        post(std::move(d));
        return;
    }

    // Make sure the extract dir exists. ``SHCreateDirectoryExW`` would
    // be nicer (recursive) but it needs shell32; CreateDirectoryW + a
    // tiny manual loop avoids the extra link dep.
    auto mkpath = [](const std::wstring& path) {
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == L'\\' || path[i] == L'/') {
                if (i == 0 || (i == 2 && path[1] == L':')) {
                    continue;  // skip drive root
                }
                ::CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
            }
        }
        ::CreateDirectoryW(path.c_str(), nullptr);
    };
    mkpath(wextract);

    {
        Notification n;
        n.kind = Kind::Progress;
        n.job_id = job_id;
        n.name = name;
        n.phase = "extract";
        n.message = "extracting to " + wide_to_utf8(wextract);
        post(std::move(n));
    }

    // Use the built-in tar.exe (BSD libarchive, Win10 1803+). It groks
    // .zip / .tar / .tar.gz / .tar.xz without needing a separate tool.
    std::wstring tar_cmd = L"tar.exe -xf \"";
    tar_cmd += archive_path;
    tar_cmd += L"\" -C \"";
    tar_cmd += wextract;
    tar_cmd += L"\"";
    int exit_code = 0;
    if (!run_process_and_wait(tar_cmd, exit_code, err) || exit_code != 0) {
        LSW_AGENT_ERROR(std::string{"installer: extract failed job="} + job_id
                        + " err=" + err
                        + " exit=" + std::to_string(exit_code));
        ::DeleteFileW(archive_path.c_str());
        Notification d;
        d.kind = Kind::Done;
        d.job_id = job_id;
        d.name = name;
        d.phase = "extract";
        d.ok = false;
        d.exit_code = exit_code;
        d.message = err.empty()
                        ? ("tar exit code " + std::to_string(exit_code))
                        : err;
        post(std::move(d));
        return;
    }

    // Archive itself is no longer needed; the extracted contents live
    // under ``wextract``.
    ::DeleteFileW(archive_path.c_str());

    // Optional follow-up script. The user supplies a regular .cmd
    // body; we wrap it with ``set LSW_EXTRACT_DIR=...`` / ``set
    // LSW_DOWNLOAD_PATH=...`` lines at the top so the script can move
    // / rename / copy without hard-coding the temp path.
    if (!script.empty()) {
        Notification n;
        n.kind = Kind::Progress;
        n.job_id = job_id;
        n.name = name;
        n.phase = "script";
        n.message = "running follow-up script";
        post(std::move(n));

        std::string wrapper = "@echo off\r\n";
        wrapper += "set LSW_EXTRACT_DIR=" + wide_to_utf8(wextract) + "\r\n";
        wrapper += "set LSW_DOWNLOAD_PATH=" + wide_to_utf8(archive_path) + "\r\n";
        for (size_t i = 0; i < script.size(); ++i) {
            const char c = script[i];
            if (c == '\n' && (i == 0 || script[i - 1] != '\r')) {
                wrapper.push_back('\r');
            }
            wrapper.push_back(c);
        }

        const std::wstring script_path =
            temp_dir() + L"lsw-" + utf8_to_wide(job_id) + L"-post.cmd";
        HANDLE h = ::CreateFileW(script_path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const auto e = std::string{"CreateFileW("} + wide_to_utf8(script_path)
                             + ") failed err=" + std::to_string(::GetLastError());
            LSW_AGENT_ERROR(std::string{"installer: wrapper write failed job="} + job_id
                            + " err=" + e);
            Notification d;
            d.kind = Kind::Done;
            d.job_id = job_id;
            d.name = name;
            d.phase = "script";
            d.ok = false;
            d.message = e;
            post(std::move(d));
            return;
        }
        DWORD wrote = 0;
        ::WriteFile(h, wrapper.data(), static_cast<DWORD>(wrapper.size()), &wrote, nullptr);
        ::CloseHandle(h);

        std::wstring cmd_line = L"cmd.exe /c \"";
        cmd_line += script_path;
        cmd_line += L"\"";
        const bool spawned = run_process_and_wait(cmd_line, exit_code, err);
        ::DeleteFileW(script_path.c_str());

        if (!spawned || exit_code != 0) {
            Notification d;
            d.kind = Kind::Done;
            d.job_id = job_id;
            d.name = name;
            d.phase = "script";
            d.ok = false;
            d.exit_code = exit_code;
            d.message = err.empty()
                            ? ("script exit code " + std::to_string(exit_code))
                            : err;
            LSW_AGENT_ERROR(std::string{"installer: script failed job="} + job_id
                            + " exit=" + std::to_string(exit_code)
                            + " err=" + err);
            post(std::move(d));
            // Don't fall through to "ok" — the script said no.
            if (extract_to_is_temp) {
                // Best-effort cleanup; ignore failures.
                std::wstring rm = L"cmd.exe /c rd /s /q \"" + wextract + L"\"";
                int rc = 0;
                std::string ignored;
                (void)run_process_and_wait(rm, rc, ignored);
            }
            return;
        }
    }

    if (extract_to_is_temp) {
        std::wstring rm = L"cmd.exe /c rd /s /q \"" + wextract + L"\"";
        int rc = 0;
        std::string ignored;
        (void)run_process_and_wait(rm, rc, ignored);
    }

    Notification d;
    d.kind = Kind::Done;
    d.job_id = job_id;
    d.name = name;
    d.phase = script.empty() ? "extract" : "script";
    d.ok = true;
    d.exit_code = 0;
    LSW_AGENT_INFO(std::string{"installer: done job="} + job_id + " ok=true");
    post(std::move(d));
}

void Installer::run_script_job(std::string job_id, std::string name,
                               std::string script) {
    const std::wstring path = temp_dir() + L"lsw-" + utf8_to_wide(job_id) + L".cmd";

    // Write CRLF — cmd.exe is sensitive to line endings on multi-line
    // ``if`` / ``for`` blocks.
    std::string crlf;
    crlf.reserve(script.size() + 8);
    for (size_t i = 0; i < script.size(); ++i) {
        const char c = script[i];
        if (c == '\n' && (i == 0 || script[i - 1] != '\r')) {
            crlf.push_back('\r');
        }
        crlf.push_back(c);
    }

    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const auto err = std::string{"CreateFileW("} + wide_to_utf8(path)
                          + ") failed err=" + std::to_string(::GetLastError());
        LSW_AGENT_ERROR(std::string{"installer: script write failed job="} + job_id
                        + " err=" + err);
        Notification d;
        d.kind = Kind::Done;
        d.job_id = job_id;
        d.name = name;
        d.phase = "script";
        d.ok = false;
        d.message = err;
        post(std::move(d));
        return;
    }
    DWORD wrote = 0;
    ::WriteFile(h, crlf.data(), static_cast<DWORD>(crlf.size()), &wrote, nullptr);
    ::CloseHandle(h);

    LSW_AGENT_INFO(std::string{"installer: run_script job="} + job_id
                   + " name='" + name + "' wrote " + std::to_string(wrote)
                   + " bytes to " + wide_to_utf8(path) + " — executing");

    {
        Notification n;
        n.kind = Kind::Progress;
        n.job_id = job_id;
        n.name = name;
        n.phase = "script";
        n.message = "running script";
        post(std::move(n));
    }

    std::wstring cmd_line = L"cmd.exe /c \"";
    cmd_line += path;
    cmd_line += L"\"";
    int exit_code = 0;
    std::string err;
    // Hard ceiling so a wedged child (historically the WiX-based
    // virtio-win-guest-tools.exe run as SYSTEM in session 0) can never
    // stall the whole setup phase forever. Well-behaved setup scripts
    // here finish in seconds-to-minutes; 30 min is comfortably above
    // any legitimate run. The step's own script bounds guest-tools
    // tighter and still exits 0, so this only fires on a true hang.
    static constexpr unsigned long kScriptTimeoutMs = 30UL * 60UL * 1000UL;
    const bool spawned = run_process_and_wait(cmd_line, exit_code, err,
                                              kScriptTimeoutMs);

    ::DeleteFileW(path.c_str());

    Notification d;
    d.kind = Kind::Done;
    d.job_id = job_id;
    d.name = name;
    d.phase = "script";
    if (!spawned) {
        d.ok = false;
        d.message = err;
    } else {
        d.exit_code = exit_code;
        d.ok = (exit_code == 0);
        if (!d.ok) {
            d.message = "script exit code " + std::to_string(exit_code);
        }
    }
    LSW_AGENT_INFO(std::string{"installer: done job="} + job_id
                   + " ok=" + (d.ok ? "true" : "false")
                   + " exit=" + std::to_string(exit_code));
    post(std::move(d));
}

}  // namespace lsw::agent::install
