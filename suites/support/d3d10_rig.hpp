#pragma once

#include "core/context.hpp"
#include "support/av_adapters.hpp"
#include "support/d3d10_device.hpp"
#include "support/oracle_rig.hpp"
#include "support/problems.hpp"

#include <functional>
#include <string>

namespace lsw::suites {
class D3d10Rig {
public:
    using Scene = std::function<bool(D3d10Device&, SceneImage&, std::string&)>;

    [[nodiscard]] bool open(bool with_1, std::string& error);

    void compare(Context& context, Problems& problems, const std::string& label, const Scene& scene, const Tolerance& tolerance);

    D3d10Device av;
    D3d10Device warp;
    AvAdapter adapter;
};
}  // namespace lsw::suites
