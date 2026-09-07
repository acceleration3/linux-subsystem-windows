#include "lsw/agent/apps/launcher.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include "lsw/agent/apps/guest_drivers.hpp"

#include "lsw/agent/apps/launch_group.hpp"
#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

namespace lsw::agent::apps {

namespace {

// The child's environment: ours, with ``overrides`` applied on top,
// laid out the way CreateProcessW wants it — NAME=value strings each
// NUL-terminated, the whole block terminated by a second NUL.
//
// Sorted case-insensitively by name because that is what the
// documentation asks of an environment block, and because a block that
// merely happens to work is the kind of thing that stops working on
// somebody else's machine.
//
// Empty when there is nothing to override, and the caller then passes
// nullptr and the child inherits ours directly — no copy, no chance of
// this code mangling an environment nobody asked it to touch.
std::vector<wchar_t> build_environment(const std::vector<Launcher::EnvVar>& overrides) {
    if (overrides.empty()) {
        return {};
    }
    auto upper = [](std::wstring s) {
        for (wchar_t& c : s) {
            c = static_cast<wchar_t>(::towupper(c));
        }
        return s;
    };

    // Existing variables, keyed by upper-cased name so an override
    // replaces a differently-cased one rather than adding a twin.
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> vars;  // KEY -> (name, value)
    if (wchar_t* block = ::GetEnvironmentStringsW(); block != nullptr) {
        for (const wchar_t* p = block; *p != L'\0';) {
            const std::wstring entry(p);
            p += entry.size() + 1;
            // Skip the "=C:=C:\..." drive-current-directory entries:
            // they start with '=' and are the shell's business.
            const auto eq = entry.find(L'=', 1);
            if (entry.empty() || entry[0] == L'=' || eq == std::wstring::npos) {
                continue;
            }
            const std::wstring name = entry.substr(0, eq);
            vars[upper(name)] = {name, entry.substr(eq + 1)};
        }
        ::FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : overrides) {
        const std::wstring wname = util::TextEncoding::to_wide(name);
        if (wname.empty()) {
            continue;
        }
        vars[upper(wname)] = {wname, util::TextEncoding::to_wide(value)};
    }

    std::vector<wchar_t> out;
    for (const auto& [key, pair] : vars) {  // std::map iterates sorted
        const std::wstring entry = pair.first + L"=" + pair.second;
        out.insert(out.end(), entry.begin(), entry.end());
        out.push_back(L'\0');
    }
    out.push_back(L'\0');  // block terminator
    return out;
}

// IShellLink-based .lnk resolution. CoInit/CoUninit per call so the
// caller doesn't need to worry about thread COM apartment state.
// Returns ``true`` and fills ``target``/``args``/``working_dir`` on
// success; ``false`` if the .lnk couldn't be loaded.
bool resolve_lnk(const std::wstring& lnk_path, std::wstring& target, std::wstring& args,
                 std::wstring& working_dir) {
    const HRESULT init_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool need_uninit = SUCCEEDED(init_hr);
    bool ok = false;

    IShellLinkW* link = nullptr;
    if (::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                            reinterpret_cast<void**>(&link)) == S_OK
        && link != nullptr) {
        IPersistFile* pf = nullptr;
        if (link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)) == S_OK
            && pf != nullptr) {
            if (pf->Load(lnk_path.c_str(), STGM_READ) == S_OK) {
                wchar_t buf[MAX_PATH] = {};
                if (link->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH) == S_OK
                    && buf[0] != L'\0') {
                    target = buf;
                    wchar_t arg_buf[2048] = {};
                    if (link->GetArguments(arg_buf, 2048) == S_OK) {
                        args = arg_buf;
                    }
                    wchar_t wd_buf[MAX_PATH] = {};
                    if (link->GetWorkingDirectory(wd_buf, MAX_PATH) == S_OK) {
                        working_dir = wd_buf;
                    }
                    ok = true;
                }
            }
            pf->Release();
        }
        link->Release();
    }
    if (need_uninit) {
        ::CoUninitialize();
    }
    return ok;
}

