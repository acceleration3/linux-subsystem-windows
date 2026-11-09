#include "render/gl/flat2d.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec frag(const std::string& name) { return CaseSpec("frag", name).min_version(4, 6); }

const char* kBlendVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec2 in_pos;\n"
    "layout(location = 1) in vec3 in_col;\n"
    "uniform vec2 u_offset;\n"
    "uniform float u_scale;\n"
    "out vec3 v_col;\n"
    "out vec2 v_pos;\n"
    "void main() {\n"
    "    v_col = in_col;\n"
    "    v_pos = in_pos;\n"
    "    gl_Position = vec4(in_pos * u_scale + u_offset, 0.0, 1.0);\n"
    "}\n";

const char* kBlendFragment =
    "#version 460 core\n"
    "in vec3 v_col;\n"
    "in vec2 v_pos;\n"
    "uniform float u_alpha;\n"
    "layout(location = 0, index = 0) out vec4 out_color;\n"
    "layout(location = 0, index = 1) out vec4 out_second;\n"
    "void main() {\n"
    "    float a = u_alpha * (0.35 + 0.65 * (v_pos.x * 0.5 + 0.5));\n"
    "    out_color = vec4(v_col, a);\n"
    "    out_second = vec4(1.0 - v_col.bgr, 0.4 + 0.5 * (v_pos.y * 0.5 + 0.5));\n"
    "}\n";

struct BlendRig {
    Target target;
    GLuint program = 0;
    GLuint vao = 0;

    bool open(Kit& kit, std::string& error, int samples = 1, GLenum color = glc::RGBA8) {
        TargetDesc desc;
        desc.samples = samples;
        desc.colors = {color};
        if (!target.create(kit, desc, error)) {
            return false;
        }
        target.bind(kit);
        Gx::clear(kit, 0.12f, 0.12f, 0.16f);
        program = kit.program(kBlendVertex, kBlendFragment, error);
        if (program == 0) {
            return false;
        }
        vao = Flat2d::upload(kit, Flat2d::tile(1));
        const GLushort idx[6] = {0, 1, 2, 0, 2, 3};
        kit.buffer(glc::ELEMENT_ARRAY_BUFFER, idx, sizeof idx, glc::STATIC_DRAW);
        kit.u1f(program, "u_alpha", 0.7f);
        return true;
    }

    void quad(Kit& kit, float x, float y, float scale, float alpha) {
        kit.u2f(program, "u_offset", x, y);
        kit.u1f(program, "u_scale", scale);
        kit.u1f(program, "u_alpha", alpha);
        kit.gl.glBindVertexArray(vao);
        kit.gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
    }

    void backdrop(Kit& kit) {
        kit.gl.glDisable(glc::BLEND);
        quad(kit, 0.0f, 0.0f, 2.0f, 1.0f);
    }

    void grid(Kit& kit, float cx) {
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 2; ++i) {
                quad(kit, cx + (static_cast<float>(i) - 0.5f) * 0.45f, (static_cast<float>(j) - 1.0f) * 0.55f, 0.2f, 0.3f + 0.3f * static_cast<float>((i + j) % 3));
            }
        }
    }
};

struct Factor {
    const char* name;
    GLenum value;
    bool dst_ok;
};

