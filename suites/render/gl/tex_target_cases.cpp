#include "render/gl/texrig.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec tex(const std::string& name) { return CaseSpec("tex", name).min_version(4, 6); }

using TexBody = std::function<bool(Kit&, TexRig&, std::string&)>;

void tex_case(const std::string& name, TexBody body, Tolerance tolerance = Tolerance::tight(1), NeedFn need = {}) {
    Cases::single(tex(name).tolerance(std::move(tolerance)), [body](Kit& kit, CaptureSet& out, std::string& error) {
        TexRig rig;
        if (!rig.open(kit, error)) {
            return false;
        }
        if (!body(kit, rig, error)) {
            return false;
        }
        return Gx::shot(kit, rig.target, out, error);
    }, std::move(need));
}

bool show(Kit& kit, TexRig& rig, const std::string& body, std::string& error, GLenum unit_target = 0, GLuint texture = 0, const char* uniforms = "uniform sampler2D u_tex;") {
    const GLuint program = rig.program(kit, body, error, uniforms);
    if (program == 0) {
        return false;
    }
    kit.u1i(program, "u_tex", 0);
    if (unit_target != 0) {
        kit.gl.glActiveTexture(glc::TEXTURE0);
        kit.gl.glBindTexture(unit_target, texture);
    }
    rig.draw(kit, program);
    return true;
}

