#pragma once

#include "render/gl/toolkit.hpp"

#include <cmath>

namespace lsw::render::gl {

struct V2 {
    float x, y, r, g, b;
};

class Flat2d {
public:
    static constexpr const char* kVertex =
        "#version 460 core\n"
        "layout(location = 0) in vec2 in_pos;\n"
        "layout(location = 1) in vec3 in_col;\n"
        "uniform float u_size;\n"
        "out vec3 v_col;\n"
        "void main() {\n"
        "    v_col = in_col;\n"
        "    gl_PointSize = u_size;\n"
        "    gl_Position = vec4(in_pos, 0.0, 1.0);\n"
        "}\n";
    static constexpr const char* kFragment =
        "#version 460 core\n"
        "in vec3 v_col;\n"
        "layout(location = 0) out vec4 out_color;\n"
        "void main() { out_color = vec4(v_col, 1.0); }\n";
    static constexpr const char* kParamsVertex =
        "#version 460 core\n"
        "layout(location = 0) in vec2 in_pos;\n"
        "layout(location = 1) in vec3 in_col;\n"
        "uniform int u_cell;\n"
        "out vec3 v_col;\n"
        "void main() {\n"
        "    int cell = gl_InstanceID + gl_BaseInstance + 4 * gl_DrawID + u_cell;\n"
        "    vec2 o = vec2(float(cell % 4) * 0.5 - 0.75, float(cell / 4) * 0.5 - 0.75);\n"
        "    v_col = in_col * (1.0 - 0.2 * float(gl_BaseVertex / 4));\n"
        "    gl_Position = vec4(in_pos * 0.5 + o, 0.0, 1.0);\n"
        "}\n";

    static std::vector<V2> ring(unsigned n, float inner, float outer, float phase = 0.0f) {
        std::vector<V2> out;
        for (unsigned i = 0; i < n; ++i) {
            const float a = phase + 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
            const float r = (i % 2 == 0) ? outer : inner;
            const float h = 6.0f * static_cast<float>(i) / static_cast<float>(n);
            const auto clamp = [](float v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : v); };
            out.push_back({r * std::cos(a), r * std::sin(a), clamp(std::fabs(h - 3) - 1), clamp(2 - std::fabs(h - 2)), clamp(2 - std::fabs(h - 4))});
        }
        return out;
    }

    static GLuint upload(Kit& kit, const std::vector<V2>& verts, GLuint* vbo_out = nullptr) {
        Api& gl = kit.gl;
        const GLuint vao = kit.vertex_array();
        const GLuint vbo = kit.buffer(glc::ARRAY_BUFFER, verts.data(), verts.size() * sizeof(V2), glc::STATIC_DRAW);
        gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(V2), nullptr);
        gl.glEnableVertexAttribArray(0);
        gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(8));
        gl.glEnableVertexAttribArray(1);
        if (vbo_out != nullptr) {
            *vbo_out = vbo;
        }
        return vao;
    }

    static std::vector<V2> tile(unsigned sets) {
        std::vector<V2> out;
        for (unsigned s = 0; s < sets; ++s) {
            const float k = 1.0f - 0.25f * static_cast<float>(s);
            out.push_back({-0.4f, -0.4f, 1.0f * k, 0.3f * k, 0.2f * k});
            out.push_back({0.4f, -0.4f, 0.3f * k, 1.0f * k, 0.2f * k});
            out.push_back({0.4f, 0.4f, 0.2f * k, 0.4f * k, 1.0f * k});
            out.push_back({-0.4f, 0.4f, 1.0f * k, 1.0f * k, 0.3f * k});
        }
        return out;
    }
};

}  // namespace lsw::render::gl
