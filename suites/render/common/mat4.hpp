#pragma once

#include <cmath>

namespace lsw::render {
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    [[nodiscard]] static Mat4 identity() { return {}; }

    [[nodiscard]] Mat4 operator*(const Mat4& rhs) const {
        Mat4 out;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + r] * rhs.m[c * 4 + k];
                }
                out.m[c * 4 + r] = sum;
            }
        }
        return out;
    }

    [[nodiscard]] static Mat4 translate(float x, float y, float z) {
        Mat4 out;
        out.m[12] = x;
        out.m[13] = y;
        out.m[14] = z;
        return out;
    }

    [[nodiscard]] static Mat4 scale(float x, float y, float z) {
        Mat4 out;
        out.m[0] = x;
        out.m[5] = y;
        out.m[10] = z;
        return out;
    }

    [[nodiscard]] static Mat4 rotate_x(float radians) {
        Mat4 out;
        out.m[5] = std::cos(radians);
        out.m[6] = std::sin(radians);
        out.m[9] = -std::sin(radians);
        out.m[10] = std::cos(radians);
        return out;
    }

    [[nodiscard]] static Mat4 rotate_y(float radians) {
        Mat4 out;
        out.m[0] = std::cos(radians);
        out.m[2] = -std::sin(radians);
        out.m[8] = std::sin(radians);
        out.m[10] = std::cos(radians);
        return out;
    }

    [[nodiscard]] static Mat4 rotate_z(float radians) {
        Mat4 out;
        out.m[0] = std::cos(radians);
        out.m[1] = std::sin(radians);
        out.m[4] = -std::sin(radians);
        out.m[5] = std::cos(radians);
        return out;
    }

    [[nodiscard]] static Mat4 perspective_d3d(float fovy, float aspect, float near_plane, float far_plane) {
        Mat4 out = perspective(fovy, aspect, near_plane, far_plane, true);
        out.m[5] = -out.m[5];
        return out;
    }

    [[nodiscard]] static Mat4 perspective(float fovy, float aspect, float near_plane, float far_plane, bool vulkan) {
        const float f = 1.0f / std::tan(fovy * 0.5f);
        Mat4 out;
        for (float& v : out.m) {
            v = 0.0f;
        }
        out.m[0] = f / aspect;
        out.m[5] = vulkan ? -f : f;
        out.m[11] = -1.0f;
        if (vulkan) {
            out.m[10] = far_plane / (near_plane - far_plane);
            out.m[14] = far_plane * near_plane / (near_plane - far_plane);
        } else {
            out.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
            out.m[14] = 2.0f * far_plane * near_plane / (near_plane - far_plane);
        }
        return out;
    }
};
}  // namespace lsw::render