bool ends_with_icase(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = std::wcslen(suffix);
    if (s.size() < n) {
        return false;
    }
    return _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

// IShellLink::GetPath with SLGP_RAWPATH returns the path as stored in
// the .lnk file — which often contains unexpanded environment vars
// like ``%windir%\\system32\\charmap.exe``. CreateProcess does not
// expand these in the application name argument, so the launch fails
// with ERROR_PATH_NOT_FOUND (3). Expand them here. Manager-supplied
// raw paths get the same treatment in case they were typed with env
// vars too.
std::wstring expand_env(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    const DWORD n = ::ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (n == 0) {
        return s;
    }
    std::wstring out(n, L'\0');
    const DWORD got = ::ExpandEnvironmentStringsW(s.c_str(), out.data(), n);
    if (got == 0) {
        return s;
    }
    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

void set_err(std::string* error_out, const std::string& s) {
    LSW_AGENT_ERROR(std::string{"launch: "} + s);
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
    LSW_AGENT_INFO(std::string{"launch: app_id="} + app_id);

    // Resolve .lnk → real exe + args + working dir. For raw .exe /
    // .bat paths we just hand the path straight to CreateProcessW.
    std::wstring path = util::TextEncoding::to_wide(app_id);
    std::wstring args;
    std::wstring working_dir;
    if (ends_with_icase(path, L".lnk")) {
        std::wstring resolved;
        if (!resolve_lnk(path, resolved, args, working_dir) || resolved.empty()) {
            set_err(error_out,
                    "lnk_resolve failed (IShellLink load/GetPath returned empty) for "
                    + app_id);
            return false;
        }
        LSW_AGENT_INFO(std::string{"launch: lnk_resolve target="} + util::TextEncoding::to_utf8(resolved)
                       + " args=" + util::TextEncoding::to_utf8(args)
                       + " working_dir=" + util::TextEncoding::to_utf8(working_dir));
        path = resolved;
    }
    path = expand_env(path);
    working_dir = expand_env(working_dir);
    args = expand_env(args);

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
        const auto slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos && slash > 0) {
            working_dir = path.substr(0, slash);
        }
    }
    LSW_AGENT_DEBUG(std::string{"launch: expanded path="} + util::TextEncoding::to_utf8(path)
                    + " working_dir=" + util::TextEncoding::to_utf8(working_dir));

    // Build a mutable command line — CreateProcessW writes through
    // ``lpCommandLine``. Quote the exe path so spaces work; append args.
    std::wstring cmdline = L"\"" + path + L"\"";
    if (!args.empty()) {
        cmdline += L" ";
        cmdline += args;
    }
    // The configured arguments come last, after whatever a .lnk
    // carried, so an app config can add to a shortcut rather than
    // having to restate it.
    if (const std::wstring extra = expand_env(util::TextEncoding::to_wide(extra_args)); !extra.empty()) {
        cmdline += L" ";
        cmdline += extra;
    }
    std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back(L'\0');

    // The drivers go beside the RESOLVED executable, which is the only
    // path any of the loaders will search — a .lnk's own directory is
    // not on it. That is why this is here rather than in the RPC
    // handler: the handler has the shortcut, this has the image.
    //
    // A failure is logged and the launch goes ahead. Without the
    // drivers the application finds whatever Windows has, which on a
    // guest with no GPU means WARP: slow, and far better than an
    // application that refuses to start.
    std::vector<EnvVar> launch_env = env;
    std::string driver_err;
    if (!GuestDrivers::link_beside(util::TextEncoding::to_utf8(path), &launch_env, &driver_err)) {
        LSW_AGENT_WARN("launch: guest drivers not fully staged: " + driver_err);
    }

    // Empty unless this launch overrides something, in which case the
    // child gets a copy of ours with the overrides applied. The app
    // config's own overrides come first, so a user who sets one of
    // these by hand wins over what was just staged.
    std::vector<wchar_t> env_block = build_environment(launch_env);
    void* env_ptr = env_block.empty() ? nullptr : env_block.data();
    if (!launch_env.empty()) {
        std::string names;
        for (const auto& [name, value] : launch_env) {
            names += (names.empty() ? "" : " ") + name;
        }
        LSW_AGENT_INFO("launch: environment overrides: " + names);
    }

    // The agent runs AS THE USER, in their interactive session
    // (HKLM\Run autostart at the post-OOBE AutoLogon; the account is
    // a local admin). So a plain CreateProcessW lands the app on the
    // user's real desktop with the agent's elevated-admin token. No
    // session bridging, no token gymnastics.
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW so the child doesn't briefly flash a console
    // window for non-GUI exes; CREATE_UNICODE_ENVIRONMENT because we
    // implicitly inherit our (UTF-16) environment.
    //
    // CREATE_SUSPENDED so the process can be put in its launch's job
    // before it executes an instruction. A launcher stub can spawn the
    // real application in the first milliseconds of its life, and a
    // child that starts before the assignment is outside the job for
    // good — which is exactly the process we most need to track.
    const BOOL ok = ::CreateProcessW(
        path.c_str(), mutable_cmd.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED, env_ptr,
        working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);
    if (!ok) {
        const DWORD err = ::GetLastError();
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
        LSW_AGENT_WARN(std::string{"launch: direct spawn failed (GetLastError="}
                       + std::to_string(err) + ") — retrying via shell (explorer)");
        const std::wstring explorer = expand_env(L"%windir%\\explorer.exe");
        std::wstring shell_cmd = L"\"" + explorer + L"\" \"" + path + L"\"";
        std::vector<wchar_t> shell_mut(shell_cmd.begin(), shell_cmd.end());
        shell_mut.push_back(L'\0');
        STARTUPINFOW si2{};
        si2.cb = sizeof(si2);
        PROCESS_INFORMATION pi2{};
        if (::CreateProcessW(explorer.c_str(), shell_mut.data(), nullptr, nullptr,
                             FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                             &si2, &pi2)
            == 0) {
            set_err(error_out,
                    std::string{"launch failed: direct CreateProcessW err="}
                        + std::to_string(err) + ", shell fallback err="
                        + std::to_string(::GetLastError())
                        + ", cmdline=" + util::TextEncoding::to_utf8(cmdline));
            return false;
        }
        LSW_AGENT_INFO("launch: dispatched via shell; PID not tracked (shell-owned)");
        ::CloseHandle(pi2.hProcess);
        ::CloseHandle(pi2.hThread);
        if (pid_out != nullptr) {
            *pid_out = 0;
        }
        return true;
    }
    // Job first, then let it run. Whether the adoption worked or not the
    // thread has to be resumed — a process left suspended is a process
    // that never starts.
    (void)LaunchGroup::adopt(pi.hProcess, pi.dwProcessId);
    ::ResumeThread(pi.hThread);

    LSW_AGENT_INFO(std::string{"launch: spawned pid="} + std::to_string(pi.dwProcessId));
    if (pid_out != nullptr) {
        *pid_out = pi.dwProcessId;
    }
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return true;
}

}  // namespace lsw::agent::apps
