#include "render/common/tolerance.hpp"

namespace lsw::render {

Tolerance Tolerance::exact() { return {}; }

Tolerance Tolerance::tight(int delta) {
    Tolerance t;
    t.byte_delta = delta;
    t.name = "tight(" + std::to_string(delta) + ")";
    return t;
}

Tolerance Tolerance::blend(int delta) {
    Tolerance t;
    t.byte_delta = delta;
    t.name = "blend(" + std::to_string(delta) + ")";
    return t;
}

Tolerance Tolerance::filter(int delta, double outliers) {
    Tolerance t;
    t.byte_delta = delta;
    t.outlier_fraction = outliers;
    t.outlier_max_delta = 24.0;
    t.max_outliers = 64;
    t.min_psnr = 35.0;
    t.name = "filter(" + std::to_string(delta) + ")";
    return t;
}

Tolerance Tolerance::raster(int delta, int radius) {
    Tolerance t;
    t.byte_delta = delta;
    t.edge_radius = radius;
    t.forgive_fraction = 0.01;
    t.name = "raster(" + std::to_string(delta) + "," + std::to_string(radius) + ")";
    return t;
}

Tolerance Tolerance::psnr(double decibels) {
    Tolerance t;
    t.byte_delta = 255;
    t.min_psnr = decibels;
    t.name = "psnr(" + std::to_string(static_cast<int>(decibels)) + ")";
    return t;
}

Tolerance Tolerance::floats(float absolute, float relative) {
    Tolerance t;
    t.absolute = absolute;
    t.relative = relative;
    t.name = "float";
    return t;
}

Tolerance Tolerance::with_outliers(double fraction) const {
    Tolerance t = *this;
    t.outlier_fraction = fraction;
    return t;
}

Tolerance Tolerance::with_edge(int radius) const {
    Tolerance t = *this;
    t.edge_radius = radius;
    t.forgive_fraction = 0.01;
    return t;
}

}  // namespace lsw::render
