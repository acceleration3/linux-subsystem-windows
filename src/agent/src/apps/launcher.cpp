#include "lsw/agent/apps/launcher.hpp"

#include "lsw/agent/apps/launch_group.hpp"
#include "lsw/agent/apps/launch_command.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/process.hpp"
#include "lsw/agent/win/process_info.hpp"
#include "lsw/agent/win/run_as.hpp"
#include "lsw/agent/win/shell_link.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>

namespace lsw::agent::apps {

namespace {

void set_err(std::string* error_out, const std::string& s) {
    LSW_AGENT_ERROR("launch: {}", s);
    if (error_out != nullptr) {
        *error_out = s;
    }
}

std::optional<unsigned long> existing_process(const std::wstring& path, const win::RunAs& as) {
    const std::wstring image = util::WideString::lower(path);
    std::optional<unsigned long> chosen;
    auto oldest = std::numeric_limits<std::uint64_t>::max();
    for (const auto& entry : win::ProcessInfo::snapshot()) {
        DWORD session = 0;
        if (!::ProcessIdToSessionId(entry.pid, &session) || session != as.session())
            continue;
        const win::UniqueHandle process{
            ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.pid)};
        if (!process ||
            util::WideString::lower(win::ProcessInfo::image_path(process.get())) != image)
            continue;
        DWORD exit_code = 0;
        if (!::GetExitCodeProcess(process.get(), &exit_code) || exit_code != STILL_ACTIVE)
            continue;
        win::UniqueHandle token;
        TOKEN_ELEVATION elevation{};
        DWORD bytes = 0;
        if (!::OpenProcessToken(process.get(), TOKEN_QUERY, token.put()) ||
            !::GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation),
                                   &bytes) ||
            (elevation.TokenIsElevated != 0) != as.elevated())
            continue;
        // The oldest image is normally the browser/application, not one of its
        // later same-image renderer/helper processes that has no windows.
        const auto created = win::ProcessInfo::creation_time(process.get());
        if (created && *created < oldest) {
            chosen = entry.pid;
            oldest = *created;
        }
    }
    return chosen;
}

bool has_application_window(unsigned long pid, const std::vector<unsigned long>& visible_pids) {
    const auto processes = win::ProcessInfo::snapshot();
    std::unordered_set<unsigned long> family{pid};
    for (std::size_t pass = 0; pass < processes.size(); ++pass) {
        bool grew = false;
        for (const auto& process : processes) {
            if (family.contains(process.parent) && family.insert(process.pid).second)
                grew = true;
        }
        if (!grew)
            break;
    }
    return std::any_of(visible_pids.begin(), visible_pids.end(),
                       [&](unsigned long candidate) { return family.contains(candidate); });
}

}  // namespace

