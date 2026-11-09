#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace lsw::render {
struct ColoredVertex {
    float position[3];
    float color[3];
};

class Mesh {
public:
    [[nodiscard]] static std::vector<ColoredVertex> cube_vertices() {
        static const float p[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
        static const float c[8][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}, {0.3f, 0.3f, 0.3f}};
        std::vector<ColoredVertex> out(8);
        for (int i = 0; i < 8; ++i) {
            for (int k = 0; k < 3; ++k) {
                out[static_cast<std::size_t>(i)].position[k] = p[i][k];
                out[static_cast<std::size_t>(i)].color[k] = c[i][k];
            }
        }
        return out;
    }

    static void stamp(ColoredVertex* out, unsigned frame) {
        const float angle = static_cast<float>(frame) * 0.7854f;
        const float cx = 0.6f * std::cos(angle);
        const float cy = 0.6f * std::sin(angle);
        const float t = static_cast<float>(frame) / 7.0f;
        for (unsigned q = 0; q < 4; ++q) {
            const float a0 = angle + static_cast<float>(q) * 1.5708f;
            const float r = 0.16f + 0.01f * static_cast<float>(frame);
            ColoredVertex* v = out + q * 3;
            v[0] = {{cx, cy, 0.0f}, {1.0f, t, 0.2f}};
            v[1] = {{cx + r * std::cos(a0), cy + r * std::sin(a0), 0.0f}, {0.2f, 1.0f, t}};
            v[2] = {{cx + r * std::cos(a0 + 1.5708f), cy + r * std::sin(a0 + 1.5708f), 0.0f}, {t, 0.3f, 1.0f}};
        }
    }

    [[nodiscard]] static std::vector<std::uint16_t> cube_indices() {
        return {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    }
};
}  // namespace lsw::render
