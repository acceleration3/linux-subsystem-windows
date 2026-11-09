#pragma once

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

    [[nodiscard]] static std::vector<std::uint16_t> cube_indices() {
        return {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    }
};
}  // namespace lsw::render
