#include "support/d3d10_rig.hpp"

#include "support/target_adapter.hpp"

namespace lsw::suites {

bool D3d10Rig::open(bool with_1, std::string& error) {
    if (!TargetAdapter::pick(adapter, error)) {
        return false;
    }
    if (with_1) {
        if (!av.create1(adapter.luid, 0, D3D10_FEATURE_LEVEL_10_1, error)) {
            return false;
        }
        if (!warp.create_warp1(0, D3D10_FEATURE_LEVEL_10_1, error)) {
            error = "the D3D10.1 WARP oracle is unavailable: " + error;
            return false;
        }
        return true;
    }
    if (!av.create(adapter.luid, 0, error)) {
        return false;
    }
    if (!warp.create_warp(0, error)) {
        error = "the D3D10 WARP oracle is unavailable: " + error;
        return false;
    }
    return true;
}

void D3d10Rig::compare(Context& context, Problems& problems, const std::string& label, const Scene& scene, const Tolerance& tolerance) {
    SceneImage expected;
    SceneImage actual;
    std::string error;
    if (!scene(warp, expected, error)) {
        problems.add(label + ": the WARP oracle failed: " + error);
        return;
    }
    error.clear();
    if (!scene(av, actual, error)) {
        problems.add(label + ": " + error);
        return;
    }
    std::string detail;
    if (!OracleRig::judge(context, label, expected, actual, tolerance, detail)) {
        problems.add(detail + " (against WARP)");
    }
}

}  // namespace lsw::suites