bool Launcher::launch(const std::string& app_id, const std::string& extra_args,
                      const std::vector<EnvVar>& env, const win::RunAs& as, unsigned long* pid_out,
                      std::string* error_out, Mode mode,
                      const std::vector<unsigned long>& visible_pids) {
    if (pid_out != nullptr) {
        *pid_out = 0;
    }
    LSW_AGENT_INFO("launch: app_id={} as {}", app_id, as.describe());

    // Resolve .lnk → real exe + args + working dir.
    std::wstring path = util::TextEncoding::to_wide(app_id);
    std::wstring args;
    std::wstring working_dir;
    if (util::WideString::iends_with(path, L".lnk")) {
        // Raw: the path as stored, variables and all — they are
        // expanded just below, with everything else.
        const auto link = win::ShellLink::resolve(path, win::ShellLink::PathMode::Raw);
        if (!link || link->path.empty()) {
            set_err(error_out,
                    "lnk_resolve failed (IShellLink load/GetPath returned empty) for " + app_id);
            return false;
        }
        LSW_AGENT_INFO("launch: lnk_resolve target={} args={} working_dir={}", link->path,
                       link->args, link->working_dir);
        path = link->path;
        args = link->args;
        working_dir = link->working_dir;
    }
    // A .lnk's raw path often contains unexpanded variables like
    // ``%windir%\system32\charmap.exe``. CreateProcess does not expand
    // these in the application name argument, so the launch fails with
    // ERROR_PATH_NOT_FOUND (3). Manager-supplied raw paths get the same
    // treatment in case they were typed with variables too. Expanded
    // against the identity the process will run as: %APPDATA% is the
    // user's, not the service's.
    path = as.expand(path);
    working_dir = as.expand(working_dir);
    args = as.expand(args);
    // Never consume a document/profile argument or pretend environment overrides
    // were applied to an existing process. Match the full image, not its basename.
    std::optional<unsigned long> existing;
    if (mode == Mode::Open && args.empty() && extra_args.empty() && env.empty() &&
        util::WideString::iends_with(path, L".exe")) {
        existing = existing_process(path, as);
        if (existing && has_application_window(*existing, visible_pids)) {
            LSW_AGENT_INFO("launch: reusing pid={} for {}", *existing, app_id);
            if (pid_out)
                *pid_out = *existing;
            return true;
        }
        // A tray-only process needs its normal launch/IPC handoff to reopen.
        // Keep mirroring the original process, not the short-lived activation stub.
    }

    // Default the working directory to the executable's own folder.
    //
    // This is what Explorer does, and applications assume it: anything
    // that opens a data file by a relative name — a config, an archive,
    // a save — reads it relative to the working directory, not to the
    // image. Launched with none, the child inherits the agent's, which
    // for a service is C:\Windows\System32, and an application that
    // finds none of its own files there fails in whatever way it fails
    // when its data is missing. Touhou 6 gets far enough to try creating a Direct3D
    // device and then reports that every mode failed, which names
    // neither the missing config nor this.
    //
    // A .lnk that carries its own working directory keeps it: the
    // shortcut's author chose it deliberately.
    if (working_dir.empty()) {
        if (auto dir = util::WideString::parent_dir(path); dir && !dir->empty()) {
            working_dir = std::move(*dir);
        }
    }
    LSW_AGENT_DEBUG("launch: expanded path={} working_dir={}", path, working_dir);

    // The configured arguments come last, after whatever a .lnk
    // carried, so an app config can add to a shortcut rather than
    // having to restate it.
    if (const std::wstring extra = as.expand(util::TextEncoding::to_wide(extra_args));
        !extra.empty()) {
        if (!args.empty()) {
            args += L" ";
        }
        args += extra;
    }
    const LaunchCommand command(path, as.expand(L"%windir%"), args);
    const auto& cmdline = command.command_line;

    // The environment ``as`` gets, with the overrides applied. For the
    // agent itself with nothing to override that is empty, and the child
    // inherits ours.
    win::SpawnOptions options;
    options.application = command.application;
    options.command_line = cmdline;
    try {
        options.environment = as.environment(env);
        as.apply(options);
    } catch (const std::exception& e) {
        set_err(error_out, std::string{"launch: cannot start a process as "} + as.describe() +
                               ": " + e.what());
        return false;
    }
    options.working_dir = working_dir;
    if (!env.empty()) {
        std::string names;
        for (const auto& [name, value] : env) {
            names += (names.empty() ? "" : " ") + name;
        }
        LSW_AGENT_INFO("launch: environment overrides: {}", names);
    }

    // ``as.apply`` has pointed the spawn at the user's token and their
    // session's interactive desktop (CreateProcessAsUserW), so the app
    // lands on the user's real desktop.
    //
    // CREATE_NO_WINDOW so the child doesn't briefly flash a console
    // window for non-GUI exes; CREATE_UNICODE_ENVIRONMENT because every
    // environment block here is UTF-16.
    //
    // CREATE_SUSPENDED so the process can be put in its launch's job
    // before it executes an instruction. A launcher stub can spawn the
    // real application in the first milliseconds of its life, and a
    // child that starts before the assignment is outside the job for
    // good — which is exactly the process we most need to track.
    options.creation_flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED;
    DWORD err = 0;
    const auto spawned = win::Process::spawn(options, &err);
    if (!spawned) {
        // Not a plain PE (alias / packaged / document): let the shell
        // open it.
        //
        // Environment overrides do not survive this: the shell starts
        // the application from its own process, with its own
        // environment. Nothing can be done about that from here — a
        // packaged app is started by the shell by definition — so it is
        // logged rather than silently ignored.
        if (!env.empty()) {
            LSW_AGENT_WARN("launch: environment overrides are lost on the shell "
                           "fallback path (packaged app / alias / document)");
        }
        LSW_AGENT_WARN("launch: direct spawn failed (GetLastError={}); retrying via shell "
                       "(explorer)",
                       win::ErrorText::win32(err));
        win::SpawnOptions shell;
        shell.application = as.expand(L"%windir%\\explorer.exe");
        shell.command_line =
            win::Process::quote(shell.application) + L" " + win::Process::quote(path);
        shell.creation_flags = CREATE_UNICODE_ENVIRONMENT;
        try {
            as.apply(shell);
        } catch (const std::exception& e) {
            set_err(error_out, std::string{"launch: shell fallback as "} + as.describe() +
                                   " failed: " + e.what());
            return false;
        }
        DWORD shell_err = 0;
        if (!win::Process::spawn(shell, &shell_err)) {
            set_err(error_out,
                    "launch failed: direct CreateProcess err=" + win::ErrorText::win32(err) +
                        ", shell fallback err=" + win::ErrorText::win32(shell_err) +
                        ", cmdline=" + util::TextEncoding::to_utf8(cmdline));
            return false;
        }
        LSW_AGENT_INFO("launch: dispatched via shell; PID not tracked (shell-owned)");
        if (pid_out != nullptr) {
            *pid_out = 0;
        }
        return true;
    }
    // Job first, then let it run. Whether the adoption worked or not the
    // thread has to be resumed — a process left suspended is a process
    // that never starts.
    (void)LaunchGroup::adopt(spawned->process.get(), spawned->pid);
    ::ResumeThread(spawned->thread.get());

    LSW_AGENT_INFO("launch: spawned pid={}", spawned->pid);
    if (pid_out != nullptr) {
        *pid_out = existing.value_or(spawned->pid);
    }
    return true;
}

}  // namespace lsw::agent::apps