const struct Batch16 {
    Batch16() {
        tex_case("target_1d", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_1D, glc::LINEAR);
            gl.glTexStorage1D(glc::TEXTURE_1D, 1, glc::RGBA8, 32);
            const auto data = TexRig::cells(32, 1);
            gl.glTexSubImage1D(glc::TEXTURE_1D, 0, 0, 32, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = c.y < 0.5 ? texelFetch(u_tex, int(c.x * 32.0), 0) : texture(u_tex, c.x); }", error, glc::TEXTURE_1D, t, "uniform sampler1D u_tex;");
        });

        tex_case("target_1d_array", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_1D_ARRAY, glc::LINEAR);
            gl.glTexStorage2D(glc::TEXTURE_1D_ARRAY, 1, glc::RGBA8, 16, 8);
            const auto data = TexRig::cells(16, 8, 1, 2);
            gl.glTexSubImage2D(glc::TEXTURE_1D_ARRAY, 0, 0, 0, 16, 8, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = c.x < 0.5 ? texelFetch(u_tex, ivec2(c.x * 32.0, int(c.y * 8.0)), 0) : texture(u_tex, vec2(c.x * 2.0 - 1.0, floor(c.y * 8.0))); }", error, glc::TEXTURE_1D_ARRAY, t, "uniform sampler1DArray u_tex;");
        });

        tex_case("target_2d_mip_levels", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST_MIPMAP_NEAREST);
            gl.glTexStorage2D(glc::TEXTURE_2D, 5, glc::RGBA8, 32, 32);
            const auto data = TexRig::cells(32, 32);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 32, 32, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            gl.glGenerateMipmap(glc::TEXTURE_2D);
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; int lod = int(c.y * 4.0); ivec2 s = textureSize(u_tex, lod); out_color = texelFetch(u_tex, ivec2(c.x * float(s.x), fract(c.y * 4.0) * float(s.y)), lod); }", error, glc::TEXTURE_2D, t);
        });

        tex_case("target_2d_array", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D_ARRAY, glc::LINEAR);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 1, glc::RGBA8, 16, 16, 6);
            const auto data = TexRig::cells(16, 16, 6);
            gl.glTexSubImage3D(glc::TEXTURE_2D_ARRAY, 0, 0, 0, 0, 16, 16, 6, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; int layer = int(c.x * 3.0) + 3 * int(c.y * 2.0); out_color = texture(u_tex, vec3(fract(c.x * 3.0), fract(c.y * 2.0), float(layer))); }", error, glc::TEXTURE_2D_ARRAY, t, "uniform sampler2DArray u_tex;");
        });

        tex_case("target_3d_filtered_slices", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_3D, glc::LINEAR);
            gl.glTexStorage3D(glc::TEXTURE_3D, 1, glc::RGBA8, 16, 16, 8);
            const auto data = TexRig::cells(16, 16, 8);
            gl.glTexSubImage3D(glc::TEXTURE_3D, 0, 0, 0, 0, 16, 16, 8, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = texture(u_tex, vec3(fract(c.x * 2.0), fract(c.y * 2.0), (float(int(c.x * 2.0) + 2 * int(c.y * 2.0)) + 0.5) / 4.0 + 0.1 * fract(c.x))); }", error, glc::TEXTURE_3D, t, "uniform sampler3D u_tex;");
        });

        const char* cube_show =
            "void main() {\n"
            "    vec2 c = gl_FragCoord.xy / 128.0;\n"
            "    int face = int(c.x * 3.0) + 3 * int(c.y * 2.0);\n"
            "    vec2 f = fract(vec2(c.x * 3.0, c.y * 2.0)) * 2.0 - 1.0;\n"
            "    vec3 dirs[6] = vec3[6](vec3(1.0, -f.y, -f.x), vec3(-1.0, -f.y, f.x), vec3(f.x, 1.0, f.y), vec3(f.x, -1.0, -f.y), vec3(f.x, -f.y, 1.0), vec3(-f.x, -f.y, -1.0));\n"
            "    out_color = texture(u_tex, dirs[face]);\n"
            "}\n";
        tex_case("target_cube_map", [cube_show](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_CUBE_MAP, glc::LINEAR);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::RGBA8, 16, 16);
            for (unsigned face = 0; face < 6; ++face) {
                const auto data = TexRig::cells(16, 16, 1 + face % 3, 4 + face % 2);
                gl.glTexSubImage2D(glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, data.data() + 16 * 16 * 4 * (face % 3) * 0);
            }
            return show(kit, rig, cube_show, error, glc::TEXTURE_CUBE_MAP, t, "uniform samplerCube u_tex;");
        });

        tex_case("target_cube_map_array", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_CUBE_MAP_ARRAY, glc::LINEAR);
            gl.glTexStorage3D(glc::TEXTURE_CUBE_MAP_ARRAY, 1, glc::RGBA8, 16, 16, 12);
            const auto data = TexRig::cells(16, 16, 12);
            gl.glTexSubImage3D(glc::TEXTURE_CUBE_MAP_ARRAY, 0, 0, 0, 0, 16, 16, 12, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            return show(kit, rig,
                        "void main() {\n"
                        "    vec2 c = gl_FragCoord.xy / 128.0;\n"
                        "    int face = int(c.x * 3.0) + 3 * int(c.y * 2.0);\n"
                        "    vec2 f = fract(vec2(c.x * 3.0, c.y * 2.0)) * 2.0 - 1.0;\n"
                        "    vec3 dirs[6] = vec3[6](vec3(1.0, -f.y, -f.x), vec3(-1.0, -f.y, f.x), vec3(f.x, 1.0, f.y), vec3(f.x, -1.0, -f.y), vec3(f.x, -f.y, 1.0), vec3(-f.x, -f.y, -1.0));\n"
                        "    out_color = texture(u_tex, vec4(dirs[face], float(int(gl_FragCoord.x) % 2)));\n"
                        "}\n", error, glc::TEXTURE_CUBE_MAP_ARRAY, t, "uniform samplerCubeArray u_tex;");
        });

        tex_case("target_rectangle", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_RECTANGLE, glc::LINEAR);
            gl.glTexStorage2D(glc::TEXTURE_RECTANGLE, 1, glc::RGBA8, 40, 24);
            const auto data = TexRig::cells(40, 24);
            gl.glTexSubImage2D(glc::TEXTURE_RECTANGLE, 0, 0, 0, 40, 24, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            gl.glTexParameteri(glc::TEXTURE_RECTANGLE, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glTexParameteri(glc::TEXTURE_RECTANGLE, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = c.y < 0.5 ? texture(u_tex, c * vec2(48.0, 28.0) * 2.0 - vec2(4.0, 2.0)) : texelFetch(u_tex, ivec2(c * vec2(40.0, 24.0) * vec2(1.0, 2.0))); }", error, glc::TEXTURE_RECTANGLE, t, "uniform sampler2DRect u_tex;");
        });

        tex_case("target_buffer_texture_range", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const auto data = TexRig::cells(64, 4);
            const GLuint buffer = kit.buffer(glc::TEXTURE_BUFFER, data.data(), data.size(), glc::STATIC_DRAW);
            GLuint t = 0;
            gl.glGenTextures(1, &t);
            gl.glBindTexture(glc::TEXTURE_BUFFER, t);
            kit.defer([&kit, t] { kit.gl.glDeleteTextures(1, &t); });
            const GLint align = kit.integer(glc::TEXTURE_BUFFER_OFFSET_ALIGNMENT);
            if (align > 1024) {
                error = "texture buffer alignment is too large";
                return false;
            }
            gl.glTexBufferRange(glc::TEXTURE_BUFFER, glc::RGBA8, buffer, 0, 64 * 4 * 4);
            GLuint ranged = 0;
            gl.glGenTextures(1, &ranged);
            gl.glBindTexture(glc::TEXTURE_BUFFER, ranged);
            kit.defer([&kit, ranged] { kit.gl.glDeleteTextures(1, &ranged); });
            gl.glTexBuffer(glc::TEXTURE_BUFFER, glc::RGBA8, buffer);
            static_cast<void>(ranged);
            gl.glActiveTexture(glc::TEXTURE1);
            gl.glBindTexture(glc::TEXTURE_BUFFER, t);
            gl.glActiveTexture(glc::TEXTURE0);
            const GLuint program = rig.program(kit,
                                               "void main() { vec2 c = gl_FragCoord.xy / 128.0; int i = int(c.x * 64.0) + 64 * int(c.y * 4.0); out_color = c.y < 0.5 ? texelFetch(u_a, i) : texelFetch(u_b, i % 128); }", error,
                                               "uniform samplerBuffer u_a;\nuniform samplerBuffer u_b;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_a", 0);
            kit.u1i(program, "u_b", 1);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_BUFFER, ranged);
            rig.draw(kit, program);
            return true;
        });

        for (const bool array : {false, true}) {
            tex_case(array ? "target_2d_multisample_array" : "target_2d_multisample", [array](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const GLenum kind = array ? glc::TEXTURE_2D_MULTISAMPLE_ARRAY : glc::TEXTURE_2D_MULTISAMPLE;
                GLuint t = 0;
                gl.glGenTextures(1, &t);
                gl.glBindTexture(kind, t);
                kit.defer([&kit, t] { kit.gl.glDeleteTextures(1, &t); });
                if (array) {
                    gl.glTexStorage3DMultisample(kind, 4, glc::RGBA8, 32, 32, 2, glc::TRUE_);
                } else {
                    gl.glTexStorage2DMultisample(kind, 4, glc::RGBA8, 32, 32, glc::TRUE_);
                }
                GLuint fbo = 0;
                gl.glGenFramebuffers(1, &fbo);
                kit.defer([&kit, fbo] { kit.gl.glDeleteFramebuffers(1, &fbo); });
                gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
                const GLuint flat = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
                if (flat == 0) {
                    return false;
                }
                kit.u1f(flat, "u_size", 1.0f);
                for (int layer = 0; layer < (array ? 2 : 1); ++layer) {
                    if (array) {
                        gl.glFramebufferTextureLayer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, t, 0, layer);
                    } else {
                        gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, kind, t, 0);
                    }
                    if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                        error = "multisample texture framebuffer incomplete";
                        return false;
                    }
                    gl.glViewport(0, 0, 32, 32);
                    gl.glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
                    gl.glClear(glc::COLOR_BUFFER_BIT);
                    gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(layer == 0 ? 9 : 6, 0.2f, 0.9f)));
                    gl.glDrawArrays(glc::TRIANGLES, 0, layer == 0 ? 9 : 6);
                }
                rig.target.bind(kit);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(kind, t);
                const std::string body = array ? "void main() { vec2 c = gl_FragCoord.xy / 128.0; int s = int(c.x * 2.0) + 2 * int(c.y * 2.0); out_color = texelFetch(u_tex, ivec3(fract(c * 2.0) * 32.0, 1), s % 4); if (c.y > 0.5) out_color = texelFetch(u_tex, ivec3(c.x * 32.0, (c.y - 0.5) * 64.0, 0), int(c.x * 4.0)); }"
                                                 : "void main() { vec2 c = gl_FragCoord.xy / 128.0; int s = int(c.x * 2.0) + 2 * int(c.y * 2.0); out_color = texelFetch(u_tex, ivec2(fract(c * 2.0) * 32.0), s); }";
                const GLuint program = rig.program(kit, body, error, array ? "uniform sampler2DMSArray u_tex;" : "uniform sampler2DMS u_tex;");
                if (program == 0) {
                    return false;
                }
                kit.u1i(program, "u_tex", 0);
                rig.draw(kit, program);
                return true;
            });
        }

        tex_case("storage_immutable_mips_and_level_params", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST_MIPMAP_LINEAR);
            gl.glTexStorage2D(glc::TEXTURE_2D, 6, glc::RGBA8, 32, 32);
            for (unsigned level = 0; level < 6; ++level) {
                const unsigned size = 32u >> level;
                std::vector<std::uint8_t> data(static_cast<std::size_t>(size) * size * 4);
                for (std::size_t i = 0; i < data.size(); i += 4) {
                    data[i] = static_cast<std::uint8_t>(255 - 45 * level);
                    data[i + 1] = static_cast<std::uint8_t>(40 * level + ((i / 4) % 2) * 60);
                    data[i + 2] = static_cast<std::uint8_t>(60 + 30 * level);
                    data[i + 3] = 255;
                }
                gl.glTexSubImage2D(glc::TEXTURE_2D, static_cast<GLint>(level), 0, 0, static_cast<GLsizei>(size), static_cast<GLsizei>(size), glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            }
            GLint immutable = 0, levels = 0, width2 = 0;
            gl.glGetTexParameteriv(glc::TEXTURE_2D, glc::TEXTURE_IMMUTABLE_FORMAT, &immutable);
            gl.glGetTexParameteriv(glc::TEXTURE_2D, glc::TEXTURE_IMMUTABLE_LEVELS, &levels);
            gl.glGetTexLevelParameteriv(glc::TEXTURE_2D, 2, glc::TEXTURE_WIDTH, &width2);
            if (immutable != 1 || levels != 6 || width2 != 8) {
                error = "immutable storage queries returned " + std::to_string(immutable) + "/" + std::to_string(levels) + "/" + std::to_string(width2);
                return false;
            }
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = textureLod(u_tex, c * 2.0, c.y * 6.0 + c.x * 0.5); }", error, glc::TEXTURE_2D, t);
        });

        tex_case("view_format_reinterpret_rgba8_as_r32ui", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint base = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 32, 32);
            const auto data = TexRig::cells(32, 32);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 32, 32, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            GLuint view = 0;
            gl.glGenTextures(1, &view);
            kit.defer([&kit, view] { kit.gl.glDeleteTextures(1, &view); });
            gl.glTextureView(view, glc::TEXTURE_2D, base, glc::R32UI, 0, 1, 0, 1);
            gl.glBindTexture(glc::TEXTURE_2D, view);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; uint v = texelFetch(u_tex, ivec2(c * 32.0), 0).r; out_color = vec4(float(v & 255u), float((v >> 8) & 255u), float((v >> 16) & 255u), float(v >> 24)) / 255.0; }",
                        error, glc::TEXTURE_2D, view, "uniform usampler2D u_tex;");
        });

        tex_case("view_cube_from_array_layers", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint base = TexRig::texture(kit, glc::TEXTURE_2D_ARRAY, glc::LINEAR);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 1, glc::RGBA8, 16, 16, 8);
            const auto data = TexRig::cells(16, 16, 8);
            gl.glTexSubImage3D(glc::TEXTURE_2D_ARRAY, 0, 0, 0, 0, 16, 16, 8, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            GLuint view = 0;
            gl.glGenTextures(1, &view);
            kit.defer([&kit, view] { kit.gl.glDeleteTextures(1, &view); });
            gl.glTextureView(view, glc::TEXTURE_CUBE_MAP, base, glc::RGBA8, 0, 1, 1, 6);
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, view);
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
            return show(kit, rig,
                        "void main() {\n"
                        "    vec2 c = gl_FragCoord.xy / 128.0;\n"
                        "    int face = int(c.x * 3.0) + 3 * int(c.y * 2.0);\n"
                        "    vec2 f = fract(vec2(c.x * 3.0, c.y * 2.0)) * 2.0 - 1.0;\n"
                        "    vec3 dirs[6] = vec3[6](vec3(1.0, -f.y, -f.x), vec3(-1.0, -f.y, f.x), vec3(f.x, 1.0, f.y), vec3(f.x, -1.0, -f.y), vec3(f.x, -f.y, 1.0), vec3(-f.x, -f.y, -1.0));\n"
                        "    out_color = texture(u_tex, dirs[face]);\n"
                        "}\n", error, glc::TEXTURE_CUBE_MAP, view, "uniform samplerCube u_tex;");
        });

        tex_case("view_layer_and_level_subrange", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint base = TexRig::texture(kit, glc::TEXTURE_2D_ARRAY, glc::NEAREST_MIPMAP_NEAREST);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 4, glc::RGBA8, 32, 32, 6);
            const auto data = TexRig::cells(32, 32, 6);
            gl.glTexSubImage3D(glc::TEXTURE_2D_ARRAY, 0, 0, 0, 0, 32, 32, 6, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            gl.glGenerateMipmap(glc::TEXTURE_2D_ARRAY);
            GLuint view = 0;
            gl.glGenTextures(1, &view);
            kit.defer([&kit, view] { kit.gl.glDeleteTextures(1, &view); });
            gl.glTextureView(view, glc::TEXTURE_2D_ARRAY, base, glc::RGBA8, 1, 2, 2, 3);
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, view);
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST_MIPMAP_NEAREST));
            return show(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; int layer = int(c.x * 3.0); out_color = textureLod(u_tex, vec3(fract(c.x * 3.0), c.y, float(layer)), c.y * 2.0); }", error,
                        glc::TEXTURE_2D_ARRAY, view, "uniform sampler2DArray u_tex;");
        });

        tex_case("dsa_create_upload_mipmap_bind_unit", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            GLuint t = 0;
            gl.glCreateTextures(glc::TEXTURE_2D, 1, &t);
            kit.defer([&kit, t] { kit.gl.glDeleteTextures(1, &t); });
            gl.glTextureStorage2D(t, 5, glc::RGBA8, 32, 32);
            const auto data = TexRig::cells(32, 32);
            gl.glTextureSubImage2D(t, 0, 0, 0, 32, 32, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            gl.glGenerateTextureMipmap(t);
            gl.glTextureParameteri(t, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR_MIPMAP_LINEAR));
            gl.glTextureParameteri(t, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glBindTextureUnit(0, t);
            const GLuint program = rig.program(kit, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = texture(u_tex, c * 3.0 + vec2(0.1, 0.2) * c.y); }", error, "uniform sampler2D u_tex;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            rig.draw(kit, program);
            return true;
        }, Tolerance::filter(4, 0.001));
    }
} kBatch16;

}  // namespace

}  // namespace lsw::render::gl
