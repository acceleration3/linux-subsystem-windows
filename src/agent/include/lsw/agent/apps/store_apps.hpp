#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace lsw::agent::apps {
class StoreApps {
public:
    StoreApps() = delete;

    [[nodiscard]] static nlohmann::json list(int icon_size, const std::wstring& only);

    [[nodiscard]] static unsigned long activate(const std::wstring& aumid, const std::wstring& args,
                                                const std::vector<std::wstring>& documents = {});
};

}
