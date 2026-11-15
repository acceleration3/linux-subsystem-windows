#pragma once

#include "render/common/case.hpp"
#include "render/gl/kit.hpp"

namespace lsw::suites {
class Suite;
}

namespace lsw::render::gl {
using GlScene = Scene<Kit>;
using GlSingleScene = SingleScene<Kit>;

template <class SceneType>
using GlRegister = Register<Kit, SceneType>;

class GlShaders {
public:
    static constexpr const char* kMeshVertex =
        "#version 330 core\n"
        "layout(location = 0) in vec3 in_pos;\n"
        "layout(location = 1) in vec3 in_color;\n"
        "uniform mat4 u_transform;\n"
        "out vec3 v_color;\n"
        "void main() {\n"
        "    gl_Position = u_transform * vec4(in_pos, 1.0);\n"
        "    v_color = in_color;\n"
        "}\n";
    static constexpr const char* kMeshFragment =
        "#version 330 core\n"
        "in vec3 v_color;\n"
        "layout(location = 0) out vec4 out_color;\n"
        "void main() {\n"
        "    out_color = vec4(v_color, 1.0);\n"
        "}\n";
    static constexpr const char* kMrtFragment =
        "#version 330 core\n"
        "in vec3 v_color;\n"
        "layout(location = 0) out vec4 out_color;\n"
        "layout(location = 1) out vec4 out_aux;\n"
        "void main() {\n"
        "    out_color = vec4(v_color, 1.0);\n"
        "    out_aux = vec4(v_color.bgr * 0.5 + 0.25, 0.75);\n"
        "}\n";
    static constexpr const char* kTextureVertex =
        "#version 330 core\n"
        "layout(location = 0) in vec2 in_pos;\n"
        "layout(location = 1) in vec2 in_uv;\n"
        "out vec2 v_uv;\n"
        "void main() {\n"
        "    gl_Position = vec4(in_pos, 0.0, 1.0);\n"
        "    v_uv = in_uv;\n"
        "}\n";
    static constexpr const char* kTextureFragment =
        "#version 330 core\n"
        "in vec2 v_uv;\n"
        "uniform sampler2D u_texture;\n"
        "uniform vec4 u_tint;\n"
        "layout(location = 0) out vec4 out_color;\n"
        "void main() {\n"
        "    out_color = texture(u_texture, v_uv) * u_tint;\n"
        "}\n";
};
}  // namespace lsw::render::gl

namespace lsw::suites::render::gl {
void add(Suite& suite);
}
