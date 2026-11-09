#pragma once

#include "render/common/mat4.hpp"
#include "render/common/pattern.hpp"
#include "render/gl/gl.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsw::render::gl {

struct Fns {
    std::function<std::string(Kit&)> unsupported;
    std::function<bool(Kit&, std::string&)> setup;
    std::function<bool(Kit&, unsigned, std::string&)> frame;
    std::function<bool(Kit&, unsigned, CaptureSet&, std::string&)> capture;
};

class FnScene : public GlScene {
public:
    explicit FnScene(Fns fns) : fns_(std::move(fns)) {}

    std::string unsupported(Kit& kit) override { return fns_.unsupported ? fns_.unsupported(kit) : std::string(); }
    bool setup(Kit& kit, std::string& error) override { return fns_.setup ? fns_.setup(kit, error) : true; }
    bool frame(Kit& kit, unsigned index, std::string& error) override { return fns_.frame ? fns_.frame(kit, index, error) : true; }
    bool capture(Kit& kit, unsigned index, CaptureSet& out, std::string& error) override { return fns_.capture(kit, index, out, error); }

private:
    Fns fns_;
};

using SingleBody = std::function<bool(Kit&, CaptureSet&, std::string&)>;
using NeedFn = std::function<std::string(Kit&)>;

class Cases {
public:
    static void single(CaseSpec spec, SingleBody body, NeedFn need = {});
    static void multi(CaseSpec spec, std::function<Fns()> factory);
};

struct Shape {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] static Shape sphere(unsigned rings, unsigned segments, float radius);
    [[nodiscard]] static Shape torus(unsigned rings, unsigned segments, float major, float minor);
    [[nodiscard]] static Shape cube(float half);
    [[nodiscard]] static Shape quad(float x0, float y0, float x1, float y1, float z = 0.0f);
    [[nodiscard]] static Shape grid(unsigned cells, float half, float z = 0.0f);
    [[nodiscard]] static Shape cone(unsigned segments, float radius, float height);
};

struct GpuShape {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei count = 0;
    GLsizei vertex_count = 0;

    void draw(Kit& kit, GLenum mode = glc::TRIANGLES) const;
    void draw_instanced(Kit& kit, GLsizei instances, GLenum mode = glc::TRIANGLES) const;
};

class Gx {
public:
    static constexpr GLsizei kStride = 32;

    [[nodiscard]] static GpuShape upload(Kit& kit, const Shape& shape);
    [[nodiscard]] static GLuint make_texture(Kit& kit, unsigned size, unsigned kind, bool mips = true);
    [[nodiscard]] static Mat4 camera(float aspect = 1.0f, float distance = 4.0f, float yaw = 0.5f, float pitch = 0.4f);
    [[nodiscard]] static Mat4 ortho(float l, float r, float b, float t, float n, float f);
    static void clear(Kit& kit, float r, float g, float b, float a = 1.0f, bool depth = false, bool stencil = false);
    [[nodiscard]] static bool shot(Kit& kit, Target& target, CaptureSet& out, std::string& error, const char* label = "", std::size_t index = 0);
    [[nodiscard]] static bool shot_depth(Kit& kit, Target& target, CaptureSet& out, std::string& error, const char* label = "depth");
    [[nodiscard]] static bool shot_stencil(Kit& kit, Target& target, CaptureSet& out, std::string& error, const char* label = "stencil");
};

class Sources {
public:
    static constexpr const char* kLitVertex =
        "#version 450 core\n"
        "layout(location = 0) in vec3 in_pos;\n"
        "layout(location = 1) in vec3 in_nrm;\n"
        "layout(location = 2) in vec2 in_uv;\n"
        "uniform mat4 u_mvp;\n"
        "uniform mat4 u_model;\n"
        "out vec3 v_nrm;\n"
        "out vec2 v_uv;\n"
        "out vec3 v_world;\n"
        "void main() {\n"
        "    v_nrm = mat3(u_model) * in_nrm;\n"
        "    v_uv = in_uv;\n"
        "    v_world = (u_model * vec4(in_pos, 1.0)).xyz;\n"
        "    gl_Position = u_mvp * vec4(in_pos, 1.0);\n"
        "}\n";
    static constexpr const char* kLitFragment =
        "#version 450 core\n"
        "in vec3 v_nrm;\n"
        "in vec2 v_uv;\n"
        "in vec3 v_world;\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec3 u_light;\n"
        "uniform vec4 u_tint;\n"
        "layout(location = 0) out vec4 out_color;\n"
        "void main() {\n"
        "    float d = max(dot(normalize(v_nrm), normalize(u_light)), 0.0);\n"
        "    vec4 t = texture(u_tex, v_uv);\n"
        "    out_color = vec4(t.rgb * u_tint.rgb * (0.2 + 0.8 * d), t.a * u_tint.a);\n"
        "}\n";
    static constexpr const char* kFlatVertex =
        "#version 450 core\n"
        "layout(location = 0) in vec3 in_pos;\n"
        "uniform mat4 u_mvp;\n"
        "void main() { gl_Position = u_mvp * vec4(in_pos, 1.0); gl_PointSize = 8.0; }\n";
    static constexpr const char* kFlatFragment =
        "#version 450 core\n"
        "uniform vec4 u_color;\n"
        "layout(location = 0) out vec4 out_color;\n"
        "void main() { out_color = u_color; }\n";
    static constexpr const char* kScreenVertex =
        "#version 450 core\n"
        "out vec2 v_uv;\n"
        "void main() {\n"
        "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
        "    v_uv = p;\n"
        "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
};

}  // namespace lsw::render::gl
