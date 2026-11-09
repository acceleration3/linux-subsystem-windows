#include "render/gl/flat2d.hpp"
#include "render/gl/spirv_data.inc"

#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec stage(const char* name) { return CaseSpec("stages", name).min_version(4, 6); }

struct Stage {
    Target target;
    GLuint vao = 0;

    bool open(Kit& kit, std::string& error, unsigned size = 128) {
        TargetDesc desc;
        desc.width = size;
        desc.height = size;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        target.bind(kit);
        Gx::clear(kit, 0.08f, 0.08f, 0.12f);
        vao = kit.vertex_array();
        return true;
    }

    void bind(Kit& kit) {
        target.bind(kit);
        kit.gl.glBindVertexArray(vao);
    }
};

GLuint screen_program(Kit& kit, const char* fragment, std::string& error) { return kit.program(Sources::kScreenVertex, fragment, error); }

void fullscreen(Kit& kit) { kit.gl.glDrawArrays(glc::TRIANGLES, 0, 3); }

const char* kLayerVertex =
    "#version 460 core\n"
    "out vec2 vp;\n"
    "void main() {\n"
    "    const vec2 p[3] = vec2[3](vec2(-0.9, -0.9), vec2(0.9, -0.8), vec2(-0.2, 0.9));\n"
    "    vp = p[gl_VertexID];\n"
    "    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);\n"
    "}\n";

std::string layer_geometry(int layers) {
    return "#version 460 core\nlayout(triangles) in;\nlayout(triangle_strip, max_vertices = " + std::to_string(layers * 3) +
           ") out;\nin vec2 vp[];\nout vec2 gp;\nflat out int layer_id;\n"
           "void main() {\n"
           "    for (int l = 0; l < " + std::to_string(layers) + "; ++l) {\n"
           "        for (int i = 0; i < 3; ++i) {\n"
           "            gl_Layer = l;\n"
           "            layer_id = l;\n"
           "            gp = vp[i];\n"
           "            gl_Position = gl_in[i].gl_Position * vec4(1.0 - 0.1 * float(l), 1.0 - 0.1 * float(l), 1.0, 1.0);\n"
           "            EmitVertex();\n"
           "        }\n"
           "        EndPrimitive();\n"
           "    }\n"
           "}\n";
}

const char* kLayerFragment =
    "#version 460 core\n"
    "in vec2 gp;\n"
    "flat in int layer_id;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec3 base = vec3(float(layer_id % 3 == 0), float(layer_id % 3 == 1), float(layer_id % 3 == 2) + 0.3 * float(layer_id / 3));\n"
    "    out_color = vec4(mix(base, vec3(gp * 0.5 + 0.5, 0.5), 0.5), 1.0);\n"
    "}\n";

bool render_layers(Kit& kit, GLenum target_kind, GLuint texture, unsigned size, int layers, std::string& error) {
    Api& gl = kit.gl;
    GLuint fbo = 0;
    gl.glGenFramebuffers(1, &fbo);
    gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    kit.defer([&kit, fbo] { kit.gl.glDeleteFramebuffers(1, &fbo); });
    gl.glFramebufferTexture(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, texture, 0);
    const GLenum status = gl.glCheckFramebufferStatus(glc::FRAMEBUFFER);
    if (status != glc::FRAMEBUFFER_COMPLETE) {
        error = "layered framebuffer incomplete (0x" + std::to_string(status) + ")";
        return false;
    }
    static_cast<void>(target_kind);
    const std::string geometry = layer_geometry(layers);
    const GLuint program = kit.program({{glc::VERTEX_SHADER, kLayerVertex}, {glc::GEOMETRY_SHADER, geometry.c_str()}, {glc::FRAGMENT_SHADER, kLayerFragment}}, error);
    if (program == 0) {
        return false;
    }
    gl.glUseProgram(program);
    gl.glViewport(0, 0, static_cast<GLsizei>(size), static_cast<GLsizei>(size));
    gl.glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    gl.glClear(glc::COLOR_BUFFER_BIT);
    gl.glDrawArrays(glc::TRIANGLES, 0, 3);
    return kit.check("layered render", error);
}

