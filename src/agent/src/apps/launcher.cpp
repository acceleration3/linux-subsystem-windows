#include "lsw/agent/apps/launcher.hpp"

#include "lsw/agent/apps/guest_drivers.hpp"
#include "lsw/agent/apps/launch_group.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/process.hpp"
#include "lsw/agent/win/shell_link.hpp"

#include <string>
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

}  // namespace

bool Launcher::launch(const std::string& app_id, const std::string& extra_args,
                      const std::vector<EnvVar>& env, unsigned long* pid_out,
                      std::string* error_out) {
    if (pid_out != nullptr) {
        *pid_out = 0;
    }
    LSW_AGENT_INFO("launch: app_id={}", app_id);

    // Resolve .lnk → real exe + args + working dir. For raw .exe /
    // .bat paths we just hand the path straight to CreateProcessW.
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
    // treatment in case they were typed with variables too.
    path = win::Environment::expand(path);
    working_dir = win::Environment::expand(working_dir);
    args = win::Environment::expand(args);

    // Default the working directory to the executable's own folder.
    //
    // This is what Explorer does, and applications assume it: anything
    // that opens a data file by a relative name — a config, an archive,
    // a save — reads it relative to the working directory, not to the
    // image. Launched with none, the child inherits the agent's, which
    // is C:\ProgramData\LSW, and an application that finds none of its
    // own files there fails in whatever way it fails when its data is
    // missing. Touhou 6 gets far enough to try creating a Direct3D
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

    // Quote the exe path so spaces work; append args.
    std::wstring cmdline = win::Process::quote(path);
    if (!args.empty()) {
        cmdline += L" ";
        cmdline += args;
    }
    // The configured arguments come last, after whatever a .lnk
    // carried, so an app config can add to a shortcut rather than
    // having to restate it.
    if (const std::wstring extra =
            win::Environment::expand(util::TextEncoding::to_wide(extra_args));
        !extra.empty()) {
        cmdline += L" ";
        cmdline += extra;
    }

    // The drivers go into the tree of the RESOLVED executable, which is
    // the only path any of the loaders will search — a .lnk's own
    // directory is not on it. That is why this is here rather than in
    // the RPC handler: the handler has the shortcut, this has the image.
    //
    // Before CreateProcessW, because the environment the loaders read is
    // built from what this returns and a child cannot be told about a
    // driver staged after it started.
    //
    // A failure is logged and the launch goes ahead. Without the
    // drivers the application finds whatever Windows has, which on a
    // guest with no GPU means WARP: slow, and far better than an
    // application that refuses to start.
    std::vector<EnvVar> launch_env = env;
    std::string driver_err;
    GuestDrivers::Staging staging =
        GuestDrivers::stage_for(util::TextEncoding::to_utf8(path), &launch_env, &driver_err);
    if (!staging.complete) {
        LSW_AGENT_WARN("launch: guest drivers not fully staged: {}", driver_err);
    }

    // Empty unless this launch overrides something, in which case the
    // child gets a copy of ours with the overrides applied. The app
    // config's own overrides come first, so a user who sets one of
    // these by hand wins over what was just staged.
    win::SpawnOptions options;
    options.application = path;
    options.command_line = cmdline;
    options.environment = win::Environment::block_with(launch_env);
    options.working_dir = working_dir;
    if (!launch_env.empty()) {
        std::string names;
        for (const auto& [name, value] : launch_env) {
            names += (names.empty() ? "" : " ") + name;
        }
        LSW_AGENT_INFO("launch: environment overrides: {}", names);
    }

    // The agent runs AS THE USER, in their interactive session (the
    // lsw-agent scheduled task, at the wizard user's logon; the account
    // is a local admin). So a plain CreateProcessW lands the app on the
    // user's real desktop with the agent's elevated-admin token. No
    // session bridging, no token gymnastics.
    //
    // CREATE_NO_WINDOW so the child doesn't briefly flash a console
    // window for non-GUI exes; CREATE_UNICODE_ENVIRONMENT because we
    // implicitly inherit our (UTF-16) environment.
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
        // Same shell fallback as the user-session path: not a plain
        // PE (alias / packaged / document) → let the shell open it.
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
        LSW_AGENT_WARN("launch: direct spawn failed (GetLastError={}) — retrying via shell "
                       "(explorer)",
                       win::ErrorText::win32(err));
        // Nothing will be tracked through the shell, so nothing will
        // ever retire and clean these up. Take them back now: without
        // the environment the loaders need, the drivers left in place
        // would do nothing but sit there.
        GuestDrivers::unstage(staging);
        win::SpawnOptions shell;
        shell.application = win::Environment::expand(L"%windir%\\explorer.exe");
        shell.command_line =
            win::Process::quote(shell.application) + L" " + win::Process::quote(path);
        shell.creation_flags = CREATE_UNICODE_ENVIRONMENT;
        DWORD shell_err = 0;
        if (!win::Process::spawn(shell, &shell_err)) {
            set_err(error_out,
                    "launch failed: direct CreateProcessW err=" + win::ErrorText::win32(err) +
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
    (void)LaunchGroup::adopt(spawned->process.get(), spawned->pid, std::move(staging));
    ::ResumeThread(spawned->thread.get());

    LSW_AGENT_INFO("launch: spawned pid={}", spawned->pid);
    if (pid_out != nullptr) {
        *pid_out = spawned->pid;
    }
    return true;
}

}  // namespace lsw::agent::apps
