#include "render/gl/flat2d.hpp"
#include "render/gl/scene3.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec frag(const std::string& name) { return CaseSpec("frag", name).min_version(4, 6); }

using Body = std::function<bool(Kit&, Scene3&, std::string&)>;

enum class Extra { None, Depth, Stencil, Both };

void scene_case(const std::string& name, Body body, Extra extra = Extra::None, Tolerance tolerance = Tolerance::tight(1), int samples = 1, GLenum depth = glc::DEPTH24_STENCIL8,
                NeedFn need = {}) {
    Cases::single(frag(name).tolerance(std::move(tolerance)), [body, extra, samples, depth](Kit& kit, CaptureSet& out, std::string& error) {
        Scene3 scene;
        if (!scene.open(kit, error, samples, depth)) {
            return false;
        }
        if (!body(kit, scene, error)) {
            return false;
        }
        if (!Gx::shot(kit, scene.target, out, error)) {
            return false;
        }
        if ((extra == Extra::Depth || extra == Extra::Both) && !Gx::shot_depth(kit, scene.target, out, error)) {
            return false;
        }
        if ((extra == Extra::Stencil || extra == Extra::Both) && !Gx::shot_stencil(kit, scene.target, out, error)) {
            return false;
        }
        return true;
    }, std::move(need));
}

void depth_pass(Kit& kit, Scene3& s, GLenum func) {
    Api& gl = kit.gl;
    s.view = Gx::camera(1.0f, 3.2f, 0.4f, 0.25f);
    gl.glDepthFunc(glc::LESS);
    for (int i = 0; i < 3; ++i) {
        s.draw(kit, s.quad, Mat4::translate(-0.9f + 0.9f * static_cast<float>(i), 0.0f, -0.6f + 0.6f * static_cast<float>(i)) * Mat4::scale(0.3f, 0.9f, 1.0f), 0.85f, 0.45f, 0.3f);
    }
    gl.glDepthFunc(func);
    s.draw(kit, s.quad, Mat4::rotate_y(0.7f) * Mat4::scale(1.3f, 0.5f, 1.0f), 0.3f, 0.6f, 0.9f);
    s.draw(kit, s.quad, Mat4::translate(0.0f, 0.0f, 0.0f) * Mat4::scale(0.3f, 0.9f, 1.0f) * Mat4::translate(0.0f, 0.0f, 0.0f) * Mat4::scale(1.0f, 0.5f, 1.0f), 0.3f, 0.9f, 0.4f);
}

const char* kDepthWriteFragment =
    "#version 460 core\n"
    "in vec3 v_nrm;\nin vec2 v_uv;\nin vec3 v_world;\n"
    "layout(%s) out float gl_FragDepth;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    out_color = vec4(0.5 + 0.5 * v_nrm, 1.0) * (0.6 + 0.4 * fract(v_uv.x * 6.0));\n"
    "    gl_FragDepth = %s;\n"
    "}\n";