const char* kArrayDisplay =
    "#version 460 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2DArray u_tex;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 c = gl_FragCoord.xy / 128.0;\n"
    "    int layer = int(c.x >= 0.5) + 2 * int(c.y >= 0.5);\n"
    "    ivec2 t = ivec2(fract(c * 2.0) * 64.0);\n"
    "    out_color = texelFetch(u_tex, ivec3(t, layer), 0);\n"
    "}\n";

const char* kCubeDisplay =
    "#version 460 core\n"
    "in vec2 v_uv;\n"
    "uniform samplerCube u_tex;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 c = gl_FragCoord.xy / 128.0;\n"
    "    int col = int(c.x * 3.0);\n"
    "    int row = int(c.y * 2.0);\n"
    "    int face = col + 3 * row;\n"
    "    vec2 f = fract(vec2(c.x * 3.0, c.y * 2.0)) * 2.0 - 1.0;\n"
    "    vec3 dirs[6] = vec3[6](vec3(1.0, -f.y, -f.x), vec3(-1.0, -f.y, f.x), vec3(f.x, 1.0, f.y), vec3(f.x, -1.0, -f.y), vec3(f.x, -f.y, 1.0), vec3(-f.x, -f.y, -1.0));\n"
    "    out_color = texture(u_tex, dirs[face]);\n"
    "}\n";

const char* kVolumeDisplay =
    "#version 460 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler3D u_tex;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 c = gl_FragCoord.xy / 128.0;\n"
    "    int slice = int(c.x >= 0.5) + 2 * int(c.y >= 0.5);\n"
    "    ivec2 t = ivec2(fract(c * 2.0) * 32.0);\n"
    "    out_color = texelFetch(u_tex, ivec3(t, slice), 0);\n"
    "}\n";

