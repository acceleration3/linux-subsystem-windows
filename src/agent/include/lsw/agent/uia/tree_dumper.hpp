#pragma once

#include <nlohmann/json.hpp>

namespace lsw::agent::uia {

class UiaWindow;

class TreeDumper {
public:
    TreeDumper() = delete;

    [[nodiscard]] static nlohmann::json dump(const UiaWindow& window,
                                            int max_depth);
};

}
