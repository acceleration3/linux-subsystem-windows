#pragma once

#include <cstdint>
#include <string>

namespace lsw::render {
struct Tolerance {
    int byte_delta = 0;
    float absolute = 0.0f;
    float relative = 0.0f;
    double outlier_fraction = 0.0;
    int edge_radius = 0;
    double forgive_fraction = 0.0;
    double outlier_max_delta = 0.0;
    std::uint64_t max_outliers = 0;
    bool integer_edges = false;
    double min_psnr = 0.0;
    std::string name = "exact";

    [[nodiscard]] static Tolerance exact();
    [[nodiscard]] static Tolerance tight(int delta = 1);
    [[nodiscard]] static Tolerance blend(int delta = 2);
    [[nodiscard]] static Tolerance filter(int delta = 4, double outliers = 0.001);
    [[nodiscard]] static Tolerance raster(int delta = 1, int radius = 1);
    [[nodiscard]] static Tolerance psnr(double decibels);
    [[nodiscard]] static Tolerance coverage_edges();
    [[nodiscard]] static Tolerance floats(float absolute, float relative = 0.0f);

    [[nodiscard]] Tolerance with_outliers(double fraction) const;
    [[nodiscard]] Tolerance with_edge(int radius) const;
};
}  // namespace lsw::render
