#pragma once

#include "render/common/image.hpp"
#include "render/common/tolerance.hpp"

#include <cstdint>
#include <string>

namespace lsw::render {
struct DiffResult {
    bool pass = false;
    std::uint64_t total = 0;
    std::uint64_t bad = 0;
    std::uint64_t forgiven = 0;
    double psnr = 0.0;
    double max_delta = 0.0;
    std::string detail;
};

class ImageDiff {
public:
    [[nodiscard]] static DiffResult compare(const Image& expected, const Image& actual, const Tolerance& tolerance,
                                            Image* diff_out = nullptr);

private:
    [[nodiscard]] static bool close(const Image& a, std::uint32_t ax, std::uint32_t ay, const Image& b,
                                    std::uint32_t bx, std::uint32_t by, const Tolerance& tolerance, double& worst);
};
}  // namespace lsw::render