const struct Batch15 {
    Batch15() {
        struct Func {
            const char* name;
            GLenum value;
        };
        for (const Func& f : {Func{"never", glc::NEVER}, Func{"less", glc::LESS}, Func{"equal", glc::EQUAL}, Func{"lequal", glc::LEQUAL}, Func{"greater", glc::GREATER},
                              Func{"notequal", glc::NOTEQUAL}, Func{"gequal", glc::GEQUAL}, Func{"always", glc::ALWAYS}}) {
            const GLenum value = f.value;
            scene_case(std::string("depth_func_") + f.name, [value](Kit& kit, Scene3& s, std::string&) {
                depth_pass(kit, s, value);
                return true;
            }, Extra::Depth);
        }

        struct Format {
            const char* name;
            GLenum value;
        };
        for (const Format& f : {Format{"d16", glc::DEPTH_COMPONENT16}, Format{"d24", glc::DEPTH_COMPONENT24}}) {
            scene_case(std::string("depth_format_") + f.name, [](Kit& kit, Scene3& s, std::string&) {
                kit.gl.glDepthFunc(glc::LESS);
                s.view = Mat4::perspective(1.0f, 1.0f, 0.1f, 100.0f, false);
                s.draw(kit, s.quad, Mat4::translate(0.0f, 0.0f, -50.0f) * Mat4::scale(40.0f, 40.0f, 1.0f), 0.3f, 0.5f, 0.9f);
                for (int i = 0; i < 8; ++i) {
                    const float dz = 0.0005f * (1.0f + static_cast<float>(i * i));
                    s.draw(kit, s.quad, Mat4::translate((static_cast<float>(i) - 3.5f) * 6.8f, 0.0f, -50.0f + dz) * Mat4::scale(3.3f, 20.0f, 1.0f), 0.9f - 0.08f * static_cast<float>(i), 0.4f, 0.3f);
                }
                return true;
            }, f.value == glc::DEPTH_COMPONENT16 ? Extra::None : Extra::Depth, Tolerance::tight(1), 1, f.value);
        }

        scene_case("depth_range_window", [](Kit& kit, Scene3& s, std::string&) {
            kit.gl.glDepthRange(0.25, 0.75);
            depth_pass(kit, s, glc::LESS);
            kit.gl.glDepthRange(0.0, 1.0);
            return true;
        }, Extra::Depth);

        scene_case("depth_range_indexed_and_f", [](Kit& kit, Scene3& s, std::string&) {
            kit.gl.glDepthRangeIndexed(0, 0.1, 0.6);
            depth_pass(kit, s, glc::LESS);
            kit.gl.glDepthRangef(0.0f, 1.0f);
            return true;
        }, Extra::Depth);

        scene_case("depth_mask_off_transparent_layers", [](Kit& kit, Scene3& s, std::string&) {
            Api& gl = kit.gl;
            s.draw(kit, s.torus, Mat4::rotate_x(0.5f));
            gl.glEnable(glc::BLEND);
            gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
            gl.glDepthMask(0);
            for (int i = 0; i < 3; ++i) {
                s.draw(kit, s.quad, Mat4::translate(0.0f, 0.0f, 0.4f - 0.4f * static_cast<float>(i)) * Mat4::rotate_y(0.3f * static_cast<float>(i)) * Mat4::scale(0.9f, 0.9f, 1.0f),
                       0.9f - 0.3f * static_cast<float>(i), 0.3f + 0.3f * static_cast<float>(i), 0.9f, 0.45f);
            }
            gl.glDepthMask(1);
            return true;
        }, Extra::Depth, Tolerance::blend(2));

        scene_case("stencil_ops_cells", [](Kit& kit, Scene3& s, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            gl.glEnable(glc::STENCIL_TEST);
            const GLenum ops[8] = {glc::KEEP, glc::ZERO, glc::REPLACE, glc::INCR, glc::DECR, glc::INVERT, glc::INCR_WRAP, glc::DECR_WRAP};
            gl.glDisable(glc::DEPTH_TEST);
            for (int i = 0; i < 8; ++i) {
                const float x = -0.75f + 0.5f * static_cast<float>(i % 4);
                const float y = -0.5f + 1.0f * static_cast<float>(i / 4);
                const std::vector<V2> quad = {{x - 0.2f, y - 0.4f, 0.8f, 0.4f, 0.2f}, {x + 0.2f, y - 0.4f, 0.8f, 0.4f, 0.2f}, {x + 0.2f, y + 0.4f, 0.8f, 0.4f, 0.2f},
                                              {x - 0.2f, y - 0.4f, 0.8f, 0.4f, 0.2f}, {x + 0.2f, y + 0.4f, 0.8f, 0.4f, 0.2f}, {x - 0.2f, y + 0.4f, 0.8f, 0.4f, 0.2f}};
                const GLuint vao = Flat2d::upload(kit, quad);
                gl.glBindVertexArray(vao);
                gl.glStencilMask(0xFF);
                gl.glStencilFunc(glc::ALWAYS, 5, 0xFF);
                gl.glStencilOp(glc::REPLACE, glc::REPLACE, glc::REPLACE);
                gl.glDrawArrays(glc::TRIANGLES, 0, 6);
                gl.glStencilFunc(glc::ALWAYS, 9, 0xFF);
                gl.glStencilOp(glc::KEEP, glc::KEEP, ops[i]);
                gl.glDrawArrays(glc::TRIANGLES, 0, 3);
                gl.glStencilOp(glc::KEEP, glc::KEEP, ops[i]);
                gl.glDrawArrays(glc::TRIANGLES, 3, 3);
            }
            static_cast<void>(s);
            return true;
        }, Extra::Stencil);

        scene_case("stencil_funcs_cells", [](Kit& kit, Scene3&, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            gl.glDisable(glc::DEPTH_TEST);
            gl.glEnable(glc::STENCIL_TEST);
            gl.glStencilMask(0xFF);
            gl.glStencilFunc(glc::ALWAYS, 0, 0xFF);
            gl.glStencilOp(glc::KEEP, glc::KEEP, glc::KEEP);
            gl.glColorMask(0, 0, 0, 0);
            for (int i = 0; i < 8; ++i) {
                gl.glStencilOp(glc::KEEP, glc::KEEP, glc::REPLACE);
                gl.glStencilFunc(glc::ALWAYS, i % 4 + 1, 0xFF);
                const float x0 = -1.0f + 0.25f * static_cast<float>(i);
                const std::vector<V2> quad = {{x0, -1, 1, 1, 1}, {x0 + 0.25f, -1, 1, 1, 1}, {x0 + 0.25f, 1, 1, 1, 1}, {x0, -1, 1, 1, 1}, {x0 + 0.25f, 1, 1, 1, 1}, {x0, 1, 1, 1, 1}};
                gl.glBindVertexArray(Flat2d::upload(kit, quad));
                gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            }
            gl.glColorMask(1, 1, 1, 1);
            gl.glStencilOp(glc::KEEP, glc::KEEP, glc::KEEP);
            const GLenum funcs[8] = {glc::NEVER, glc::LESS, glc::LEQUAL, glc::GREATER, glc::GEQUAL, glc::EQUAL, glc::NOTEQUAL, glc::ALWAYS};
            for (int i = 0; i < 8; ++i) {
                gl.glStencilFunc(funcs[i], 2, 0xFF);
                const float y0 = -1.0f + 0.25f * static_cast<float>(i);
                const std::vector<V2> quad = {{-1, y0, 0.3f + 0.08f * static_cast<float>(i), 0.8f, 0.4f}, {1, y0, 0.3f + 0.08f * static_cast<float>(i), 0.8f, 0.4f},
                                              {1, y0 + 0.2f, 0.3f + 0.08f * static_cast<float>(i), 0.8f, 0.4f}, {-1, y0, 0.3f + 0.08f * static_cast<float>(i), 0.8f, 0.4f},
                                              {1, y0 + 0.2f, 0.3f + 0.08f * static_cast<float>(i), 0.8f, 0.4f}, {-1, y0 + 0.2f, 0.3f + 0.08f * static_cast<float>(i), 0.8f, 0.4f}};
                gl.glBindVertexArray(Flat2d::upload(kit, quad));
                gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            }
            return true;
        }, Extra::Stencil);

        scene_case("stencil_front_back_separate", [](Kit& kit, Scene3& s, std::string& error) {
            Api& gl = kit.gl;
            gl.glDisable(glc::CULL_FACE);
            gl.glEnable(glc::STENCIL_TEST);
            gl.glStencilMask(0xFF);
            gl.glDisable(glc::DEPTH_TEST);
            gl.glDepthMask(0);
            gl.glColorMask(0, 0, 0, 0);
            gl.glStencilFuncSeparate(glc::FRONT, glc::ALWAYS, 0, 0xFF);
            gl.glStencilFuncSeparate(glc::BACK, glc::ALWAYS, 0, 0xFF);
            gl.glStencilOpSeparate(glc::FRONT, glc::KEEP, glc::KEEP, glc::INCR_WRAP);
            gl.glStencilOpSeparate(glc::BACK, glc::KEEP, glc::KEEP, glc::INVERT);
            s.draw(kit, s.torus, Mat4::rotate_x(0.7f));
            s.draw(kit, s.sphere, Mat4::translate(0.8f, 0.3f, 0.0f) * Mat4::scale(0.5f, 0.5f, 0.5f));
            gl.glColorMask(1, 1, 1, 1);
            gl.glDepthMask(1);
            gl.glDisable(glc::DEPTH_TEST);
            gl.glStencilFuncSeparate(glc::FRONT_AND_BACK, glc::NOTEQUAL, 0, 0xFF);
            gl.glStencilOpSeparate(glc::FRONT_AND_BACK, glc::KEEP, glc::KEEP, glc::KEEP);
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            const std::vector<V2> quad = {{-1, -1, 0.9f, 0.7f, 0.2f}, {1, -1, 0.9f, 0.7f, 0.2f}, {1, 1, 0.2f, 0.7f, 0.9f}, {-1, -1, 0.9f, 0.7f, 0.2f}, {1, 1, 0.2f, 0.7f, 0.9f}, {-1, 1, 0.2f, 0.7f, 0.9f}};
            gl.glBindVertexArray(Flat2d::upload(kit, quad));
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return true;
        }, Extra::Stencil);

        scene_case("stencil_masks_partial_write", [](Kit& kit, Scene3&, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            gl.glDisable(glc::DEPTH_TEST);
            gl.glEnable(glc::STENCIL_TEST);
            gl.glStencilFunc(glc::ALWAYS, 0xA5, 0xFF);
            gl.glStencilOp(glc::REPLACE, glc::REPLACE, glc::REPLACE);
            gl.glStencilMask(0x0F);
            const std::vector<V2> a = {{-0.9f, -0.9f, 0.9f, 0.5f, 0.2f}, {0.3f, -0.9f, 0.9f, 0.5f, 0.2f}, {0.3f, 0.3f, 0.9f, 0.5f, 0.2f}};
            gl.glBindVertexArray(Flat2d::upload(kit, a));
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            gl.glStencilMask(0xF0);
            gl.glStencilFunc(glc::ALWAYS, 0x3C, 0xFF);
            const std::vector<V2> b = {{-0.3f, -0.3f, 0.2f, 0.5f, 0.9f}, {0.9f, -0.3f, 0.2f, 0.5f, 0.9f}, {0.9f, 0.9f, 0.2f, 0.5f, 0.9f}};
            gl.glBindVertexArray(Flat2d::upload(kit, b));
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            gl.glStencilMask(0xFF);
            gl.glStencilFunc(glc::EQUAL, 0x05, 0x0F);
            const std::vector<V2> c = {{-1, -1, 0.3f, 0.9f, 0.4f}, {1, -1, 0.3f, 0.9f, 0.4f}, {1, 1, 0.3f, 0.9f, 0.4f}, {-1, -1, 0.3f, 0.9f, 0.4f}, {1, 1, 0.3f, 0.9f, 0.4f}, {-1, 1, 0.3f, 0.9f, 0.4f}};
            gl.glStencilOp(glc::KEEP, glc::KEEP, glc::KEEP);
            gl.glBindVertexArray(Flat2d::upload(kit, c));
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return true;
        }, Extra::Stencil);

        for (const bool stencil_mode : {true, false}) {
            Cases::single(frag(stencil_mode ? "stencil_texturing_stencil_index" : "stencil_texturing_depth_component"), [stencil_mode](Kit& kit, CaptureSet& out, std::string& error) {
                Api& gl = kit.gl;
                GLuint texture = 0;
                GLuint fbo = 0;
                gl.glGenTextures(1, &texture);
                gl.glGenFramebuffers(1, &fbo);
                kit.defer([&kit, texture, fbo] {
                    kit.gl.glDeleteTextures(1, &texture);
                    kit.gl.glDeleteFramebuffers(1, &fbo);
                });
                gl.glBindTexture(glc::TEXTURE_2D, texture);
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::DEPTH24_STENCIL8, 64, 64);
                gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
                gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::DEPTH_STENCIL_ATTACHMENT, glc::TEXTURE_2D, texture, 0);
                gl.glDrawBuffer(glc::NONE);
                gl.glReadBuffer(glc::NONE);
                if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                    error = "depth-stencil texture framebuffer incomplete";
                    return false;
                }
                gl.glViewport(0, 0, 64, 64);
                gl.glClearDepth(0.5);
                gl.glClearStencil(0);
                gl.glClear(glc::DEPTH_BUFFER_BIT | glc::STENCIL_BUFFER_BIT);
                static const char* sloped_vs =
                    "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec3 in_col;\nout vec3 v_col;\n"
                    "void main() { v_col = in_col; gl_Position = vec4(in_pos, in_pos.x * 0.8, 1.0); }\n";
                const GLuint flat = kit.program(sloped_vs, Flat2d::kFragment, error);
                if (flat == 0) {
                    return false;
                }
                gl.glUseProgram(flat);
                gl.glEnable(glc::STENCIL_TEST);
                gl.glStencilFunc(glc::ALWAYS, 0, 0xFF);
                gl.glStencilOp(glc::KEEP, glc::KEEP, glc::INCR);
                gl.glEnable(glc::DEPTH_TEST);
                gl.glDepthFunc(glc::ALWAYS);
                const std::vector<V2> ring = Flat2d::ring(12, 0.3f, 0.9f);
                gl.glBindVertexArray(Flat2d::upload(kit, ring));
                gl.glDrawArrays(glc::TRIANGLES, 0, 12);
                gl.glDisable(glc::STENCIL_TEST);
                Target target;
                if (!target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                target.bind(kit);
                Gx::clear(kit, 0, 0, 0);
                gl.glDisable(glc::DEPTH_TEST);
                gl.glBindTexture(glc::TEXTURE_2D, texture);
                gl.glTexParameteri(glc::TEXTURE_2D, glc::DEPTH_STENCIL_TEXTURE_MODE, static_cast<GLint>(stencil_mode ? glc::STENCIL_INDEX : glc::DEPTH_COMPONENT));
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
                const char* fs = stencil_mode ? "#version 460 core\nuniform usampler2D u_tex;\nlayout(location = 0) out vec4 out_color;\n"
                                                "void main() { uint v = texelFetch(u_tex, ivec2(gl_FragCoord.xy * 0.5), 0).r; out_color = vec4(float(v) * 0.4, float(v == 1u), float(v >= 2u), 1.0); }\n"
                                              : "#version 460 core\nuniform sampler2D u_tex;\nlayout(location = 0) out vec4 out_color;\n"
                                                "void main() { float v = texelFetch(u_tex, ivec2(gl_FragCoord.xy * 0.5), 0).r; out_color = vec4(v, v * 0.5, 1.0 - v, 1.0); }\n";
                const GLuint program = kit.program(Sources::kScreenVertex, fs, error);
                if (program == 0) {
                    return false;
                }
                kit.u1i(program, "u_tex", 0);
                kit.vertex_array();
                gl.glDrawArrays(glc::TRIANGLES, 0, 3);
                return Gx::shot(kit, target, out, error);
            });
        }

        struct DepthWrite {
            const char* name;
            const char* layout;
            const char* expr;
        };
        for (const DepthWrite& w : {DepthWrite{"frag_depth_any", "depth_any", "clamp(gl_FragCoord.z + 0.15 * sin(v_uv.x * 20.0), 0.0, 1.0)"},
                                    DepthWrite{"frag_depth_greater", "depth_greater", "clamp(gl_FragCoord.z + 0.1 * fract(v_uv.x * 8.0), 0.0, 1.0)"},
                                    DepthWrite{"frag_depth_less", "depth_less", "gl_FragCoord.z - 0.05 * fract(v_uv.y * 8.0)"},
                                    DepthWrite{"frag_depth_unchanged", "depth_unchanged", "gl_FragCoord.z"}}) {
            scene_case(w.name, [w](Kit& kit, Scene3& s, std::string& error) {
                char fs[1024];
                std::snprintf(fs, sizeof fs, kDepthWriteFragment, w.layout, w.expr);
                const GLuint program = kit.program(Sources::kLitVertex, fs, error);
                if (program == 0) {
                    return false;
                }
                const GLuint previous = s.program;
                s.program = program;
                s.draw(kit, s.sphere, Mat4::translate(-0.4f, 0.0f, 0.0f) * Mat4::scale(0.9f, 0.9f, 0.9f));
                s.draw(kit, s.torus, Mat4::translate(0.2f, 0.0f, 0.1f) * Mat4::rotate_x(1.0f) * Mat4::scale(0.7f, 0.7f, 0.7f));
                s.program = previous;
                return true;
            }, Extra::Depth);
        }

        scene_case("early_fragment_tests_overdraw_counter", [](Kit& kit, Scene3& s, std::string& error) {
            Api& gl = kit.gl;
            static const char* fs =
                "#version 460 core\nlayout(early_fragment_tests) in;\nlayout(r32ui, binding = 0) coherent uniform uimage2D u_heat;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { imageAtomicAdd(u_heat, ivec2(gl_FragCoord.xy), 1u); out_color = vec4(0.9, 0.4, 0.2, 1.0); }\n";
            const GLuint program = kit.program(Sources::kLitVertex, fs, error);
            if (program == 0) {
                return false;
            }
            GLuint texture = 0;
            gl.glGenTextures(1, &texture);
            kit.defer([&kit, texture] { kit.gl.glDeleteTextures(1, &texture); });
            gl.glBindTexture(glc::TEXTURE_2D, texture);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::R32UI, 128, 128);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            const GLuint zero = 0;
            gl.glClearTexImage(texture, 0, glc::RED_INTEGER, glc::UNSIGNED_INT, &zero);
            gl.glBindImageTexture(0, texture, 0, glc::FALSE_, 0, glc::READ_WRITE, glc::R32UI);
            const GLuint previous = s.program;
            s.program = program;
            s.draw(kit, s.sphere, Mat4::translate(0.0f, 0.0f, 0.4f) * Mat4::scale(0.9f, 0.9f, 0.9f));
            s.draw(kit, s.sphere, Mat4::translate(0.2f, 0.1f, -0.5f) * Mat4::scale(1.1f, 1.1f, 1.1f));
            s.draw(kit, s.torus, Mat4::translate(0.0f, 0.0f, -0.9f));
            s.program = previous;
            gl.glMemoryBarrier(glc::SHADER_IMAGE_ACCESS_BARRIER_BIT | glc::TEXTURE_FETCH_BARRIER_BIT);
            gl.glDisable(glc::DEPTH_TEST);
            static const char* show =
                "#version 460 core\nuniform usampler2D u_heat;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { uint n = texelFetch(u_heat, ivec2(gl_FragCoord.xy), 0).r; out_color = vec4(float(n == 1u), float(n == 2u), float(n >= 3u), 1.0); }\n";
            const GLuint display = kit.program(Sources::kScreenVertex, show, error);
            if (display == 0) {
                return false;
            }
            kit.u1i(display, "u_heat", 0);
            kit.vertex_array();
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            return true;
        });

        scene_case("discard_alpha_masked_texture", [](Kit& kit, Scene3& s, std::string& error) {
            static const char* fs =
                "#version 460 core\nin vec3 v_nrm;\nin vec2 v_uv;\nuniform sampler2D u_tex;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { vec4 t = texture(u_tex, v_uv * 3.0); if (t.r > 0.9 && t.g > 0.85) discard; out_color = vec4(t.rgb * (0.4 + 0.6 * max(v_nrm.y, 0.2)), 1.0); }\n";
            const GLuint program = kit.program(Sources::kLitVertex, fs, error);
            if (program == 0) {
                return false;
            }
            const GLuint previous = s.program;
            s.program = program;
            kit.u1i(program, "u_tex", 0);
            kit.gl.glDisable(glc::CULL_FACE);
            s.draw(kit, s.sphere, Mat4::rotate_x(0.4f) * Mat4::scale(1.2f, 1.2f, 1.2f));
            s.program = previous;
            return true;
        });

        scene_case("derivatives_coarse_fine_fwidth", [](Kit& kit, Scene3& s, std::string& error) {
            static const char* fs =
                "#version 460 core\nin vec3 v_nrm;\nin vec2 v_uv;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 p = v_uv * 24.0;\n"
                "    vec2 c = abs(vec2(dFdxCoarse(p.x), dFdyCoarse(p.y)));\n"
                "    vec2 f = abs(vec2(dFdxFine(p.x), dFdyFine(p.y)));\n"
                "    float w = fwidth(p.x) + fwidth(p.y);\n"
                "    out_color = vec4(c * 0.8, f.x * 0.8 + w * 0.1 + f.y * 0.0, 1.0);\n"
                "}\n";
            const GLuint program = kit.program(Sources::kLitVertex, fs, error);
            if (program == 0) {
                return false;
            }
            const GLuint previous = s.program;
            s.program = program;
            s.draw(kit, s.sphere, Mat4::rotate_x(0.3f) * Mat4::scale(1.2f, 1.2f, 1.2f));
            s.program = previous;
            return true;
        });

        scene_case("interpolation_flat_noperspective_smooth", [](Kit& kit, Scene3& s, std::string& error) {
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec3 in_pos;\nlayout(location = 1) in vec3 in_nrm;\nlayout(location = 2) in vec2 in_uv;\nuniform mat4 u_mvp;\n"
                "flat out vec3 f_col;\nnoperspective out vec3 n_col;\nout vec3 s_col;\n"
                "void main() { vec3 c = abs(in_nrm) * 0.5 + 0.4 * vec3(in_uv, 0.5); f_col = c; n_col = c; s_col = c; gl_Position = u_mvp * vec4(in_pos, 1.0); }\n";
            static const char* fs =
                "#version 460 core\nflat in vec3 f_col;\nnoperspective in vec3 n_col;\nin vec3 s_col;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = vec4(c.x < 0.34 ? f_col : (c.x < 0.67 ? n_col : s_col), 1.0); }\n";
            const GLuint program = kit.program(vs, fs, error);
            if (program == 0) {
                return false;
            }
            s.view = Gx::camera(1.0f, 2.4f, 0.5f, 0.4f);
            kit.gl.glUseProgram(program);
            kit.umat4(program, "u_mvp", (s.view * Mat4::scale(1.5f, 1.5f, 1.5f)).m);
            s.cube.draw(kit);
            return true;
        });

        for (const bool sample_mode : {false, true}) {
            scene_case(sample_mode ? "interpolation_sample_qualifier_msaa" : "interpolation_centroid_qualifier_msaa", [sample_mode](Kit& kit, Scene3& s, std::string& error) {
                const std::string fs = std::string("#version 460 core\n") + (sample_mode ? "sample" : "centroid") +
                                       " in vec2 v_uv;\nin vec3 v_nrm;\nlayout(location = 0) out vec4 out_color;\n"
                                       "void main() { out_color = vec4(fract(v_uv.x * 6.0), fract(v_uv.y * 6.0), 0.5 + 0.5 * v_nrm.y, 1.0); }\n";
                std::string vs = Sources::kLitVertex;
                if (sample_mode) {
                    vs.replace(vs.find("out vec2 v_uv;"), 14, "sample out vec2 v_uv;");
                }
                const GLuint program = kit.program(vs.c_str(), fs.c_str(), error);
                if (program == 0) {
                    return false;
                }
                const GLuint previous = s.program;
                s.program = program;
                s.draw(kit, s.sphere, Mat4::rotate_x(0.3f) * Mat4::scale(1.2f, 1.2f, 1.2f));
                s.program = previous;
                return true;
            }, Extra::None, Tolerance::raster(2, 1).with_outliers(0.02), 4);
        }

        scene_case("interpolate_at_centroid_offset_sample", [](Kit& kit, Scene3& s, std::string& error) {
            static const char* fs =
                "#version 460 core\nin vec2 v_uv;\nin vec3 v_nrm;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 a = interpolateAtCentroid(v_uv);\n"
                "    vec2 b = interpolateAtOffset(v_uv, vec2(0.4, -0.3));\n"
                "    vec2 c = interpolateAtSample(v_uv, 1);\n"
                "    out_color = vec4(fract(a.x * 6.0), fract(b.y * 6.0), fract(c.x * 6.0 + c.y * 3.0), 1.0);\n"
                "}\n";
            const GLuint program = kit.program(Sources::kLitVertex, fs, error);
            if (program == 0) {
                return false;
            }
            const GLuint previous = s.program;
            s.program = program;
            s.draw(kit, s.sphere, Mat4::rotate_x(0.3f) * Mat4::scale(1.2f, 1.2f, 1.2f));
            s.program = previous;
            return true;
        }, Extra::None, Tolerance::raster(2, 1).with_outliers(0.02), 4);

        scene_case("sample_mask_in_out_shader", [](Kit& kit, Scene3& s, std::string& error) {
            static const char* fs =
                "#version 460 core\nin vec2 v_uv;\nin vec3 v_nrm;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    int covered = bitCount(gl_SampleMaskIn[0]);\n"
                "    out_color = vec4(float(covered) / 4.0, 0.5 + 0.5 * v_nrm.y, 0.4, 1.0);\n"
                "    gl_SampleMask[0] = int(gl_SampleMaskIn[0] & (v_uv.x > 0.5 ? 0x3u : 0xFu));\n"
                "}\n";
            const GLuint program = kit.program(Sources::kLitVertex, fs, error);
            if (program == 0) {
                return false;
            }
            const GLuint previous = s.program;
            s.program = program;
            s.draw(kit, s.sphere, Mat4::rotate_x(0.3f) * Mat4::scale(1.2f, 1.2f, 1.2f));
            s.program = previous;
            return true;
        }, Extra::None, Tolerance::raster(2, 1).with_outliers(0.02), 4);

        scene_case("front_facing_and_primitive_id", [](Kit& kit, Scene3& s, std::string& error) {
            static const char* fs =
                "#version 460 core\nin vec3 v_nrm;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    float h = fract(float(gl_PrimitiveID) * 0.0618);\n"
                "    vec3 c = vec3(h, 1.0 - h, 0.5 + 0.5 * v_nrm.y);\n"
                "    out_color = vec4(gl_FrontFacing ? c : c.bgr * 0.5, 1.0);\n"
                "}\n";
            const GLuint program = kit.program(Sources::kLitVertex, fs, error);
            if (program == 0) {
                return false;
            }
            const GLuint previous = s.program;
            s.program = program;
            kit.gl.glDisable(glc::CULL_FACE);
            s.draw(kit, s.torus, Mat4::rotate_x(0.8f));
            s.program = previous;
            return true;
        });

        scene_case("fragment_shader_interlock_ordered", [](Kit& kit, Scene3& s, std::string& error) {
            Api& gl = kit.gl;
            static const char* fs =
                "#version 460 core\n#extension GL_ARB_fragment_shader_interlock : require\nlayout(pixel_interlock_ordered) in;\n"
                "layout(rgba8, binding = 0) coherent uniform image2D u_image;\nin vec3 v_nrm;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    beginInvocationInterlockARB();\n"
                "    vec4 old = imageLoad(u_image, ivec2(gl_FragCoord.xy));\n"
                "    imageStore(u_image, ivec2(gl_FragCoord.xy), mix(old, vec4(abs(v_nrm), 1.0), 0.5));\n"
                "    endInvocationInterlockARB();\n"
                "    out_color = vec4(0.0);\n"
                "}\n";
            const GLuint program = kit.program(Sources::kLitVertex, fs, error);
            if (program == 0) {
                return false;
            }
            GLuint texture = 0;
            gl.glGenTextures(1, &texture);
            kit.defer([&kit, texture] { kit.gl.glDeleteTextures(1, &texture); });
            gl.glBindTexture(glc::TEXTURE_2D, texture);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 128, 128);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            const GLfloat black[4] = {0.1f, 0.1f, 0.2f, 1.0f};
            gl.glClearTexImage(texture, 0, glc::RGBA, glc::FLOAT, black);
            gl.glBindImageTexture(0, texture, 0, glc::FALSE_, 0, glc::READ_WRITE, glc::RGBA8);
            const GLuint previous = s.program;
            s.program = program;
            gl.glDisable(glc::DEPTH_TEST);
            gl.glDisable(glc::CULL_FACE);
            s.draw(kit, s.torus, Mat4::rotate_x(0.8f));
            s.draw(kit, s.sphere, Mat4::scale(0.9f, 0.9f, 0.9f));
            s.program = previous;
            gl.glMemoryBarrier(glc::SHADER_IMAGE_ACCESS_BARRIER_BIT | glc::TEXTURE_FETCH_BARRIER_BIT);
            static const char* show =
                "#version 460 core\nuniform sampler2D u_tex;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { out_color = texelFetch(u_tex, ivec2(gl_FragCoord.xy), 0); }\n";
            const GLuint display = kit.program(Sources::kScreenVertex, show, error);
            if (display == 0) {
                return false;
            }
            kit.u1i(display, "u_tex", 0);
            kit.vertex_array();
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            return true;
        }, Extra::None, Tolerance::tight(1), 1, glc::DEPTH24_STENCIL8, [](Kit& kit) -> std::string {
            return kit.has_extension("GL_ARB_fragment_shader_interlock") ? std::string() : "GL_ARB_fragment_shader_interlock is not exposed";
        });

        scene_case("frag_data_location_indexed_binding", [](Kit& kit, Scene3&, std::string& error) {
            Api& gl = kit.gl;
            static const char* fs =
                "#version 460 core\nin vec3 v_col;\nout vec4 first;\nout vec4 second;\n"
                "void main() { first = vec4(v_col, 0.6); second = vec4(1.0 - v_col, 0.5); }\n";
            const GLuint vs = kit.shader(glc::VERTEX_SHADER, Flat2d::kVertex, error);
            const GLuint fsid = vs != 0 ? kit.shader(glc::FRAGMENT_SHADER, fs, error) : 0;
            if (fsid == 0) {
                return false;
            }
            const GLuint program = gl.glCreateProgram();
            gl.glAttachShader(program, vs);
            gl.glAttachShader(program, fsid);
            gl.glBindFragDataLocationIndexed(program, 0, 0, "first");
            gl.glBindFragDataLocationIndexed(program, 0, 1, "second");
            gl.glLinkProgram(program);
            GLint ok = 0;
            gl.glGetProgramiv(program, glc::LINK_STATUS, &ok);
            if (ok == 0) {
                error = "dual source program failed to link";
                return false;
            }
            kit.defer([&kit, program] { kit.gl.glDeleteProgram(program); });
            const GLint index = gl.glGetFragDataIndex(program, "second");
            const GLint location = gl.glGetFragDataLocation(program, "first");
            if (index != 1 || location != 0) {
                error = "fragment output queries returned index " + std::to_string(index) + " location " + std::to_string(location);
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            gl.glDisable(glc::DEPTH_TEST);
            gl.glEnable(glc::BLEND);
            gl.glBlendFunc(glc::ONE, glc::SRC1_COLOR);
            gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(9, 0.3f, 0.85f)));
            gl.glDrawArrays(glc::TRIANGLES, 0, 9);
            return true;
        }, Extra::None, Tolerance::blend(2));
    }
} kBatch15;

}  // namespace

}  // namespace lsw::render::gl
