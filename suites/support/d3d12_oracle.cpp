#include "support/d3d12_oracle.hpp"

namespace lsw::suites {

bool D3d12Oracle::open(const AvAdapter& adapter, D3D_FEATURE_LEVEL level, std::string& error) {
    if (!av.open(adapter, level, error)) {
        return false;
    }
    std::string warp_error;
    if (!warp.open_warp(level, warp_error)) {
        error = "the WARP12 oracle is unavailable: " + warp_error;
        return false;
    }
    return true;
}

void D3d12Oracle::compare(Context& context, Problems& problems, const std::string& label, const Scene& scene,
                          const Tolerance& tolerance) {
    SceneImage expected;
    SceneImage actual;
    std::string error;
    if (!scene(warp, expected, error)) {
        problems.add(label + ": the WARP12 oracle failed: " + error);
        return;
    }
    error.clear();
    if (!scene(av, actual, error)) {
        problems.add(label + ": " + error);
        return;
    }
    std::string detail;
    if (!OracleRig::judge(context, label, expected, actual, tolerance, detail)) {
        problems.add(detail + " (against WARP12)");
    }
}

}  // namespace lsw::suites