const struct Batch10 {
    Batch10() {
        Cases::single(stage("gs_layered_texture_array"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            GLuint texture = 0;
            gl.glGenTextures(1, &texture);
            kit.defer([&kit, texture] { kit.gl.glDeleteTextures(1, &texture); });
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, texture);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 1, glc::RGBA8, 64, 64, 4);
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            if (!render_layers(kit, glc::TEXTURE_2D_ARRAY, texture, 64, 4, error)) {
                return false;
            }
            s.bind(kit);
            const GLuint program = screen_program(kit, kArrayDisplay, error);
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, texture);
            fullscreen(kit);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("gs_layered_cube_map"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            GLuint texture = 0;
            gl.glGenTextures(1, &texture);
            kit.defer([&kit, texture] { kit.gl.glDeleteTextures(1, &texture); });
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, texture);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::RGBA8, 64, 64);
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            if (!render_layers(kit, glc::TEXTURE_CUBE_MAP, texture, 64, 6, error)) {
                return false;
            }
            s.bind(kit);
            const GLuint program = screen_program(kit, kCubeDisplay, error);
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, texture);
            fullscreen(kit);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("gs_layered_3d_slices"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            GLuint texture = 0;
            gl.glGenTextures(1, &texture);
            kit.defer([&kit, texture] { kit.gl.glDeleteTextures(1, &texture); });
            gl.glBindTexture(glc::TEXTURE_3D, texture);
            gl.glTexStorage3D(glc::TEXTURE_3D, 1, glc::RGBA8, 32, 32, 4);
            gl.glTexParameteri(glc::TEXTURE_3D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            if (!render_layers(kit, glc::TEXTURE_3D, texture, 32, 4, error)) {
                return false;
            }
            s.bind(kit);
            const GLuint program = screen_program(kit, kVolumeDisplay, error);
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_3D, texture);
            fullscreen(kit);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("gs_instanced_invocations"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            static const char* gs =
                "#version 460 core\n"
                "layout(triangles, invocations = 4) in;\n"
                "layout(triangle_strip, max_vertices = 3) out;\n"
                "in vec2 vp[];\n"
                "out vec3 gc;\n"
                "void main() {\n"
                "    vec2 o = vec2(float(gl_InvocationID % 2) - 0.5, float(gl_InvocationID / 2) - 0.5) * 0.9;\n"
                "    for (int i = 0; i < 3; ++i) {\n"
                "        gl_Position = vec4(vp[i] * 0.5 + o, 0.0, 1.0);\n"
                "        gc = vec3(float(gl_InvocationID) / 3.0, vp[i] * 0.5 + 0.5);\n"
                "        EmitVertex();\n"
                "    }\n"
                "    EndPrimitive();\n"
                "}\n";
            static const char* fs =
                "#version 460 core\nin vec3 gc;\nlayout(location = 0) out vec4 out_color;\nvoid main() { out_color = vec4(gc, 1.0); }\n";
            const GLuint program = kit.program({{glc::VERTEX_SHADER, kLayerVertex}, {glc::GEOMETRY_SHADER, gs}, {glc::FRAGMENT_SHADER, fs}}, error);
            if (program == 0) {
                return false;
            }
            kit.gl.glUseProgram(program);
            fullscreen(kit);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("gs_viewport_index_array"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* gs =
                "#version 460 core\n"
                "layout(triangles, invocations = 4) in;\n"
                "layout(triangle_strip, max_vertices = 3) out;\n"
                "in vec2 vp[];\n"
                "out vec3 gc;\n"
                "void main() {\n"
                "    for (int i = 0; i < 3; ++i) {\n"
                "        gl_ViewportIndex = gl_InvocationID;\n"
                "        gl_Position = vec4(vp[i], 0.0, 1.0);\n"
                "        gc = vec3(float(gl_InvocationID) / 3.0, vp[i] * 0.5 + 0.5);\n"
                "        EmitVertex();\n"
                "    }\n"
                "    EndPrimitive();\n"
                "}\n";
            static const char* fs =
                "#version 460 core\nin vec3 gc;\nlayout(location = 0) out vec4 out_color;\nvoid main() { out_color = vec4(gc, 1.0); }\n";
            const GLuint program = kit.program({{glc::VERTEX_SHADER, kLayerVertex}, {glc::GEOMETRY_SHADER, gs}, {glc::FRAGMENT_SHADER, fs}}, error);
            if (program == 0) {
                return false;
            }
            const float views[16] = {0, 0, 64, 64, 64, 0, 64, 64, 0, 64, 64, 64, 64, 64, 64, 64};
            gl.glViewportArrayv(0, 4, views);
            gl.glUseProgram(program);
            fullscreen(kit);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("gs_point_to_quad_sprites"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            static const char* vs =
                "#version 460 core\n"
                "layout(location = 0) in vec2 in_pos;\n"
                "layout(location = 1) in vec3 in_col;\n"
                "out vec3 vc;\n"
                "void main() { vc = in_col; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";
            static const char* gs =
                "#version 460 core\n"
                "layout(points) in;\n"
                "layout(triangle_strip, max_vertices = 4) out;\n"
                "in vec3 vc[];\n"
                "out vec3 gc;\n"
                "out vec2 guv;\n"
                "void main() {\n"
                "    for (int i = 0; i < 4; ++i) {\n"
                "        vec2 c = vec2(float(i & 1), float(i >> 1));\n"
                "        guv = c;\n"
                "        gc = vc[0];\n"
                "        gl_Position = gl_in[0].gl_Position + vec4((c * 2.0 - 1.0) * 0.14, 0.0, 0.0);\n"
                "        EmitVertex();\n"
                "    }\n"
                "    EndPrimitive();\n"
                "}\n";
            static const char* fs =
                "#version 460 core\nin vec3 gc;\nin vec2 guv;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { float d = length(guv - 0.5); if (d > 0.5) discard; out_color = vec4(gc * (1.0 - d), 1.0); }\n";
            const GLuint program = kit.program({{glc::VERTEX_SHADER, vs}, {glc::GEOMETRY_SHADER, gs}, {glc::FRAGMENT_SHADER, fs}}, error);
            if (program == 0) {
                return false;
            }
            const GLuint vao = Flat2d::upload(kit, Flat2d::ring(10, 0.4f, 0.75f));
            kit.gl.glUseProgram(program);
            kit.gl.glBindVertexArray(vao);
            kit.gl.glDrawArrays(glc::POINTS, 0, 10);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("gs_triangle_to_wireframe").tolerance(Tolerance::raster(2, 1)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            static const char* gs =
                "#version 460 core\n"
                "layout(triangles) in;\n"
                "layout(line_strip, max_vertices = 4) out;\n"
                "in vec3 vc[];\n"
                "out vec3 gc;\n"
                "void main() {\n"
                "    for (int i = 0; i < 4; ++i) {\n"
                "        gl_Position = gl_in[i % 3].gl_Position;\n"
                "        gc = vc[i % 3];\n"
                "        EmitVertex();\n"
                "    }\n"
                "    EndPrimitive();\n"
                "}\n";
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec3 in_col;\nout vec3 vc;\n"
                "void main() { vc = in_col; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";
            static const char* fs =
                "#version 460 core\nin vec3 gc;\nlayout(location = 0) out vec4 out_color;\nvoid main() { out_color = vec4(gc, 1.0); }\n";
            const GLuint program = kit.program({{glc::VERTEX_SHADER, vs}, {glc::GEOMETRY_SHADER, gs}, {glc::FRAGMENT_SHADER, fs}}, error);
            if (program == 0) {
                return false;
            }
            const GLuint vao = Flat2d::upload(kit, Flat2d::ring(12, 0.35f, 0.85f));
            kit.gl.glUseProgram(program);
            kit.gl.glBindVertexArray(vao);
            kit.gl.glDrawArrays(glc::TRIANGLES, 0, 12);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("separate_programs_pipeline"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* vs =
                "#version 460 core\n"
                "layout(location = 0) in vec2 in_pos;\n"
                "layout(location = 1) in vec3 in_col;\n"
                "layout(location = 0) uniform vec2 u_offset;\n"
                "layout(location = 0) out vec3 v_col;\n"
                "out gl_PerVertex { vec4 gl_Position; };\n"
                "void main() { v_col = in_col; gl_Position = vec4(in_pos * 0.5 + u_offset, 0.0, 1.0); }\n";
            static const char* fs_a =
                "#version 460 core\nlayout(location = 0) in vec3 v_col;\nlayout(location = 1) uniform vec4 u_mul;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { out_color = vec4(v_col * u_mul.rgb, 1.0); }\n";
            static const char* fs_b =
                "#version 460 core\nlayout(location = 0) in vec3 v_col;\nlayout(location = 1) uniform vec4 u_mul;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { float g = dot(v_col, vec3(0.3, 0.59, 0.11)); out_color = vec4(vec3(g) * u_mul.rgb, 1.0); }\n";
            const GLuint program_v = kit.program({{glc::VERTEX_SHADER, vs}}, error, true);
            const GLuint program_a = program_v != 0 ? kit.program({{glc::FRAGMENT_SHADER, fs_a}}, error, true) : 0;
            const GLuint program_b = program_a != 0 ? kit.program({{glc::FRAGMENT_SHADER, fs_b}}, error, true) : 0;
            if (program_b == 0) {
                return false;
            }
            GLuint pipeline = 0;
            gl.glGenProgramPipelines(1, &pipeline);
            kit.defer([&kit, pipeline] { kit.gl.glDeleteProgramPipelines(1, &pipeline); });
            gl.glBindProgramPipeline(pipeline);
            gl.glUseProgramStages(pipeline, glc::VERTEX_SHADER_BIT, program_v);
            const GLuint vao = Flat2d::upload(kit, Flat2d::tile(1));
            const GLushort idx[6] = {0, 1, 2, 0, 2, 3};
            kit.buffer(glc::ELEMENT_ARRAY_BUFFER, idx, sizeof idx, glc::STATIC_DRAW);
            gl.glBindVertexArray(vao);
            const GLuint fragments[4] = {program_a, program_b, program_a, program_b};
            for (int i = 0; i < 4; ++i) {
                gl.glUseProgramStages(pipeline, glc::FRAGMENT_SHADER_BIT, fragments[i]);
                const float ox = -0.5f + static_cast<float>(i % 2);
                const float oy = -0.5f + static_cast<float>(i / 2);
                gl.glActiveShaderProgram(pipeline, program_v);
                gl.glUniform2f(0, ox, oy);
                gl.glProgramUniform4f(fragments[i], 1, 1.0f - 0.2f * static_cast<float>(i), 0.9f, 0.7f + 0.1f * static_cast<float>(i), 0);
                gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
            }
            gl.glBindProgramPipeline(0);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("subroutine_uniforms"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* fs =
                "#version 460 core\n"
                "in vec2 v_uv;\n"
                "subroutine vec3 Shade(vec2 p);\n"
                "subroutine(Shade) vec3 stripes(vec2 p) { return vec3(step(0.5, fract(p.x * 6.0)), 0.4, 0.8); }\n"
                "subroutine(Shade) vec3 rings(vec2 p) { return vec3(0.9, fract(length(p - 0.5) * 8.0), 0.2); }\n"
                "subroutine(Shade) vec3 gradient(vec2 p) { return vec3(p, 1.0 - p.x); }\n"
                "subroutine uniform Shade u_shade;\n"
                "uniform vec4 u_rect;\n"
                "layout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 c = gl_FragCoord.xy / 128.0;\n"
                "    if (c.x < u_rect.x || c.x > u_rect.z || c.y < u_rect.y || c.y > u_rect.w) discard;\n"
                "    out_color = vec4(u_shade((c - u_rect.xy) / (u_rect.zw - u_rect.xy)), 1.0);\n"
                "}\n";
            const GLuint program = screen_program(kit, fs, error);
            if (program == 0) {
                return false;
            }
            gl.glUseProgram(program);
            const GLint location = gl.glGetSubroutineUniformLocation(program, glc::FRAGMENT_SHADER, "u_shade");
            const char* names[3] = {"stripes", "rings", "gradient"};
            for (int i = 0; i < 3; ++i) {
                const GLuint index = gl.glGetSubroutineIndex(program, glc::FRAGMENT_SHADER, names[i]);
                gl.glUniformSubroutinesuiv(glc::FRAGMENT_SHADER, 1, &index);
                kit.u4f(program, "u_rect", 0.05f + 0.3f * static_cast<float>(i), 0.1f + 0.1f * static_cast<float>(i), 0.35f + 0.3f * static_cast<float>(i), 0.9f - 0.1f * static_cast<float>(i));
                gl.glUniformSubroutinesuiv(glc::FRAGMENT_SHADER, 1, &index);
                fullscreen(kit);
            }
            static_cast<void>(location);
            return Gx::shot(kit, s.target, out, error);
        });

        const auto spirv_case = [](const char* name, bool specialized) {
            Cases::single(stage(name).extension("GL_ARB_gl_spirv"), [specialized](Kit& kit, CaptureSet& out, std::string& error) {
                Stage s;
                if (!s.open(kit, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                GLuint shaders[2] = {gl.glCreateShader(glc::VERTEX_SHADER), gl.glCreateShader(glc::FRAGMENT_SHADER)};
                gl.glShaderBinary(1, &shaders[0], glc::SHADER_BINARY_FORMAT_SPIR_V, kSpirvVertex, static_cast<GLsizei>(sizeof kSpirvVertex));
                gl.glShaderBinary(1, &shaders[1], glc::SHADER_BINARY_FORMAT_SPIR_V, kSpirvFragment, static_cast<GLsizei>(sizeof kSpirvFragment));
                const GLuint constant_index[2] = {0, 1};
                const GLuint constant_value[2] = {0x3f000000u, 1u};
                const GLuint frag_index[1] = {2};
                const GLuint frag_value[1] = {0x3f19999au};
                if (specialized) {
                    gl.glSpecializeShader(shaders[0], "main", 2, constant_index, constant_value);
                    gl.glSpecializeShader(shaders[1], "main", 1, frag_index, frag_value);
                } else {
                    gl.glSpecializeShader(shaders[0], "main", 0, nullptr, nullptr);
                    gl.glSpecializeShader(shaders[1], "main", 0, nullptr, nullptr);
                }
                GLint ok0 = 0, ok1 = 0;
                gl.glGetShaderiv(shaders[0], glc::COMPILE_STATUS, &ok0);
                gl.glGetShaderiv(shaders[1], glc::COMPILE_STATUS, &ok1);
                if (ok0 == 0 || ok1 == 0) {
                    error = "SPIR-V specialization failed";
                    return false;
                }
                const GLuint program = kit.link({shaders[0], shaders[1]}, error, false);
                if (program == 0) {
                    return false;
                }
                gl.glUseProgram(program);
                const GLuint vao = Flat2d::upload(kit, Flat2d::tile(1));
                const GLushort idx[6] = {0, 1, 2, 0, 2, 3};
                kit.buffer(glc::ELEMENT_ARRAY_BUFFER, idx, sizeof idx, glc::STATIC_DRAW);
                gl.glBindVertexArray(vao);
                for (int i = 0; i < 4; ++i) {
                    gl.glUniform2f(0, -0.5f + static_cast<float>(i % 2), -0.5f + static_cast<float>(i / 2));
                    gl.glUniform4f(1, 1.0f, 1.0f - 0.2f * static_cast<float>(i), 1.0f, 1.0f);
                    gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
                }
                return Gx::shot(kit, s.target, out, error);
            });
        };
        spirv_case("spirv_modules", false);
        spirv_case("spirv_specialization_constants", true);

        Cases::single(stage("program_binary_roundtrip"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* fs =
                "#version 460 core\nin vec2 v_uv;\nuniform vec3 u_tint;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = vec4(u_tint * (0.4 + 0.6 * fract(c.x * 5.0 + c.y * 3.0)), 1.0); }\n";
            const GLuint program = screen_program(kit, fs, error);
            if (program == 0) {
                return false;
            }
            GLint length = 0;
            gl.glGetProgramiv(program, glc::PROGRAM_BINARY_LENGTH, &length);
            std::vector<std::uint8_t> binary(static_cast<std::size_t>(length));
            GLenum format = 0;
            GLsizei written = 0;
            gl.glGetProgramBinary(program, length, &written, &format, binary.data());
            const GLuint restored = gl.glCreateProgram();
            kit.defer([&kit, restored] { kit.gl.glDeleteProgram(restored); });
            gl.glProgramBinary(restored, format, binary.data(), written);
            GLint ok = 0;
            gl.glGetProgramiv(restored, glc::LINK_STATUS, &ok);
            if (ok == 0) {
                error = "glProgramBinary did not restore a linked program";
                return false;
            }
            gl.glEnable(glc::SCISSOR_TEST);
            gl.glScissor(0, 0, 64, 128);
            kit.u3f(program, "u_tint", 0.9f, 0.5f, 0.3f);
            fullscreen(kit);
            gl.glScissor(64, 0, 64, 128);
            gl.glUseProgram(restored);
            kit.u3f(restored, "u_tint", 0.9f, 0.5f, 0.3f);
            fullscreen(kit);
            gl.glDisable(glc::SCISSOR_TEST);
            return Gx::shot(kit, s.target, out, error);
        }, [](Kit& kit) -> std::string { return kit.integer(glc::NUM_PROGRAM_BINARY_FORMATS) == 0 ? "no program binary formats are reported" : std::string(); });
    }
} kBatch10;

}  // namespace

}  // namespace lsw::render::gl
