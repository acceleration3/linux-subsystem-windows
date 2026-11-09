#include "render/gl/texrig.hpp"

#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec tex(const std::string& name) { return CaseSpec("tex", name).min_version(4, 6); }

using TexBody = std::function<bool(Kit&, TexRig&, std::string&)>;

void tex_case(const std::string& name, TexBody body, Tolerance tolerance = Tolerance::tight(1), NeedFn need = {}, CaseSpec extra = CaseSpec("", "")) {
    static_cast<void>(extra);
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

GLuint bind_program(Kit& kit, TexRig& rig, const std::string& body, const char* uniforms, std::string& error) {
    const GLuint program = rig.program(kit, body, error, uniforms);
    if (program != 0) {
        kit.u1i(program, "u_tex", 0);
    }
    return program;
}

GLuint checker_texture(Kit& kit, unsigned size, unsigned block, GLenum filter, unsigned levels) {
    Api& gl = kit.gl;
    const auto data = TexRig::cells(size, size, 1, block);
    const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, filter);
    gl.glTexStorage2D(glc::TEXTURE_2D, static_cast<GLsizei>(levels), glc::RGBA8, static_cast<GLsizei>(size), static_cast<GLsizei>(size));
    gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(size), static_cast<GLsizei>(size), glc::RGBA, glc::UNSIGNED_BYTE, data.data());
    if (levels > 1) {
        gl.glGenerateMipmap(glc::TEXTURE_2D);
    }
    return t;
}

const char* kMinify =
    "void main() {\n"
    "    vec2 c = gl_FragCoord.xy / 128.0;\n"
    "    mat2 r = mat2(0.94, 0.34, -0.34, 0.94);\n"
    "    out_color = texture(u_tex, r * c * 5.0);\n"
    "}\n";

