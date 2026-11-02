#pragma once

#include <nlohmann/json.hpp>

namespace lsw::agent::system {
class Info {
public:
    [[nodiscard]] static nlohmann::json collect();

    [[nodiscard]] static const char* build_stamp();

    [[nodiscard]] static nlohmann::json release_record();
};

}
