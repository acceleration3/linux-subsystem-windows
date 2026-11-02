#include "lsw/agent/apps/store_apps.hpp"

#include "lsw/agent/apps/apps_folder.hpp"
#include "lsw/agent/apps/file_associations.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"
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
constexpr CLSID kApplicationActivationManager = {
    0x45ba127d, 0x10a8, 0x46ea, {0x8a, 0xb7, 0x56, 0xea, 0x90, 0x78, 0x94, 0x3c}};

constexpr wchar_t kFamilySeparator = L'!';

struct StoreApp {
    std::wstring aumid;
    std::wstring name;
    std::string icon;
};

constexpr DWORD kHandOffMs = 3000;
constexpr UINT32 kFamilyNameMax = 64 + 1 + 13;

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

DWORD running_instance(const std::wstring& family, DWORD gone) {
    std::vector<DWORD> pids = family_processes(family);
    pids.erase(std::remove(pids.begin(), pids.end(), gone), pids.end());
    if (pids.empty()) return 0;
    for (const auto& window : win::Window::enumerate()) {
        if (std::find(pids.begin(), pids.end(), window.pid) != pids.end()) return window.pid;
    }
    return pids.front();
}

bool exits_within(DWORD pid, DWORD ms) {
    const win::UniqueHandle process(::OpenProcess(SYNCHRONIZE, FALSE, pid));
    return !process || ::WaitForSingleObject(process.get(), ms) == WAIT_OBJECT_0;
}


}

nlohmann::json StoreApps::list(int icon_size, const std::wstring& only) {
    std::vector<StoreApp> found;
    for (const AppsFolderItem& item : AppsFolder::items()) {
        if (!item.packaged() || item.name.empty()) continue;
        if (!only.empty() && _wcsicmp(item.parsing_name.c_str(), only.c_str()) != 0) continue;
        found.push_back({item.parsing_name, item.name, AppsFolder::icon(*item.shell.Get(), icon_size)});
    }
    std::sort(found.begin(), found.end(), [](const StoreApp& a, const StoreApp& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    const auto associations = FileAssociations::scan();
    auto apps = nlohmann::json::array();
    for (const StoreApp& app : found) {
        const std::wstring family = app.aumid.substr(0, app.aumid.find(kFamilySeparator));
        auto extensions = nlohmann::json::array();
        for (const std::wstring& extension : FileAssociations::of(associations, app.aumid)) {
            extensions.push_back(util::TextEncoding::to_utf8(extension));
        }
        apps.push_back({{"aumid", util::TextEncoding::to_utf8(app.aumid)},
                        {"name", util::TextEncoding::to_utf8(app.name)},
                        {"family", util::TextEncoding::to_utf8(family)},
                        {"extensions", std::move(extensions)},
                        {"icon_png_base64", app.icon}});
    }
    return {{"apps", std::move(apps)}};
}

namespace {
win::ComPtr<IShellItemArray> items_of(const std::vector<std::wstring>& paths) {
    std::vector<PIDLIST_ABSOLUTE> ids;
    HRESULT hr = S_OK;
    for (const std::wstring& path : paths) {
        PIDLIST_ABSOLUTE id = nullptr;
        hr = ::SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr);
        if (FAILED(hr) || id == nullptr) {
            for (PIDLIST_ABSOLUTE made : ids) ::CoTaskMemFree(made);
            throw std::runtime_error("Windows cannot find " + util::TextEncoding::to_utf8(path) +
                                     ": " + win::ErrorText::hresult(hr));
        }
        ids.push_back(id);
    }
    win::ComPtr<IShellItemArray> array;
    hr = ::SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()),
                                             const_cast<PCIDLIST_ABSOLUTE_ARRAY>(ids.data()),
                                             array.GetAddressOf());
    for (PIDLIST_ABSOLUTE made : ids) ::CoTaskMemFree(made);
    if (FAILED(hr)) {
        throw std::runtime_error("cannot hand the documents to Windows: " + win::ErrorText::hresult(hr));
    }
    return array;
}

}

unsigned long StoreApps::activate(const std::wstring& aumid, const std::wstring& args,
                                  const std::vector<std::wstring>& documents) {
    win::ComPtr<IApplicationActivationManager> activation;
    HRESULT hr = ::CoCreateInstance(kApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                                    IID_PPV_ARGS(activation.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error("cannot reach Windows' application activation: " + win::ErrorText::hresult(hr));
    }
    const std::wstring family = aumid.substr(0, aumid.find(kFamilySeparator));
    const bool was_running = !family_processes(family).empty();
    DWORD pid = 0;
    if (!documents.empty()) {
        if (!args.empty()) {
            LSW_AGENT_WARN("store app {}: arguments cannot go along with documents; dropped",
                           util::TextEncoding::to_utf8(aumid));
        }
        const auto items = items_of(documents);
        hr = activation->ActivateForFile(aumid.c_str(), items.Get(), L"open", &pid);
    } else {
        hr = activation->ActivateApplication(aumid.c_str(),
                                             args.empty() ? nullptr : args.c_str(), AO_NONE, &pid);
    }
    if (FAILED(hr)) {
        throw std::runtime_error("Windows could not start " + util::TextEncoding::to_utf8(aumid) +
                                 ": " + win::ErrorText::hresult(hr));
    }
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

}