const struct Batch19 {
    Batch19() {
        struct Filter {
            const char* name;
            GLenum min;
            GLenum mag;
        };
        for (const Filter& f : {Filter{"min_nearest", glc::NEAREST, glc::NEAREST}, Filter{"min_linear", glc::LINEAR, glc::LINEAR},
                                Filter{"min_nearest_mip_nearest", glc::NEAREST_MIPMAP_NEAREST, glc::NEAREST}, Filter{"min_linear_mip_nearest", glc::LINEAR_MIPMAP_NEAREST, glc::LINEAR},
                                Filter{"min_nearest_mip_linear", glc::NEAREST_MIPMAP_LINEAR, glc::NEAREST}, Filter{"min_linear_mip_linear", glc::LINEAR_MIPMAP_LINEAR, glc::LINEAR}}) {
            tex_case(std::string("sampler_filter_") + f.name, [f](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const GLuint t = checker_texture(kit, 64, 2, glc::NEAREST, 7);
                GLuint sampler = 0;
                gl.glGenSamplers(1, &sampler);
                kit.defer([&kit, sampler] { kit.gl.glDeleteSamplers(1, &sampler); });
                gl.glSamplerParameteri(sampler, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(f.min));
                gl.glSamplerParameteri(sampler, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(f.mag));
                gl.glSamplerParameteri(sampler, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::REPEAT));
                gl.glSamplerParameteri(sampler, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::REPEAT));
                gl.glBindSampler(0, sampler);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, t);
                const GLuint program = bind_program(kit, rig, kMinify, "uniform sampler2D u_tex;", error);
                if (program == 0) {
                    return false;
                }
                rig.draw(kit, program);
                gl.glBindSampler(0, 0);
                return true;
            }, Tolerance::filter(4, 0.001));
        }

        for (const bool linear : {false, true}) {
            tex_case(linear ? "sampler_mag_linear" : "sampler_mag_nearest", [linear](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const GLuint t = checker_texture(kit, 8, 1, linear ? glc::LINEAR : glc::NEAREST, 1);
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
                gl.glBindTexture(glc::TEXTURE_2D, t);
                const GLuint program = bind_program(kit, rig, "void main() { out_color = texture(u_tex, gl_FragCoord.xy / 128.0); }", "uniform sampler2D u_tex;", error);
                if (program == 0) {
                    return false;
                }
                rig.draw(kit, program);
                return true;
            }, Tolerance::filter(4, 0.001));
        }

        tex_case("sampler_anisotropy_1_4_16", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = checker_texture(kit, 128, 8, glc::NEAREST, 8);
            GLfloat max_aniso = 1.0f;
            gl.glGetFloatv(glc::MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso);
            const GLuint program = bind_program(kit, rig,
                                                "void main() { vec2 c = gl_FragCoord.xy / 128.0; float y = fract(c.y * 3.0); vec2 p = vec2((c.x - 0.5) / (y + 0.15), 0.4 / (y + 0.15)); out_color = texture(u_tex, p + 0.5); }",
                                                "uniform sampler2D u_tex;", error);
            if (program == 0) {
                return false;
            }
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_2D, t);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR_MIPMAP_LINEAR));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::REPEAT));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::REPEAT));
            gl.glEnable(glc::SCISSOR_TEST);
            const float levels[3] = {1.0f, 4.0f, 16.0f};
            for (int i = 0; i < 3; ++i) {
                gl.glScissor(0, i * 43, 128, 43);
                gl.glTexParameterf(glc::TEXTURE_2D, glc::TEXTURE_MAX_ANISOTROPY, std::min(levels[i], max_aniso));
                rig.draw(kit, program);
            }
            gl.glDisable(glc::SCISSOR_TEST);
            return true;
        }, Tolerance::filter(6, 0.002), [](Kit& kit) { return kit.has_extension("GL_EXT_texture_filter_anisotropic") || kit.at_least(4, 6) ? std::string() : "anisotropic filtering is not available"; });

        struct Wrap {
            const char* name;
            GLenum mode;
        };
        for (const Wrap& w : {Wrap{"repeat", glc::REPEAT}, Wrap{"mirrored_repeat", glc::MIRRORED_REPEAT}, Wrap{"clamp_to_edge", glc::CLAMP_TO_EDGE},
                              Wrap{"clamp_to_border", glc::CLAMP_TO_BORDER}, Wrap{"mirror_clamp_to_edge", glc::MIRROR_CLAMP_TO_EDGE}}) {
            tex_case(std::string("wrap_") + w.name, [w](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const GLuint t = checker_texture(kit, 8, 2, glc::LINEAR, 1);
                GLuint sampler = 0;
                gl.glGenSamplers(1, &sampler);
                kit.defer([&kit, sampler] { kit.gl.glDeleteSamplers(1, &sampler); });
                gl.glSamplerParameteri(sampler, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR));
                gl.glSamplerParameteri(sampler, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
                gl.glSamplerParameteri(sampler, glc::TEXTURE_WRAP_S, static_cast<GLint>(w.mode));
                gl.glSamplerParameteri(sampler, glc::TEXTURE_WRAP_T, static_cast<GLint>(w.mode == glc::REPEAT ? glc::MIRRORED_REPEAT : w.mode));
                const float border[4] = {0.9f, 0.2f, 0.5f, 1.0f};
                gl.glSamplerParameterfv(sampler, glc::TEXTURE_BORDER_COLOR, border);
                gl.glBindSampler(0, sampler);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, t);
                const GLuint program = bind_program(kit, rig, "void main() { out_color = texture(u_tex, gl_FragCoord.xy / 128.0 * 3.0 - 1.0); }", "uniform sampler2D u_tex;", error);
                if (program == 0) {
                    return false;
                }
                rig.draw(kit, program);
                gl.glBindSampler(0, 0);
                return true;
            }, Tolerance::filter(4, 0.001));
        }

        tex_case("wrap_border_color_integer", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            std::vector<std::uint8_t> data(8 * 8 * 4);
            for (unsigned i = 0; i < 64; ++i) {
                data[i * 4] = static_cast<std::uint8_t>(i * 3);
                data[i * 4 + 1] = static_cast<std::uint8_t>(255 - i * 3);
                data[i * 4 + 2] = static_cast<std::uint8_t>((i % 8) * 30);
                data[i * 4 + 3] = 200;
            }
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8UI, 8, 8);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 8, 8, glc::RGBA_INTEGER, glc::UNSIGNED_BYTE, data.data());
            GLuint sampler = 0;
            gl.glGenSamplers(1, &sampler);
            kit.defer([&kit, sampler] { kit.gl.glDeleteSamplers(1, &sampler); });
            gl.glSamplerParameteri(sampler, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glSamplerParameteri(sampler, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glSamplerParameteri(sampler, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_BORDER));
            gl.glSamplerParameteri(sampler, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_BORDER));
            const GLuint border[4] = {250, 40, 120, 255};
            gl.glSamplerParameterIuiv(sampler, glc::TEXTURE_BORDER_COLOR, border);
            gl.glBindSampler(0, sampler);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_2D, t);
            const GLuint program = bind_program(kit, rig, "void main() { out_color = vec4(texture(u_tex, gl_FragCoord.xy / 128.0 * 2.0 - 0.5)) / 255.0; }", "uniform usampler2D u_tex;", error);
            if (program == 0) {
                return false;
            }
            rig.draw(kit, program);
            gl.glBindSampler(0, 0);
            return true;
        });

        for (const bool seamless : {false, true}) {
            tex_case(seamless ? "cube_seamless_enabled" : "cube_seamless_disabled", [seamless](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const GLuint t = TexRig::texture(kit, glc::TEXTURE_CUBE_MAP, glc::LINEAR);
                gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::RGBA8, 4, 4);
                for (unsigned face = 0; face < 6; ++face) {
                    std::vector<std::uint8_t> data(4 * 4 * 4);
                    for (unsigned i = 0; i < 16; ++i) {
                        data[i * 4] = static_cast<std::uint8_t>(face * 45 + 20);
                        data[i * 4 + 1] = static_cast<std::uint8_t>(255 - face * 40);
                        data[i * 4 + 2] = static_cast<std::uint8_t>((i % 4) * 60);
                        data[i * 4 + 3] = 255;
                    }
                    gl.glTexSubImage2D(glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 4, 4, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
                }
                if (seamless) {
                    gl.glEnable(glc::TEXTURE_CUBE_MAP_SEAMLESS);
                } else {
                    gl.glDisable(glc::TEXTURE_CUBE_MAP_SEAMLESS);
                }
                gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
                gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
                const GLuint program = bind_program(kit, rig,
                                                    "void main() { vec2 c = gl_FragCoord.xy / 128.0; vec3 d = normalize(vec3(1.0, c.y * 2.0 - 1.0, c.x * 2.0 - 1.0 + 0.8)); out_color = texture(u_tex, d); }",
                                                    "uniform samplerCube u_tex;", error);
                if (program == 0) {
                    return false;
                }
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_CUBE_MAP, t);
                rig.draw(kit, program);
                gl.glDisable(glc::TEXTURE_CUBE_MAP_SEAMLESS);
                return true;
            }, Tolerance::filter(4, 0.001));
        }

        const auto depth_texture = [](Kit& kit, unsigned size) {
            Api& gl = kit.gl;
            std::vector<float> d(size * size);
            for (unsigned y = 0; y < size; ++y) {
                for (unsigned x = 0; x < size; ++x) {
                    d[y * size + x] = static_cast<float>(x + y) / static_cast<float>(2 * size - 2);
                }
            }
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::DEPTH_COMPONENT32F, static_cast<GLsizei>(size), static_cast<GLsizei>(size));
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(size), static_cast<GLsizei>(size), glc::DEPTH_COMPONENT, glc::FLOAT, d.data());
            return t;
        };

        tex_case("shadow_compare_funcs", [depth_texture](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = depth_texture(kit, 32);
            GLuint sampler = 0;
            gl.glGenSamplers(1, &sampler);
            kit.defer([&kit, sampler] { kit.gl.glDeleteSamplers(1, &sampler); });
            gl.glSamplerParameteri(sampler, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glSamplerParameteri(sampler, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glSamplerParameteri(sampler, glc::TEXTURE_COMPARE_MODE, static_cast<GLint>(glc::COMPARE_REF_TO_TEXTURE));
            gl.glBindSampler(0, sampler);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_2D, t);
            const GLuint program = bind_program(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; float r = texture(u_tex, vec3(c, 0.5)); out_color = vec4(vec3(r) * (0.6 + 0.4 * c.x), 1.0); }", "uniform sampler2DShadow u_tex;", error);
            if (program == 0) {
                return false;
            }
            const GLenum funcs[8] = {glc::NEVER, glc::LESS, glc::EQUAL, glc::LEQUAL, glc::GREATER, glc::NOTEQUAL, glc::GEQUAL, glc::ALWAYS};
            gl.glEnable(glc::SCISSOR_TEST);
            for (int i = 0; i < 8; ++i) {
                gl.glScissor((i % 4) * 32, (i / 4) * 64, 32, 64);
                gl.glSamplerParameteri(sampler, glc::TEXTURE_COMPARE_FUNC, static_cast<GLint>(funcs[i]));
                rig.draw(kit, program);
            }
            gl.glDisable(glc::SCISSOR_TEST);
            gl.glBindSampler(0, 0);
            return true;
        });

        tex_case("shadow_pcf_linear_filter", [depth_texture](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = depth_texture(kit, 32);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_COMPARE_MODE, static_cast<GLint>(glc::COMPARE_REF_TO_TEXTURE));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_COMPARE_FUNC, static_cast<GLint>(glc::LEQUAL));
            gl.glBindTexture(glc::TEXTURE_2D, t);
            const GLuint program = bind_program(kit, rig, "void main() { vec2 c = gl_FragCoord.xy / 128.0; float r = texture(u_tex, vec3(c * 1.1, 0.35 + 0.3 * c.y)); out_color = vec4(r, r * 0.6, 1.0 - r, 1.0); }", "uniform sampler2DShadow u_tex;", error);
            if (program == 0) {
                return false;
            }
            rig.draw(kit, program);
            return true;
        }, Tolerance::filter(4, 0.001));

        tex_case("shadow_array_and_cube", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            std::vector<float> data(16 * 16 * 3);
            for (unsigned l = 0; l < 3; ++l) {
                for (unsigned i = 0; i < 256; ++i) {
                    data[l * 256 + i] = static_cast<float>((i % 16) + l * 5) / 31.0f;
                }
            }
            const GLuint array = TexRig::texture(kit, glc::TEXTURE_2D_ARRAY, glc::NEAREST);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 1, glc::DEPTH_COMPONENT32F, 16, 16, 3);
            gl.glTexSubImage3D(glc::TEXTURE_2D_ARRAY, 0, 0, 0, 0, 16, 16, 3, glc::DEPTH_COMPONENT, glc::FLOAT, data.data());
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_COMPARE_MODE, static_cast<GLint>(glc::COMPARE_REF_TO_TEXTURE));
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_COMPARE_FUNC, static_cast<GLint>(glc::LESS));
            const GLuint cube = TexRig::texture(kit, glc::TEXTURE_CUBE_MAP, glc::NEAREST);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::DEPTH_COMPONENT32F, 16, 16);
            for (unsigned face = 0; face < 6; ++face) {
                gl.glTexSubImage2D(glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 16, 16, glc::DEPTH_COMPONENT, glc::FLOAT, data.data() + (face % 3) * 256);
            }
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_COMPARE_MODE, static_cast<GLint>(glc::COMPARE_REF_TO_TEXTURE));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_COMPARE_FUNC, static_cast<GLint>(glc::LEQUAL));
            const GLuint program = rig.program(kit,
                                               "void main() {\n"
                                               "    vec2 c = gl_FragCoord.xy / 128.0;\n"
                                               "    float v = c.y < 0.5 ? texture(u_array, vec4(c.x, c.y * 2.0, float(int(c.x * 3.0)), 0.3 + 0.25 * c.x)) : texture(u_cube, vec4(normalize(vec3(1.0, c.x * 2.0 - 1.0, (c.y - 0.5) * 4.0 - 1.0)), 0.5));\n"
                                               "    out_color = vec4(v, c.y < 0.5 ? 0.4 : 0.8, 1.0 - v, 1.0);\n"
                                               "}\n", error, "uniform sampler2DArrayShadow u_array;\nuniform samplerCubeShadow u_cube;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_array", 0);
            kit.u1i(program, "u_cube", 1);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, array);
            gl.glActiveTexture(glc::TEXTURE1);
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, cube);
            gl.glActiveTexture(glc::TEXTURE0);
            rig.draw(kit, program);
            return true;
        });

        const auto function_case = [](const char* name, const char* body, Tolerance tol = Tolerance::tight(1)) {
            tex_case(name, [body](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const GLuint t = checker_texture(kit, 16, 2, glc::LINEAR, 5);
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
                gl.glBindTexture(glc::TEXTURE_2D, t);
                const GLuint program = bind_program(kit, rig, body, "uniform sampler2D u_tex;", error);
                if (program == 0) {
                    return false;
                }
                rig.draw(kit, program);
                return true;
            }, std::move(tol));
        };
        function_case("fn_texture_gather_components",
                      "void main() { vec2 c = gl_FragCoord.xy / 128.0; int comp = int(c.x * 2.0) + 2 * int(c.y * 2.0); vec2 uv = fract(c * 2.0);\n"
                      "    vec4 g = comp == 0 ? textureGather(u_tex, uv, 0) : (comp == 1 ? textureGather(u_tex, uv, 1) : (comp == 2 ? textureGather(u_tex, uv, 2) : textureGather(u_tex, uv, 3)));\n"
                      "    out_color = vec4(g.x, g.y, g.z, 1.0) + vec4(g.w * 0.3); }");
        function_case("fn_texture_gather_offsets",
                      "void main() { vec2 c = gl_FragCoord.xy / 128.0; const ivec2 offs[4] = ivec2[4](ivec2(0, 0), ivec2(2, 0), ivec2(0, 2), ivec2(3, 3));\n"
                      "    vec4 g = textureGatherOffsets(u_tex, c, offs, 0); out_color = vec4(g.xyz, 1.0) * 0.8 + 0.2 * vec4(g.w); }");
        function_case("fn_texture_grad_and_lod",
                      "void main() { vec2 c = gl_FragCoord.xy / 128.0; vec4 a = textureGrad(u_tex, c, vec2(0.1 * c.x, 0.0), vec2(0.0, 0.2 * c.y));\n"
                      "    vec4 b = textureLod(u_tex, c, 3.0 * c.x); out_color = c.y < 0.5 ? a : b; }", Tolerance::filter(4, 0.001));
        function_case("fn_texture_query_lod_and_size",
                      "void main() { vec2 c = gl_FragCoord.xy / 128.0; vec2 q = textureQueryLod(u_tex, c * (1.0 + 7.0 * c.x)); ivec2 s = textureSize(u_tex, 1); int lv = textureQueryLevels(u_tex);\n"
                      "    out_color = vec4(q.x / 4.0, fract(q.y), float(s.x) / 16.0 * c.y, float(lv) / 5.0 * c.x + 0.2); }", Tolerance::filter(4, 0.001));
        function_case("fn_texel_fetch_and_offsets",
                      "void main() { vec2 c = gl_FragCoord.xy / 128.0; vec4 a = texelFetchOffset(u_tex, ivec2(c * 16.0), 0, ivec2(1, 2)); vec4 b = textureOffset(u_tex, c, ivec2(3, -2)); vec4 d = textureLodOffset(u_tex, c, 1.0, ivec2(-1, 1));\n"
                      "    out_color = c.x < 0.34 ? a : (c.x < 0.67 ? b : d); }", Tolerance::filter(4, 0.001));
        function_case("fn_texture_proj",
                      "void main() { vec2 c = gl_FragCoord.xy / 128.0; float w = 0.5 + 1.5 * c.y; out_color = textureProj(u_tex, vec3(c * w, w)) * 0.5 + 0.5 * textureProj(u_tex, vec4(c * w, 0.0, w)); }", Tolerance::filter(4, 0.001));

        tex_case("bindless_texture_handle", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint t = checker_texture(kit, 16, 2, glc::NEAREST, 1);
            const GLuint64 handle = gl.glGetTextureHandleARB(t);
            gl.glMakeTextureHandleResidentARB(handle);
            const GLuint program = rig.program(kit, "void main() { out_color = texture(u_tex, gl_FragCoord.xy / 128.0 * 2.0); }", error,
                                               "#extension GL_ARB_bindless_texture : require\nlayout(bindless_sampler) uniform sampler2D u_tex;");
            if (program == 0) {
                return false;
            }
            gl.glUseProgram(program);
            gl.glUniformHandleui64ARB(gl.glGetUniformLocation(program, "u_tex"), handle);
            rig.draw(kit, program);
            gl.glMakeTextureHandleNonResidentARB(handle);
            return true;
        }, Tolerance::tight(1), [](Kit& kit) -> std::string { return kit.has_extension("GL_ARB_bindless_texture") ? std::string() : "GL_ARB_bindless_texture is not exposed"; });

        tex_case("sparse_texture_commit_decommit", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            GLint page_x = 0, page_y = 0;
            gl.glGetInternalformativ(glc::TEXTURE_2D, glc::RGBA8, glc::VIRTUAL_PAGE_SIZE_X_ARB, 1, &page_x);
            gl.glGetInternalformativ(glc::TEXTURE_2D, glc::RGBA8, glc::VIRTUAL_PAGE_SIZE_Y_ARB, 1, &page_y);
            if (page_x <= 0 || page_y <= 0) {
                error = "no sparse page size is reported for RGBA8";
                return false;
            }
            TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_SPARSE_ARB, 1);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, page_x * 2, page_y * 2);
            std::vector<std::uint8_t> page(static_cast<std::size_t>(page_x) * static_cast<std::size_t>(page_y) * 4);
            for (std::size_t i = 0; i < page.size(); i += 4) {
                page[i] = static_cast<std::uint8_t>((i / 4) % static_cast<std::size_t>(page_x) * 255 / static_cast<std::size_t>(page_x));
                page[i + 1] = 200;
                page[i + 2] = static_cast<std::uint8_t>((i / 4) / static_cast<std::size_t>(page_x) * 255 / static_cast<std::size_t>(page_y));
                page[i + 3] = 255;
            }
            gl.glTexPageCommitmentARB(glc::TEXTURE_2D, 0, 0, 0, 0, page_x, page_y, 1, glc::TRUE_);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, page_x, page_y, glc::RGBA, glc::UNSIGNED_BYTE, page.data());
            gl.glTexPageCommitmentARB(glc::TEXTURE_2D, 0, page_x, page_y, 0, page_x, page_y, 1, glc::TRUE_);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, page_x, page_y, page_x, page_y, glc::RGBA, glc::UNSIGNED_BYTE, page.data());
            gl.glTexPageCommitmentARB(glc::TEXTURE_2D, 0, 0, 0, 0, page_x, page_y, 1, glc::FALSE_);
            const GLuint program = bind_program(kit, rig,
                                                "void main() { vec2 c = gl_FragCoord.xy / 128.0; vec2 p = vec2(1.0, 1.0); out_color = c.x > 0.5 && c.y > 0.5 ? texture(u_tex, c) : vec4(0.1, 0.1, 0.2, 1.0); }",
                                                "uniform sampler2D u_tex;", error);
            if (program == 0) {
                return false;
            }
            rig.draw(kit, program);
            return true;
        }, Tolerance::tight(1), [](Kit& kit) -> std::string { return kit.has_extension("GL_ARB_sparse_texture") ? std::string() : "GL_ARB_sparse_texture is not exposed"; });

        tex_case("copy_image_sub_data_2d_to_array_layers", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint source = checker_texture(kit, 32, 4, glc::NEAREST, 1);
            const GLuint array = TexRig::texture(kit, glc::TEXTURE_2D_ARRAY, glc::NEAREST);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 1, glc::RGBA8, 32, 32, 4);
            gl.glCopyImageSubData(source, glc::TEXTURE_2D, 0, 0, 0, 0, array, glc::TEXTURE_2D_ARRAY, 0, 0, 0, 1, 32, 32, 1);
            gl.glCopyImageSubData(source, glc::TEXTURE_2D, 0, 8, 8, 0, array, glc::TEXTURE_2D_ARRAY, 0, 4, 12, 2, 16, 16, 1);
            gl.glCopyImageSubData(array, glc::TEXTURE_2D_ARRAY, 0, 0, 0, 1, array, glc::TEXTURE_2D_ARRAY, 0, 0, 0, 3, 32, 16, 1);
            const GLuint program = rig.program(kit, "void main() { vec2 c = gl_FragCoord.xy / 128.0; int l = int(c.x * 2.0) + 2 * int(c.y * 2.0); out_color = texelFetch(u_tex, ivec3(fract(c * 2.0) * 32.0, l), 0); }", error, "uniform sampler2DArray u_tex;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, array);
            rig.draw(kit, program);
            return true;
        });

        tex_case("copy_image_sub_data_cube_face_to_2d", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint cube = TexRig::texture(kit, glc::TEXTURE_CUBE_MAP, glc::NEAREST);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::RGBA8, 16, 16);
            for (unsigned face = 0; face < 6; ++face) {
                const auto data = TexRig::cells(16, 16, 1 + face, 2 + face % 3);
                gl.glTexSubImage2D(glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            }
            const GLuint plain = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 16, 32);
            gl.glCopyImageSubData(cube, glc::TEXTURE_CUBE_MAP, 0, 0, 0, 3, plain, glc::TEXTURE_2D, 0, 0, 0, 0, 16, 16, 1);
            gl.glCopyImageSubData(cube, glc::TEXTURE_CUBE_MAP, 0, 0, 0, 5, plain, glc::TEXTURE_2D, 0, 0, 16, 0, 16, 16, 1);
            const GLuint program = rig.program(kit, "void main() { out_color = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 128.0 * vec2(16.0, 32.0)), 0); }", error, "uniform sampler2D u_tex;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            gl.glBindTexture(glc::TEXTURE_2D, plain);
            rig.draw(kit, program);
            return true;
        });

        tex_case("clear_tex_image_and_sub_image", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint volume = TexRig::texture(kit, glc::TEXTURE_3D, glc::NEAREST);
            gl.glTexStorage3D(glc::TEXTURE_3D, 1, glc::RGBA8, 16, 16, 4);
            const GLubyte blue[4] = {20, 60, 220, 255};
            gl.glClearTexImage(volume, 0, glc::RGBA, glc::UNSIGNED_BYTE, blue);
            const GLubyte orange[4] = {240, 140, 30, 255};
            gl.glClearTexSubImage(volume, 0, 2, 3, 1, 8, 6, 2, glc::RGBA, glc::UNSIGNED_BYTE, orange);
            const GLfloat green[4] = {0.2f, 0.9f, 0.3f, 1.0f};
            gl.glClearTexSubImage(volume, 0, 8, 8, 3, 8, 8, 1, glc::RGBA, glc::FLOAT, green);
            const GLuint program = rig.program(kit, "void main() { vec2 c = gl_FragCoord.xy / 128.0; int l = int(c.x * 4.0); out_color = texelFetch(u_tex, ivec3(fract(vec2(c.x * 4.0, c.y)) * 16.0, l), 0); }", error, "uniform sampler3D u_tex;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            gl.glBindTexture(glc::TEXTURE_3D, volume);
            rig.draw(kit, program);
            return true;
        });
    }
} kBatch19;

}  // namespace

}  // namespace lsw::render::gl
