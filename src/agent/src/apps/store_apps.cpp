#include "lsw/agent/apps/store_apps.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/com_ptr.hpp"
#include "lsw/agent/win/icon_extractor.hpp"
#include "lsw/agent/win/process_info.hpp"
#include "lsw/agent/win/unique_handle.hpp"
#include "lsw/agent/win/window.hpp"

// clang-format off
#include <windows.h>
#include <appmodel.h>
#include <knownfolders.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace lsw::agent::apps {

namespace {

// Not in mingw-w64's shobjidl.h, which declares the coclass but not its CLSID.
constexpr CLSID kApplicationActivationManager = {
    0x45ba127d, 0x10a8, 0x46ea, {0x8a, 0xb7, 0x56, 0xea, 0x90, 0x78, 0x94, 0x3c}};

// A packaged application's ID is its package family, '!', and the
// application's name in the manifest. Everything else in AppsFolder (Win32
// shortcuts, by path or by an ID their shortcut set) has no '!'.
constexpr wchar_t kFamilySeparator = L'!';

struct StoreApp {
    std::wstring aumid;
    std::wstring name;
    std::string icon;
};

std::wstring display_name(IShellItem& item, SIGDN form) {
    PWSTR text = nullptr;
    if (FAILED(item.GetDisplayName(form, &text)) || text == nullptr) return {};
    std::wstring out{text};
    ::CoTaskMemFree(text);
    return out;
}

std::string icon_of(IShellItem& item, int size) {
    win::ComPtr<IShellItemImageFactory> images;
    if (FAILED(item.QueryInterface(IID_PPV_ARGS(images.GetAddressOf())))) return {};
    HBITMAP bitmap = nullptr;
    if (FAILED(images->GetImage(SIZE{size, size}, SIIGBF_ICONONLY, &bitmap)) || bitmap == nullptr) {
        return {};
    }
    std::string png = win::IconExtractor::from_premultiplied_bitmap(bitmap);
    ::DeleteObject(bitmap);
    return png;
}

// How long a new process is given to hand an activation to the instance
// already running and exit, as a single-instance application does.
constexpr DWORD kHandOffMs = 3000;
// PACKAGE_FAMILY_NAME_MAX_LENGTH, which mingw-w64's appmodel.h leaves out:
// a 64-character name, '_', and a 13-character publisher ID.
constexpr UINT32 kFamilyNameMax = 64 + 1 + 13;

// The running processes of a package family, oldest first.
std::vector<DWORD> family_processes(const std::wstring& family) {
    std::vector<std::pair<std::uint64_t, DWORD>> found;
    for (const auto& entry : win::ProcessInfo::snapshot()) {
        const win::UniqueHandle process(
            ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.pid));
        if (!process) continue;
        wchar_t name[kFamilyNameMax + 1];
        UINT32 length = static_cast<UINT32>(std::size(name));
        if (::GetPackageFamilyName(process.get(), &length, name) != ERROR_SUCCESS) continue;
        if (_wcsicmp(name, family.c_str()) != 0) continue;
        found.emplace_back(win::ProcessInfo::creation_time(process.get()).value_or(0), entry.pid);
    }
    std::sort(found.begin(), found.end());
    std::vector<DWORD> pids;
    pids.reserve(found.size());
    for (const auto& [started, pid] : found) pids.push_back(pid);
    return pids;
}

// The instance a single-instance application handed its activation to:
// the one of ``family`` showing a window, else the oldest. Zero for none.
DWORD running_instance(const std::wstring& family, DWORD gone) {
    std::vector<DWORD> pids = family_processes(family);
    pids.erase(std::remove(pids.begin(), pids.end(), gone), pids.end());
    if (pids.empty()) return 0;
    for (const auto& window : win::Window::enumerate()) {
        if (std::find(pids.begin(), pids.end(), window.pid) != pids.end()) return window.pid;
    }
    return pids.front();
}

// Whether ``pid`` ended within ``ms``; a process that cannot be opened has.
bool exits_within(DWORD pid, DWORD ms) {
    const win::UniqueHandle process(::OpenProcess(SYNCHRONIZE, FALSE, pid));
    return !process || ::WaitForSingleObject(process.get(), ms) == WAIT_OBJECT_0;
}

std::string hresult_text(HRESULT hr) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08lx", static_cast<unsigned long>(hr));
    return text;
}

}  // namespace

nlohmann::json StoreApps::list(int icon_size, const std::wstring& only) {
    win::ComPtr<IShellItem> folder;
    HRESULT hr = ::SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
                                        IID_PPV_ARGS(folder.GetAddressOf()));
    if (FAILED(hr)) throw std::runtime_error("cannot open the Start menu's apps: " + hresult_text(hr));
    win::ComPtr<IEnumShellItems> items;
    hr = folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(items.GetAddressOf()));
    if (FAILED(hr)) throw std::runtime_error("cannot list the Start menu's apps: " + hresult_text(hr));

    std::vector<StoreApp> found;
    for (;;) {
        win::ComPtr<IShellItem> item;
        if (items->Next(1, item.GetAddressOf(), nullptr) != S_OK) break;
        std::wstring aumid = display_name(*item.Get(), SIGDN_PARENTRELATIVEPARSING);
        if (aumid.find(kFamilySeparator) == std::wstring::npos) continue;
        if (!only.empty() && _wcsicmp(aumid.c_str(), only.c_str()) != 0) continue;
        std::wstring name = display_name(*item.Get(), SIGDN_NORMALDISPLAY);
        if (name.empty()) continue;
        found.push_back({std::move(aumid), std::move(name), icon_of(*item.Get(), icon_size)});
    }
    std::sort(found.begin(), found.end(), [](const StoreApp& a, const StoreApp& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    auto apps = nlohmann::json::array();
    for (const StoreApp& app : found) {
        const std::wstring family = app.aumid.substr(0, app.aumid.find(kFamilySeparator));
        apps.push_back({{"aumid", util::TextEncoding::to_utf8(app.aumid)},
                        {"name", util::TextEncoding::to_utf8(app.name)},
                        {"family", util::TextEncoding::to_utf8(family)},
                        {"icon_png_base64", app.icon}});
    }
    return {{"apps", std::move(apps)}};
}

unsigned long StoreApps::activate(const std::wstring& aumid, const std::wstring& args) {
    win::ComPtr<IApplicationActivationManager> activation;
    HRESULT hr = ::CoCreateInstance(kApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                                    IID_PPV_ARGS(activation.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error("cannot reach Windows' application activation: " + hresult_text(hr));
    }
    const std::wstring family = aumid.substr(0, aumid.find(kFamilySeparator));
    const bool was_running = !family_processes(family).empty();
    DWORD pid = 0;
    hr = activation->ActivateApplication(aumid.c_str(), args.empty() ? nullptr : args.c_str(),
                                         AO_NONE, &pid);
    if (FAILED(hr)) {
        throw std::runtime_error("Windows could not start " + util::TextEncoding::to_utf8(aumid) +
                                 ": " + hresult_text(hr));
    }
    // Already running, a single-instance application starts a process that
    // passes the activation to the running one and exits; the instance to
    // follow is the one that was there.
    if (was_running && exits_within(pid, kHandOffMs)) {
        if (const DWORD running = running_instance(family, pid); running != 0) {
            LSW_AGENT_INFO("store app {} handed its activation (pid {}) to pid {}",
                           util::TextEncoding::to_utf8(aumid), pid, running);
            return running;
        }
    }
    LSW_AGENT_INFO("store app {} activated, pid {}", util::TextEncoding::to_utf8(aumid), pid);
    return pid;
}

}  // namespace lsw::agent::apps
