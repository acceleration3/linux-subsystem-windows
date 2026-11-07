#include "sanity/life/kit.hpp"

#include "sanity/iso/loads.hpp"
#include "support/api_policy.hpp"
#include "support/bitmap.hpp"
#include "support/present_path.hpp"
#include "support/screen_capture.hpp"

#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <numeric>

namespace lsw::suites::sanity::life {

namespace {

using Clock = std::chrono::steady_clock;

double since_ms(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

bool documented_loss(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

}  // namespace

std::vector<DWORD> Kit::process_ids(const std::wstring& exe) {
    std::vector<DWORD> out;
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return out;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = ::Process32FirstW(snapshot, &entry); more != FALSE; more = ::Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, exe.c_str()) == 0) {
            out.push_back(entry.th32ProcessID);
        }
    }
    ::CloseHandle(snapshot);
    return out;
}

bool Kit::run(const std::wstring& command, DWORD& exit_code, int timeout_ms) {
    std::wstring line = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    exit_code = ~0u;
    if (!::CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
        return false;
    }
    const DWORD waited = ::WaitForSingleObject(info.hProcess, static_cast<DWORD>(timeout_ms));
    if (waited == WAIT_OBJECT_0) {
        ::GetExitCodeProcess(info.hProcess, &exit_code);
    }
    ::CloseHandle(info.hProcess);
    ::CloseHandle(info.hThread);
    return waited == WAIT_OBJECT_0;
}

bool Kit::restart_dwm(int timeout_ms, double& ms, std::string& error) {
    const std::vector<DWORD> before = process_ids(L"dwm.exe");
    if (before.empty()) {
        error = "dwm.exe is not running";
        return false;
    }
    const auto begin = Clock::now();
    DWORD code = 0;
    if (!run(L"taskkill.exe /F /IM dwm.exe", code) || code != 0) {
        error = "taskkill of dwm.exe failed with exit code " + std::to_string(code);
        return false;
    }
    while (since_ms(begin) < timeout_ms) {
        const std::vector<DWORD> now = process_ids(L"dwm.exe");
        const bool replaced = !now.empty() && std::none_of(now.begin(), now.end(), [&](DWORD pid) { return std::find(before.begin(), before.end(), pid) != before.end(); });
        if (replaced) {
            ms = since_ms(begin);
            return true;
        }
        ::Sleep(100);
    }
    error = "dwm.exe did not come back within " + std::to_string(timeout_ms) + " ms";
    return false;
}

std::vector<std::string> Kit::app_apis() {
    std::vector<std::string> out;
    for (const char* api : {"d3d11", "vulkan"}) {
        if (!PresentPath::ids_of(api).empty()) {
            out.push_back(api);
        }
    }
    return out;
}

Outcome Kit::app_role(Context& context, const std::string& role) {
    if (role.rfind("app-", 0) != 0) {
        return Outcome::fail("unknown role " + role);
    }
    const std::string api = role.substr(4);
    const std::vector<std::string> ids = PresentPath::ids_of(api);
    if (ids.empty()) {
        return Context::skip("no present path for " + api);
    }
    std::unique_ptr<PresentPath> path = PresentPath::make(ids.front());
    std::string error;
    if (path == nullptr || !path->open(80, 80, 256, 256, error)) {
        return Context::skip("opening the " + ids.front() + " path failed: " + error);
    }
    const double total = iso::Loads::seconds();
    const auto begin = Clock::now();
    std::vector<int> per_second(static_cast<std::size_t>(total) + 2, 0);
    bool lost = false;
    HRESULT lost_hr = S_OK;
    double lost_ms = 0;
    int frame = 0;
    int errors = 0;
    std::string first_error;
    while (since_ms(begin) < total * 1000.0) {
        const PresentResult result = path->present(frame++, 1);
        const std::size_t second = static_cast<std::size_t>(since_ms(begin) / 1000.0);
        if (result.ok) {
            ++per_second[std::min(second, per_second.size() - 1)];
        } else if (!result.occluded) {
            if (documented_loss(result.hr)) {
                lost = true;
                lost_hr = result.hr;
                lost_ms = since_ms(begin);
                break;
            }
            if (++errors == 1) {
                first_error = result.error + " (hr 0x" + std::to_string(static_cast<unsigned>(result.hr)) + ")";
            }
            ::Sleep(10);
        } else {
            ::Sleep(5);
        }
    }
    context.measure("frames", std::accumulate(per_second.begin(), per_second.end(), 0));
    context.measure("errors", errors);
    if (lost) {
        context.measure("lost_ms", lost_ms);
        context.measure("lost_hr", static_cast<long long>(static_cast<unsigned>(lost_hr)));
        return Outcome::ok();
    }
    const std::size_t tail_from = static_cast<std::size_t>(total * 2 / 3);
    int tail = 0;
    for (std::size_t i = tail_from; i < per_second.size(); ++i) {
        tail += per_second[i];
    }
    context.measure("tail_frames", tail);
    if (tail == 0) {
        return Outcome::fail("no frame was presented in the last third of the run, with " + std::to_string(errors) + " failed presents, first: " + first_error);
    }
    return Outcome::ok();
}

void Kit::judge_apps(Problems& problems, const iso::Fleet& fleet, const std::string& what) {
    for (const iso::FleetMember& member : fleet.members()) {
        if (member.result.status != Status::Ok && member.result.status != Status::Skip) {
            problems.add(what + " " + member.label + ": " + Outcome::name(member.result.status) + " " + member.result.reason);
        }
    }
}

std::vector<DEVMODEW> Kit::modes() {
    std::vector<DEVMODEW> out;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD i = 0; ::EnumDisplaySettingsW(nullptr, i, &mode) != FALSE; ++i) {
        if (mode.dmBitsPerPel == 32) {
            out.push_back(mode);
        }
    }
    return out;
}

bool Kit::current_mode(DEVMODEW& mode) {
    mode = {};
    mode.dmSize = sizeof(mode);
    return ::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) != FALSE;
}

LONG Kit::set_mode(const DEVMODEW& mode) {
    DEVMODEW copy = mode;
    copy.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
    return ::ChangeDisplaySettingsExW(nullptr, &copy, nullptr, 0, nullptr);
}

void Kit::restore_mode() {
    ::ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
}

bool Kit::desktop_shows(const RECT& area, COLORREF color, int timeout_ms, double& ms) {
    const auto begin = Clock::now();
    while (since_ms(begin) < timeout_ms) {
        Bitmap shot;
        std::string error;
        if (ScreenCapture::screen(area, false, shot, error) && shot.width > 0 && shot.height > 0) {
            const std::uint32_t got = shot.at(shot.width / 2, shot.height / 2);
            const int dr = static_cast<int>(got >> 16 & 255) - GetRValue(color);
            const int dg = static_cast<int>(got >> 8 & 255) - GetGValue(color);
            const int db = static_cast<int>(got & 255) - GetBValue(color);
            if (std::abs(dr) <= 3 && std::abs(dg) <= 3 && std::abs(db) <= 3) {
                ms = since_ms(begin);
                return true;
            }
        }
        ::Sleep(100);
    }
    ms = since_ms(begin);
    return false;
}

}  // namespace lsw::suites::sanity::life
