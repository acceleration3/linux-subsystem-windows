#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

struct IUIAutomation;

namespace lsw::agent::uia {

// Walks the UIA tree under a window into JSON: control type, name,
// automation id and bounding rect per node.
//
// A diagnostic tool above all — when a selector does not match, this is
// what shows why.
class TreeDumper {
public:
    TreeDumper() = delete;

    // ``max_depth`` clamps recursion; <= 0 falls back to 4 levels, which
    // is deep enough to find real controls and shallow enough not to
    // serialise an entire Electron app.
    [[nodiscard]] static nlohmann::json run(IUIAutomation* root, std::uintptr_t hwnd,
                                            int max_depth);
};

}  // namespace lsw::agent::uia
