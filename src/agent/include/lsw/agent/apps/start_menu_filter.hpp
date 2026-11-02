#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lsw::agent::apps {
struct StartMenuEntry {
    std::wstring name;
    std::wstring target;
    std::wstring arguments;
};

class StartMenuFilter {
public:
    StartMenuFilter() = delete;

    [[nodiscard]] static std::vector<StartMenuEntry>
    applications(const std::vector<StartMenuEntry>& entries, const std::wstring& windows_dir,
                 const std::function<bool(const std::wstring&)>& exists);

    [[nodiscard]] static bool is_uninstaller(const std::wstring& exe);
};

}
