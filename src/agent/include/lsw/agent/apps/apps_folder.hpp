#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <shobjidl.h>
// clang-format on

#include <string>
#include <vector>

namespace lsw::agent::apps {

// One item of the Start menu's All apps list.
struct AppsFolderItem {
    // The item's parsing name: a packaged application's ID
    // (family!application), or for anything else the shortcut's own
    // identity (a path, or an explicit AppUserModelID).
    std::wstring parsing_name;
    // What the Start menu shows.
    std::wstring name;
    // For a shortcut, what it starts and with which arguments (the
    // shell's System.Link properties); empty for anything that has none.
    std::wstring target;
    std::wstring arguments;
    win::ComPtr<IShellItem> shell;

    [[nodiscard]] bool packaged() const {
        return parsing_name.find(L'!') != std::wstring::npos;
    }
};

// The shell's AppsFolder (shell:AppsFolder): everything the Start menu
// lists for the session's user, packaged or not. The one walk of it, which
// the Win32 catalog and the Store listing both read.
//
// Needs an apartment on the calling thread, and the user's session: the
// folder is the user's shell's.
class AppsFolder {
public:
    AppsFolder() = delete;

    // Every item, in the shell's order. Throws std::runtime_error with
    // Windows' reason when the folder cannot be opened.
    [[nodiscard]] static std::vector<AppsFolderItem> items();

    // The icon of ``item`` as a PNG, base64, ``size`` pixels square; empty
    // if Windows has none.
    [[nodiscard]] static std::string icon(IShellItem& item, int size);
};

}  // namespace lsw::agent::apps
