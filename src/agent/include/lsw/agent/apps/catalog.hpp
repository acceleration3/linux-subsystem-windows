#pragma once

#include <nlohmann/json.hpp>

namespace lsw::agent::apps {
class Catalog {
public:
    [[nodiscard]] static nlohmann::json list();
};

}
