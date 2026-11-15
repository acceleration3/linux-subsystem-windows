#pragma once

#include "render/common/image.hpp"
#include "render/common/mat4.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace lsw::render {
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 1;
};

class CpuRaster {
public:
    enum class Cull { None, Back, Front };

    CpuRaster(std::uint32_t width, std::uint32_t height, std::uint32_t clear_rgba)
        : image_(Image::solid(width, height, clear_rgba)), depth_(static_cast<std::size_t>(width) * height, 1.0f) {}

    [[nodiscard]] static Vec4 transform(const Mat4& m, float x, float y, float z) {
        Vec4 out;
        out.x = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
        out.y = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
        out.z = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
        out.w = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15];
        return out;
    }

    [[nodiscard]] static std::uint32_t pack(float r, float g, float b, float a = 1.0f) {
        auto q = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }

    [[nodiscard]] float width() const { return static_cast<float>(image_.width()); }
    [[nodiscard]] float height() const { return static_cast<float>(image_.height()); }
    [[nodiscard]] Image& image() { return image_; }
    [[nodiscard]] Image take() { return std::move(image_); }

    void triangle(const Vec4 clip[3], Cull cull, bool depth_test, const std::function<std::uint32_t(int x, int y, bool front, float depth, const float bary[3])>& shade) {
        if (clip[0].w <= 1e-6f || clip[1].w <= 1e-6f || clip[2].w <= 1e-6f) {
            return;
        }
        float sx[3], sy[3], sz[3];
        for (int k = 0; k < 3; ++k) {
            sx[k] = (clip[k].x / clip[k].w * 0.5f + 0.5f) * width();
            sy[k] = (0.5f - clip[k].y / clip[k].w * 0.5f) * height();
            sz[k] = clip[k].z / clip[k].w;
        }
        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (area == 0.0f) {
            return;
        }
        const bool front = area > 0.0f;
        if ((cull == Cull::Back && !front) || (cull == Cull::Front && front)) {
            return;
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]}))));
        const int x1 = std::min(static_cast<int>(image_.width()) - 1, static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]}))));
        const int y1 = std::min(static_cast<int>(image_.height()) - 1, static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                float b[3];
                b[0] = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
                b[1] = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
                b[2] = 1.0f - b[0] - b[1];
                if (b[0] < 0.0f || b[1] < 0.0f || b[2] < 0.0f) {
                    continue;
                }
                const float z = b[0] * sz[0] + b[1] * sz[1] + b[2] * sz[2];
                if (z < 0.0f || z > 1.0f) {
                    continue;
                }
                float& stored = depth_[static_cast<std::size_t>(y) * image_.width() + static_cast<std::size_t>(x)];
                if (depth_test && z >= stored) {
                    continue;
                }
                stored = z;
                image_.set_rgba8(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), shade(x, y, front, z, b));
            }
        }
    }

    void line(float ax, float ay, float bx, float by, const std::function<std::uint32_t(float t)>& shade) {
        const float dx = bx - ax;
        const float dy = by - ay;
        const bool x_major = std::fabs(dx) >= std::fabs(dy);
        const float span = x_major ? dx : dy;
        if (span == 0.0f) {
            return;
        }
        const float lo = std::floor(std::min(x_major ? ax : ay, x_major ? bx : by));
        const float hi = std::ceil(std::max(x_major ? ax : ay, x_major ? bx : by));
        for (float m = lo; m <= hi; m += 1.0f) {
            const float c = m + 0.5f;
            const float t = ((c - (x_major ? ax : ay)) / span);
            if (t < 0.0f || t > 1.0f) {
                continue;
            }
            const float other = (x_major ? ay : ax) + t * (x_major ? dy : dx);
            const int px = x_major ? static_cast<int>(m) : static_cast<int>(std::floor(other));
            const int py = x_major ? static_cast<int>(std::floor(other)) : static_cast<int>(m);
            if (px < 0 || py < 0 || px >= static_cast<int>(image_.width()) || py >= static_cast<int>(image_.height())) {
                continue;
            }
            image_.set_rgba8(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py), shade(t));
        }
    }

private:
    Image image_;
    std::vector<float> depth_;
};
}  // namespace lsw::render
