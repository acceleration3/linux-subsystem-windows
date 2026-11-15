#pragma once

#include "core/context.hpp"
#include "support/av_adapters.hpp"
#include "support/d3d11_tools.hpp"
#include "support/problems.hpp"

#include <functional>
#include <string>
#include <vector>

namespace lsw::suites {
struct SceneImage {
    UINT width = 0;
    UINT height = 0;
    bool rgba = false;
    std::vector<std::uint8_t> data;
};

struct Tolerance {
    int byte = 0;
    float absolute = 0.0f;
    float relative = 0.0f;
    bool floats = false;
    double bad_fraction = 0.0;

    [[nodiscard]] static Tolerance exact() { return {}; }
    [[nodiscard]] static Tolerance bytes(int delta, double fraction = 0.0) { return {delta, 0, 0, false, fraction}; }
    [[nodiscard]] static Tolerance float32(float absolute_error, float relative_error = 0.0f) {
        return {0, absolute_error, relative_error, true, 0.0};
    }
};

class OracleRig {
public:
    using Scene = std::function<bool(D3d11Device&, SceneImage&, std::string&)>;

    [[nodiscard]] bool open(std::string& error, UINT device_flags = 0);

    [[nodiscard]] static std::string sanitize(const std::string& label);

    void compare(Context& context, Problems& problems, const std::string& label, const Scene& scene,
                 const Tolerance& tolerance);

    [[nodiscard]] static bool judge(Context& context, const std::string& label, const SceneImage& expected,
                                    const SceneImage& actual, const Tolerance& tolerance, std::string& detail);

    D3d11Device av;
    D3d11Device warp;
    AvAdapter adapter;
};
}  // namespace lsw::suites
