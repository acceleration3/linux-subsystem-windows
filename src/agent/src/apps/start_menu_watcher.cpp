#include "lsw/agent/apps/start_menu_watcher.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <map>
#include <string>
#include <vector>

namespace lsw::agent::apps {

namespace {
constexpr DWORD kQuietMs = 2000;
constexpr DWORD kRescanMs = 5000;

constexpr const wchar_t kProgramsTail[] = L"\\Microsoft\\Windows\\Start Menu\\Programs";

std::vector<std::wstring> folders() {
    std::vector<std::wstring> out;
    const std::wstring common = win::Environment::expand(L"%ProgramData%");
    if (!common.empty() && common != L"%ProgramData%") {
        out.push_back(common + kProgramsTail);
    }
    const std::wstring users = win::Environment::expand(L"%SystemDrive%") + L"\\Users\\";
    WIN32_FIND_DATAW fd{};
    const HANDLE find = ::FindFirstFileW((users + L"*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || name == L"." || name == L"..") {
                continue;
            }
            out.push_back(users + name + L"\\AppData\\Roaming" + kProgramsTail);
        } while (::FindNextFileW(find, &fd));
        ::FindClose(find);
    }
    return out;
}

}

StartMenuWatcher::~StartMenuWatcher() {
    if (stop_event_ != nullptr) {
        ::SetEvent(stop_event_);
        if (thread_.joinable()) {
            thread_.join();
        }
        ::CloseHandle(stop_event_);
    }
}

void StartMenuWatcher::start() {
    std::call_once(started_, [this] {
        stop_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stop_event_ == nullptr) {
            LSW_AGENT_WARN("start menu watcher: cannot create its stop event");
            return;
        }
        thread_ = std::thread([this] { run(); });
    });
}

std::vector<rpc::Event> StartMenuWatcher::collect_pending() {
    if (!changed_.exchange(false)) {
        return {};
    }
    return {rpc::Event{linkproto::topic::kAppsChanged, nlohmann::json::object()}};
}

void StartMenuWatcher::run() {
    std::map<std::wstring, HANDLE> watches;
    ULONGLONG last_scan = 0;
    ULONGLONG last_change = 0;
    bool dirty = false;
    for (;;) {
        const ULONGLONG now = ::GetTickCount64();
        if (last_scan == 0 || now - last_scan >= kRescanMs) {
            last_scan = now;
            for (const std::wstring& folder : folders()) {
                if (watches.contains(folder)) {
                    continue;
                }
                const HANDLE handle = ::FindFirstChangeNotificationW(
                    folder.c_str(), TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
                if (handle != INVALID_HANDLE_VALUE) {
                    watches.emplace(folder, handle);
                }
            }
        }
        std::vector<HANDLE> handles{static_cast<HANDLE>(stop_event_)};
        std::vector<std::wstring> named{std::wstring{}};
        for (const auto& [folder, handle] : watches) {
            if (handles.size() < MAXIMUM_WAIT_OBJECTS) {
                handles.push_back(handle);
                named.push_back(folder);
            }
        }
        const DWORD wait = dirty ? 250 : kRescanMs;
        const DWORD which = ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(),
                                                     FALSE, wait);
        if (which == WAIT_OBJECT_0) {
            break;
        }
        if (which > WAIT_OBJECT_0 && which < WAIT_OBJECT_0 + handles.size()) {
            const std::size_t index = which - WAIT_OBJECT_0;
            if (!::FindNextChangeNotification(handles[index])) {
                ::FindCloseChangeNotification(handles[index]);
                watches.erase(named[index]);
            }
            dirty = true;
            last_change = ::GetTickCount64();
        }
        if (dirty && ::GetTickCount64() - last_change >= kQuietMs) {
            dirty = false;
            LSW_AGENT_INFO("start menu changed; telling the manager its app list is out of date");
            changed_ = true;
        }
    }
    for (const auto& [folder, handle] : watches) {
        ::FindCloseChangeNotification(handle);
    }
}

}
