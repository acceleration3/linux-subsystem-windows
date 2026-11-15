#pragma once

#include "support/d3d12_rig.hpp"
#include "support/oracle_rig.hpp"

#include <functional>

namespace lsw::suites {
class D3d12Oracle {
public:
    using Scene = std::function<bool(D3d12Rig&, SceneImage&, std::string&)>;

    [[nodiscard]] bool open(const AvAdapter& adapter, D3D_FEATURE_LEVEL level, std::string& error);

    void compare(Context& context, Problems& problems, const std::string& label, const Scene& scene, const Tolerance& tolerance);

    D3d12Rig av;
    D3d12Rig warp;
};
}  // namespace lsw::suites
