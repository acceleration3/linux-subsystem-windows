#include "core/desktop_guard.hpp"

#include "core/text.hpp"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>

namespace lsw::suites {

namespace {

constexpr std::array<const char*, 4> kSuiteImages = {"lsw-suites", "lsw-render-ref", "lsw-guest-agent", "dwm.exe"};
constexpr std::array<const char*, 15> kSystemImages = {
    "cmd.exe",          "conhost.exe",   "openconsole.exe",         "explorer.exe",
    "shellexperiencehost.exe", "startmenuexperiencehost.exe", "searchhost.exe", "searchapp.exe",
    "shellhost.exe",    "textinputhost.exe", "sihost.exe",          "applicationframehost.exe",
    "lockapp.exe",      "logonui.exe",   "csrss.exe"};

std::string normalized(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return path;
}

std::string directory_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string file_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string image_of(DWORD pid) {
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return {};
    }
    std::wstring buffer(1024, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size());
    const bool ok = ::QueryFullProcessImageNameW(process, 0, buffer.data(), &size) != FALSE;
    ::CloseHandle(process);
    if (!ok) {
        return {};
    }
    buffer.resize(size);
    return Text::utf8(buffer);
}

struct Sweep {
    const DesktopGuard* guard;
    std::string own_directory;
    std::map<DWORD, std::string> images;
    std::vector<std::string> notes;
};

BOOL CALLBACK visit(HWND window, LPARAM parameter) {
    Sweep& sweep = *reinterpret_cast<Sweep*>(parameter);
    if (!::IsWindowVisible(window) || ::IsIconic(window) || ::GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    if ((::GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0) {
        return TRUE;
    }
    DWORD pid = 0;
    ::GetWindowThreadProcessId(window, &pid);
    auto found = sweep.images.find(pid);
    if (found == sweep.images.end()) {
        found = sweep.images.emplace(pid, image_of(pid)).first;
    }
    if (found->second.empty() || !DesktopGuard::foreign(found->second, sweep.own_directory)) {
        return TRUE;
    }
    wchar_t title[256] = {};
    ::GetWindowTextW(window, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
    ::ShowWindowAsync(window, SW_SHOWMINNOACTIVE);
    sweep.notes.push_back("desktop: minimized a window of " + file_of(normalized(found->second)) + " titled \"" +
                          Text::utf8(title) + "\" before the row");
    return TRUE;
}

}  // namespace

DesktopGuard::DesktopGuard() {
    std::wstring path(1024, L'\0');
    path.resize(::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    own_directory_ = directory_of(normalized(Text::utf8(path)));
}

std::vector<std::string> DesktopGuard::sweep() const {
    Sweep state{this, own_directory_, {}, {}};
    ::EnumWindows(visit, reinterpret_cast<LPARAM>(&state));
    return state.notes;
}

bool DesktopGuard::foreign(const std::string& image_path, const std::string& own_directory) {
    const std::string path = normalized(image_path);
    const std::string name = file_of(path);
    for (const char* prefix : kSuiteImages) {
        if (name.rfind(prefix, 0) == 0) {
            return false;
        }
    }
    for (const char* system : kSystemImages) {
        if (name == system) {
            return false;
        }
    }
    return own_directory.empty() || directory_of(path) != normalized(own_directory);
}

}  // namespace lsw::suites
