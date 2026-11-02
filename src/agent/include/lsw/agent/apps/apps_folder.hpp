#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <shobjidl.h>
// clang-format on

#include <string>
#include <vector>

namespace lsw::agent::apps {
struct AppsFolderItem {
    std::wstring parsing_name;
    std::wstring name;
    std::wstring target;
    std::wstring arguments;
    win::ComPtr<IShellItem> shell;

    [[nodiscard]] bool packaged() const {
        return parsing_name.find(L'!') != std::wstring::npos;
    }
};

class AppsFolder {
public:
    AppsFolder() = delete;

    [[nodiscard]] static std::vector<AppsFolderItem> items();

    [[nodiscard]] static std::string icon(IShellItem& item, int size);
};

}
