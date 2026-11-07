#include "support/oracle_rig.hpp"

#include "support/artifact_writer.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites {

bool OracleRig::open(std::string& error, UINT device_flags) {
    if (!TargetAdapter::pick(adapter, error)) {
        return false;
    }
    if (!av.create_ex(adapter.luid, nullptr, 0, device_flags, error)) {
        return false;
    }
    D3D_FEATURE_LEVEL level = av.level;
    if (level > D3D_FEATURE_LEVEL_11_1) {
        level = D3D_FEATURE_LEVEL_11_1;
    }
    if (!warp.create_warp(&level, 1, device_flags, error)) {
        error = "the WARP oracle is unavailable: " + error;
        return false;
    }
    return true;
}

std::string OracleRig::sanitize(const std::string& label) {
    std::string out;
    for (char c : label) {
        out += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_';
    }
    return out;
}

bool OracleRig::judge(Context& context, const std::string& label, const SceneImage& expected,
                      const SceneImage& actual, const Tolerance& tolerance, std::string& detail) {
    if (expected.data.size() != actual.data.size()) {
        detail = label + ": result size " + std::to_string(actual.data.size()) + " differs from expected " +
                 std::to_string(expected.data.size());
        return false;
    }
    std::size_t bad = 0;
    std::size_t first = 0;
    std::size_t total = 0;
    int largest = 0;
    std::size_t largest_at = 0;
    std::vector<std::uint8_t> diff(actual.data.size(), 0);
    if (tolerance.floats) {
        total = actual.data.size() / 4;
        for (std::size_t i = 0; i < total; ++i) {
            float e = 0;
            float a = 0;
            std::memcpy(&e, expected.data.data() + i * 4, 4);
            std::memcpy(&a, actual.data.data() + i * 4, 4);
            const bool both_nan = std::isnan(e) && std::isnan(a);
            const bool close = both_nan || e == a || std::fabs(a - e) <= tolerance.absolute + tolerance.relative * std::fabs(e);
            if (!close) {
                if (bad++ == 0) {
                    first = i;
                }
                std::memset(diff.data() + i * 4, 0xff, 4);
            }
        }
    } else {
        total = actual.data.size();
        for (std::size_t i = 0; i < total; ++i) {
            const int delta = std::abs(static_cast<int>(expected.data[i]) - static_cast<int>(actual.data[i]));
            if (delta > tolerance.byte) {
                if (delta > largest) {
                    largest = delta;
                    largest_at = i;
                }
                if (bad++ == 0) {
                    first = i;
                }
                diff[i] = static_cast<std::uint8_t>(std::min(255, 64 + delta * 4));
            }
        }
    }
    const double allowed = tolerance.bad_fraction * static_cast<double>(total);
    if (static_cast<double>(bad) <= allowed) {
        return true;
    }
    detail = label + ": " + std::to_string(bad) + " of " + std::to_string(total) + " elements differ, first at " +
             std::to_string(first);
    if (!tolerance.floats && first < actual.data.size()) {
        detail += " expected " + std::to_string(expected.data[first]) + " actual " + std::to_string(actual.data[first]);
        detail += "; largest difference " + std::to_string(largest) + " at " + std::to_string(largest_at) + " expected " + std::to_string(expected.data[largest_at]) +
                  " actual " + std::to_string(actual.data[largest_at]);
    }
    const bool image = actual.width * actual.height * 4 == actual.data.size() && actual.width > 0;
    if (image) {
        ArtifactWriter writer(context);
        const std::string base = sanitize(label);
        auto write = [&](const char* kind, const std::vector<std::uint8_t>& pixels) {
            std::vector<std::uint8_t> bgra = pixels;
            if (actual.rgba) {
                for (std::size_t i = 0; i + 3 < bgra.size(); i += 4) {
                    std::swap(bgra[i], bgra[i + 2]);
                }
            }
            for (std::size_t i = 3; i < bgra.size(); i += 4) {
                bgra[i] = 255;
            }
            writer.image(base + "." + kind, bgra.data(), actual.width, actual.height, actual.width * 4);
        };
        write("expected", expected.data);
        write("actual", actual.data);
        write("diff", diff);
    } else {
        ArtifactWriter writer(context);
        writer.bytes(sanitize(label) + ".expected", expected.data.data(), expected.data.size());
        writer.bytes(sanitize(label) + ".actual", actual.data.data(), actual.data.size());
    }
    return false;
}

void OracleRig::compare(Context& context, Problems& problems, const std::string& label, const Scene& scene,
                        const Tolerance& tolerance) {
    SceneImage on_warp;
    SceneImage on_av;
    std::string error;
    warp.context->ClearState();
    av.context->ClearState();
    if (!scene(warp, on_warp, error)) {
        problems.add(label + ": the WARP oracle failed: " + error);
        return;
    }
    error.clear();
    av.context->ClearState();
    if (!scene(av, on_av, error)) {
        problems.add(label + ": " + error);
        return;
    }
    std::string detail;
    if (!judge(context, label, on_warp, on_av, tolerance, detail)) {
        problems.add(detail + " (against WARP)");
    }
}

}  // namespace lsw::suites
