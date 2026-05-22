#include "lsw/agent/apps/launcher.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

#include <cwchar>
#include <vector>

namespace lsw::agent::apps {

namespace {

std::wstring utf8_to_utf16(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), -1, nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), -1, out.data(), n);
    return out;
}

std::string utf16_to_utf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0,
                                          nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
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

bool Launcher::launch(const std::string& app_id, unsigned long* pid_out,
                              std::string* error_out) {
    if (pid_out != nullptr) {
        *pid_out = 0;
    }
    LSW_AGENT_INFO(std::string{"launch: app_id="} + app_id);

    // Resolve .lnk → real exe + args + working dir. For raw .exe /
    // .bat paths we just hand the path straight to CreateProcessW.
    std::wstring path = utf8_to_utf16(app_id);
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
        LSW_AGENT_INFO(std::string{"launch: lnk_resolve target="} + utf16_to_utf8(resolved)
                       + " args=" + utf16_to_utf8(args)
                       + " working_dir=" + utf16_to_utf8(working_dir));
        path = resolved;
    }
    path = expand_env(path);
    working_dir = expand_env(working_dir);
    args = expand_env(args);
    LSW_AGENT_DEBUG(std::string{"launch: expanded path="} + utf16_to_utf8(path)
                    + " working_dir=" + utf16_to_utf8(working_dir));

    // Build a mutable command line — CreateProcessW writes through
    // ``lpCommandLine``. Quote the exe path so spaces work; append args.
    std::wstring cmdline = L"\"" + path + L"\"";
    if (!args.empty()) {
        cmdline += L" ";
        cmdline += args;
    }
    std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back(L'\0');

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
    const BOOL ok = ::CreateProcessW(
        path.c_str(), mutable_cmd.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, nullptr,
        working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);
    if (!ok) {
        const DWORD err = ::GetLastError();
        // Same shell fallback as the user-session path: not a plain
        // PE (alias / packaged / document) → let the shell open it.
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
                        + ", cmdline=" + utf16_to_utf8(cmdline));
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
    LSW_AGENT_INFO(std::string{"launch: spawned pid="} + std::to_string(pi.dwProcessId));
    if (pid_out != nullptr) {
        *pid_out = pi.dwProcessId;
    }
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return true;
}

}  // namespace lsw::agent::apps