const struct Batch14 {
    Batch14() {
        const Factor factors[] = {{"zero", glc::ZERO, true},
                                  {"one", glc::ONE, true},
                                  {"src_color", glc::SRC_COLOR, true},
                                  {"one_minus_src_color", glc::ONE_MINUS_SRC_COLOR, true},
                                  {"dst_color", glc::DST_COLOR, true},
                                  {"one_minus_dst_color", glc::ONE_MINUS_DST_COLOR, true},
                                  {"src_alpha", glc::SRC_ALPHA, true},
                                  {"one_minus_src_alpha", glc::ONE_MINUS_SRC_ALPHA, true},
                                  {"dst_alpha", glc::DST_ALPHA, true},
                                  {"one_minus_dst_alpha", glc::ONE_MINUS_DST_ALPHA, true},
                                  {"constant_color", glc::CONSTANT_COLOR, true},
                                  {"one_minus_constant_color", glc::ONE_MINUS_CONSTANT_COLOR, true},
                                  {"constant_alpha", glc::CONSTANT_ALPHA, true},
                                  {"one_minus_constant_alpha", glc::ONE_MINUS_CONSTANT_ALPHA, true},
                                  {"src_alpha_saturate", glc::SRC_ALPHA_SATURATE, false},
                                  {"src1_color", glc::SRC1_COLOR, true},
                                  {"one_minus_src1_color", glc::ONE_MINUS_SRC1_COLOR, true},
                                  {"src1_alpha", glc::SRC1_ALPHA, true},
                                  {"one_minus_src1_alpha", glc::ONE_MINUS_SRC1_ALPHA, true}};
        for (const Factor& f : factors) {
            Cases::single(frag(std::string("blend_factor_") + f.name).tolerance(Tolerance::blend(2)), [f](Kit& kit, CaptureSet& out, std::string& error) {
                BlendRig rig;
                if (!rig.open(kit, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                rig.backdrop(kit);
                gl.glEnable(glc::BLEND);
                gl.glBlendColor(0.6f, 0.3f, 0.8f, 0.5f);
                gl.glBlendEquation(glc::FUNC_ADD);
                gl.glBlendFunc(f.value, glc::ONE);
                rig.grid(kit, -0.5f);
                gl.glBlendFunc(glc::ONE, f.dst_ok ? f.value : glc::ONE_MINUS_SRC_ALPHA);
                rig.grid(kit, 0.5f);
                return Gx::shot(kit, rig.target, out, error);
            });
        }

        struct Equation {
            const char* name;
            GLenum value;
        };
        for (const Equation& e : {Equation{"add", glc::FUNC_ADD}, Equation{"subtract", glc::FUNC_SUBTRACT}, Equation{"reverse_subtract", glc::FUNC_REVERSE_SUBTRACT},
                                  Equation{"min", glc::MIN}, Equation{"max", glc::MAX}}) {
            Cases::single(frag(std::string("blend_equation_") + e.name).tolerance(Tolerance::blend(2)), [e](Kit& kit, CaptureSet& out, std::string& error) {
                BlendRig rig;
                if (!rig.open(kit, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                rig.backdrop(kit);
                gl.glEnable(glc::BLEND);
                gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
                gl.glBlendEquationSeparate(e.value, glc::FUNC_ADD);
                rig.grid(kit, -0.5f);
                gl.glBlendFuncSeparate(glc::ONE, glc::ONE, glc::ONE, glc::ONE);
                gl.glBlendEquation(e.value);
                rig.grid(kit, 0.5f);
                return Gx::shot(kit, rig.target, out, error);
            });
        }

        Cases::single(frag("blend_separate_rgb_alpha_funcs").tolerance(Tolerance::blend(2)), [](Kit& kit, CaptureSet& out, std::string& error) {
            BlendRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            rig.backdrop(kit);
            gl.glEnable(glc::BLEND);
            gl.glBlendFuncSeparate(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA, glc::ONE, glc::ZERO);
            rig.grid(kit, -0.5f);
            gl.glBlendFuncSeparate(glc::DST_COLOR, glc::ZERO, glc::ZERO, glc::SRC_ALPHA);
            rig.grid(kit, 0.5f);
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::single(frag("blend_per_draw_buffer"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            TargetDesc desc;
            desc.colors = {glc::RGBA8, glc::RGBA8, glc::RGBA16F};
            if (!target.create(kit, desc, error)) {
                return false;
            }
            target.bind(kit);
            Api& gl = kit.gl;
            gl.glClearColor(0.2f, 0.25f, 0.3f, 1.0f);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            static const char* fs =
                "#version 460 core\nin vec3 v_col;\nin vec2 v_pos;\nlayout(location = 0) out vec4 c0;\nlayout(location = 1) out vec4 c1;\nlayout(location = 2) out vec4 c2;\n"
                "void main() { float a = 0.35 + 0.4 * (v_pos.x * 0.5 + 0.5); c0 = vec4(v_col, a); c1 = vec4(v_col.bgr, a); c2 = vec4(v_col * 2.0, a); }\n";
            const GLuint program = kit.program(kBlendVertex, fs, error);
            if (program == 0) {
                return false;
            }
            const GLuint vao = Flat2d::upload(kit, Flat2d::tile(1));
            const GLushort idx[6] = {0, 1, 2, 0, 2, 3};
            kit.buffer(glc::ELEMENT_ARRAY_BUFFER, idx, sizeof idx, glc::STATIC_DRAW);
            gl.glEnable(glc::BLEND);
            gl.glBlendFunci(0, glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
            gl.glBlendFunci(1, glc::ONE, glc::ONE);
            gl.glBlendEquationi(1, glc::FUNC_REVERSE_SUBTRACT);
            gl.glDisablei(glc::BLEND, 2);
            gl.glColorMaski(1, 1, 0, 1, 1);
            kit.u1f(program, "u_scale", 0.9f);
            for (int i = 0; i < 3; ++i) {
                kit.u2f(program, "u_offset", -0.25f + 0.25f * static_cast<float>(i), 0.0f);
                gl.glBindVertexArray(vao);
                gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
            }
            gl.glColorMaski(1, 1, 1, 1, 1);
            if (!kit.check("per-buffer blend", error)) {
                return false;
            }
            for (std::size_t i = 0; i < 3; ++i) {
                Image image;
                if (!target.read_color(kit, i, image, error)) {
                    return false;
                }
                out.add("c" + std::to_string(i), std::move(image));
            }
            return true;
        });

        struct Logic {
            const char* name;
            GLenum value;
        };
        const Logic logics[] = {{"clear", glc::CLEAR}, {"set", glc::SET}, {"copy", glc::COPY}, {"copy_inverted", glc::COPY_INVERTED},
                                {"noop", glc::NOOP}, {"invert", glc::INVERT}, {"and", glc::AND}, {"nand", glc::NAND},
                                {"or", glc::OR}, {"nor", glc::NOR}, {"xor", glc::XOR}, {"equiv", glc::EQUIV},
                                {"and_reverse", glc::AND_REVERSE}, {"and_inverted", glc::AND_INVERTED}, {"or_reverse", glc::OR_REVERSE}, {"or_inverted", glc::OR_INVERTED}};
        for (const Logic& l : logics) {
            Cases::single(frag(std::string("logic_op_") + l.name), [l](Kit& kit, CaptureSet& out, std::string& error) {
                BlendRig rig;
                if (!rig.open(kit, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                rig.backdrop(kit);
                gl.glEnable(glc::COLOR_LOGIC_OP);
                gl.glLogicOp(l.value);
                rig.grid(kit, -0.5f);
                gl.glEnable(glc::BLEND);
                rig.grid(kit, 0.5f);
                return Gx::shot(kit, rig.target, out, error);
            });
        }

        Cases::single(frag("color_mask_channels"), [](Kit& kit, CaptureSet& out, std::string& error) {
            BlendRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            rig.backdrop(kit);
            const GLboolean masks[6][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 1, 0}, {1, 0, 1, 1}};
            for (int i = 0; i < 6; ++i) {
                gl.glColorMask(masks[i][0], masks[i][1], masks[i][2], masks[i][3]);
                rig.quad(kit, -0.6f + 0.4f * static_cast<float>(i % 4), -0.4f + 0.7f * static_cast<float>(i / 4), 0.25f, 1.0f);
            }
            gl.glColorMask(1, 1, 1, 1);
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::single(frag("color_mask_per_buffer"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            TargetDesc desc;
            desc.colors = {glc::RGBA8, glc::RGBA8};
            if (!target.create(kit, desc, error)) {
                return false;
            }
            target.bind(kit);
            Api& gl = kit.gl;
            Gx::clear(kit, 0.1f, 0.1f, 0.1f);
            static const char* fs =
                "#version 460 core\nin vec3 v_col;\nin vec2 v_pos;\nlayout(location = 0) out vec4 c0;\nlayout(location = 1) out vec4 c1;\n"
                "void main() { c0 = vec4(v_col, 1.0); c1 = vec4(v_col.bgr, 1.0); }\n";
            const GLuint program = kit.program(kBlendVertex, fs, error);
            if (program == 0) {
                return false;
            }
            const GLuint vao = Flat2d::upload(kit, Flat2d::tile(1));
            const GLushort idx[6] = {0, 1, 2, 0, 2, 3};
            kit.buffer(glc::ELEMENT_ARRAY_BUFFER, idx, sizeof idx, glc::STATIC_DRAW);
            gl.glBindVertexArray(vao);
            kit.u1f(program, "u_scale", 0.8f);
            const GLboolean masks[3][2][4] = {{{1, 0, 0, 1}, {0, 1, 1, 1}}, {{0, 1, 0, 1}, {1, 0, 1, 1}}, {{1, 1, 0, 1}, {0, 0, 1, 1}}};
            for (int i = 0; i < 3; ++i) {
                gl.glColorMaski(0, masks[i][0][0], masks[i][0][1], masks[i][0][2], masks[i][0][3]);
                gl.glColorMaski(1, masks[i][1][0], masks[i][1][1], masks[i][1][2], masks[i][1][3]);
                kit.u2f(program, "u_offset", -0.5f + 0.5f * static_cast<float>(i), -0.2f + 0.2f * static_cast<float>(i));
                gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
            }
            gl.glColorMaski(0, 1, 1, 1, 1);
            gl.glColorMaski(1, 1, 1, 1, 1);
            if (!kit.check("masks", error)) {
                return false;
            }
            for (std::size_t i = 0; i < 2; ++i) {
                Image image;
                if (!target.read_color(kit, i, image, error)) {
                    return false;
                }
                out.add("c" + std::to_string(i), std::move(image));
            }
            return true;
        });

        for (const bool one : {false, true}) {
            Cases::single(frag(one ? "alpha_to_one_msaa" : "alpha_to_coverage_msaa").tolerance(Tolerance::blend(2).with_outliers(0.02)), [one](Kit& kit, CaptureSet& out, std::string& error) {
                BlendRig rig;
                if (!rig.open(kit, error, 4)) {
                    return false;
                }
                Api& gl = kit.gl;
                rig.backdrop(kit);
                gl.glEnable(glc::SAMPLE_ALPHA_TO_COVERAGE);
                if (one) {
                    gl.glEnable(glc::SAMPLE_ALPHA_TO_ONE);
                    gl.glEnable(glc::BLEND);
                    gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
                }
                rig.grid(kit, -0.5f);
                rig.grid(kit, 0.5f);
                return Gx::shot(kit, rig.target, out, error);
            });
        }

        for (const bool enabled : {false, true}) {
            Cases::single(frag(enabled ? "framebuffer_srgb_enabled" : "framebuffer_srgb_disabled").tolerance(Tolerance::blend(2)), [enabled](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                TargetDesc desc;
                desc.colors = {glc::SRGB8_ALPHA8};
                Api& gl = kit.gl;
                GLuint fbo = 0;
                GLuint texture = 0;
                gl.glGenFramebuffers(1, &fbo);
                gl.glGenTextures(1, &texture);
                kit.defer([&kit, fbo, texture] {
                    kit.gl.glDeleteFramebuffers(1, &fbo);
                    kit.gl.glDeleteTextures(1, &texture);
                });
                gl.glBindTexture(glc::TEXTURE_2D, texture);
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::SRGB8_ALPHA8, 128, 128);
                gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
                gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, texture, 0);
                gl.glViewport(0, 0, 128, 128);
                if (enabled) {
                    gl.glEnable(glc::FRAMEBUFFER_SRGB);
                } else {
                    gl.glDisable(glc::FRAMEBUFFER_SRGB);
                }
                gl.glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
                gl.glClear(glc::COLOR_BUFFER_BIT);
                const GLuint program = kit.program(kBlendVertex, kBlendFragment, error);
                if (program == 0) {
                    return false;
                }
                const GLuint vao = Flat2d::upload(kit, Flat2d::tile(1));
                const GLushort idx[6] = {0, 1, 2, 0, 2, 3};
                kit.buffer(glc::ELEMENT_ARRAY_BUFFER, idx, sizeof idx, glc::STATIC_DRAW);
                gl.glBindVertexArray(vao);
                gl.glEnable(glc::BLEND);
                gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
                for (int i = 0; i < 4; ++i) {
                    kit.u2f(program, "u_offset", -0.5f + 0.33f * static_cast<float>(i), -0.4f + 0.25f * static_cast<float>(i));
                    kit.u1f(program, "u_scale", 0.5f);
                    kit.u1f(program, "u_alpha", 0.3f + 0.2f * static_cast<float>(i));
                    gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
                }
                gl.glFinish();
                if (!kit.check("srgb", error)) {
                    return false;
                }
                Image image(128, 128, PixelKind::Rgba8Unorm);
                gl.glReadBuffer(glc::COLOR_ATTACHMENT0);
                gl.glReadPixels(0, 0, 128, 128, glc::RGBA, glc::UNSIGNED_BYTE, image.bytes().data());
                image.flip_vertical();
                out.add("", std::move(image));
                return kit.check("srgb readback", error);
            });
        }

        const char* advanced[4] = {"multiply", "screen", "difference", "hsl_hue"};
        const GLenum advanced_modes[4] = {glc::MULTIPLY_KHR, glc::SCREEN_KHR, glc::DIFFERENCE_KHR, glc::HSL_HUE_KHR};
        for (int i = 0; i < 4; ++i) {
            const GLenum mode = advanced_modes[i];
            Cases::single(frag(std::string("blend_advanced_") + advanced[i]).tolerance(Tolerance::blend(4)).extension("GL_KHR_blend_equation_advanced"), [mode](Kit& kit, CaptureSet& out, std::string& error) {
                BlendRig rig;
                if (!rig.open(kit, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                static const char* fs =
                    "#version 460 core\n#extension GL_KHR_blend_equation_advanced : require\nlayout(blend_support_all_equations) out;\n"
                    "in vec3 v_col;\nin vec2 v_pos;\nuniform float u_alpha;\nlayout(location = 0) out vec4 out_color;\n"
                    "void main() { out_color = vec4(v_col, u_alpha * (0.5 + 0.5 * (v_pos.x * 0.5 + 0.5))); }\n";
                const GLuint program = kit.program(kBlendVertex, fs, error);
                if (program == 0) {
                    return false;
                }
                rig.program = program;
                rig.backdrop(kit);
                gl.glEnable(glc::BLEND);
                gl.glBlendEquation(mode);
                rig.grid(kit, -0.5f);
                gl.glBlendBarrierKHR();
                rig.grid(kit, 0.5f);
                return Gx::shot(kit, rig.target, out, error);
            });
        }
    }
} kBatch14;

}  // namespace

}  // namespace lsw::render::gl
