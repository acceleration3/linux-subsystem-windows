#include "render/gl/legacy.hpp"
#include "render/gl/scene_extra.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec frag(const std::string& name) { return CaseSpec("frag", name).min_version(4, 6); }
CaseSpec tex(const std::string& name) { return CaseSpec("tex", name).min_version(4, 6); }
CaseSpec query(const std::string& name) { return CaseSpec("query", name).min_version(4, 6); }

const char* kDepthVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec2 in_pos;\n"
    "layout(location = 1) in vec3 in_col;\n"
    "uniform float u_z;\n"
    "out vec3 v_col;\n"
    "void main() { v_col = in_col; gl_Position = vec4(in_pos, u_z, 1.0); }\n";

std::vector<V2> square(float cx, float cy, float h, float r, float g, float b) {
    return {{cx - h, cy - h, r, g, b}, {cx + h, cy - h, r, g, b}, {cx + h, cy + h, r, g, b}, {cx - h, cy - h, r, g, b}, {cx + h, cy + h, r, g, b}, {cx - h, cy + h, r, g, b}};
}

std::uint32_t lcg(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

struct ReadbackFormat {
    const char* name;
    GLenum internal;
    GLenum format;
    GLenum type;
    PixelKind kind;
    unsigned channels;
    unsigned bytes;
    bool half;
};

const struct ExtraBatch {
    ExtraBatch() {
        for (const bool inverted : {false, true}) {
            struct Mode {
                const char* name;
                GLenum wait;
                GLenum wait_inverted;
            };
            for (const GLenum type : {glc::SAMPLES_PASSED, glc::ANY_SAMPLES_PASSED}) {
                unsigned index = 0;
                for (const Mode& mode : {Mode{"wait", glc::QUERY_WAIT, glc::QUERY_WAIT_INVERTED}, Mode{"no_wait", glc::QUERY_NO_WAIT, glc::QUERY_NO_WAIT_INVERTED},
                                         Mode{"by_region_wait", glc::QUERY_BY_REGION_WAIT, glc::QUERY_BY_REGION_WAIT_INVERTED},
                                         Mode{"by_region_no_wait", glc::QUERY_BY_REGION_NO_WAIT, glc::QUERY_BY_REGION_NO_WAIT_INVERTED}}) {
                    const GLenum conditional = inverted ? mode.wait_inverted : mode.wait;
                    const unsigned tint = index++ + (type == glc::ANY_SAMPLES_PASSED ? 4u : 0u);
                    const std::string name = std::string("conditional_render_") + (type == glc::SAMPLES_PASSED ? "samples" : "any_samples") + "_" + mode.name + (inverted ? "_inverted" : "");
                    Cases::single(query(name), [type, conditional, tint, inverted](Kit& kit, CaptureSet& out, std::string& error) {
                        Api& gl = kit.gl;
                        Target target;
                        TargetDesc desc;
                        desc.depth = glc::DEPTH_COMPONENT24;
                        if (!target.create(kit, desc, error)) {
                            return false;
                        }
                        target.bind(kit);
                        Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1.0f, true, true);
                        const GLuint program = kit.program(kDepthVertex, Flat2d::kFragment, error);
                        if (program == 0) {
                            return false;
                        }
                        gl.glEnable(glc::DEPTH_TEST);
                        gl.glDepthFunc(glc::LESS);
                        gl.glUseProgram(program);
                        const float k = static_cast<float>(tint);
                        const auto draw = [&](const std::vector<V2>& verts, float z) {
                            gl.glUniform1f(gl.glGetUniformLocation(program, "u_z"), z);
                            gl.glBindVertexArray(Flat2d::upload(kit, verts));
                            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
                        };
                        draw(square(0.0f, 0.0f, 0.95f, 0.25f, 0.25f, 0.3f), 0.0f);
                        GLuint queries[2] = {};
                        gl.glGenQueries(2, queries);
                        kit.defer([&kit, q0 = queries[0], q1 = queries[1]] {
                            kit.gl.glDeleteQueries(1, &q0);
                            kit.gl.glDeleteQueries(1, &q1);
                        });
                        gl.glColorMask(0, 0, 0, 0);
                        gl.glDepthMask(0);
                        gl.glBeginQuery(type, queries[0]);
                        draw(square(0.0f, 0.0f, 0.4f, 1, 1, 1), -0.5f);
                        gl.glEndQuery(type);
                        gl.glBeginQuery(type, queries[1]);
                        draw(square(0.0f, 0.0f, 0.4f, 1, 1, 1), 0.5f);
                        gl.glEndQuery(type);
                        gl.glColorMask(1, 1, 1, 1);
                        gl.glDepthMask(1);
                        gl.glDisable(glc::DEPTH_TEST);
                        gl.glFinish();
                        const float hue[3] = {0.25f + 0.09f * k, 0.95f - 0.1f * k, 0.3f + 0.08f * k};
                        for (int q = 0; q < 2; ++q) {
                            gl.glBeginConditionalRender(queries[q], conditional);
                            draw(square(-0.5f + 1.0f * static_cast<float>(q), 0.45f, 0.3f, hue[0], hue[1] * (q == 0 ? 1.0f : 0.4f), hue[2]), 0.0f);
                            draw(square(-0.5f + 1.0f * static_cast<float>(q), -0.45f, 0.2f, hue[2], hue[0], inverted ? 0.9f : 0.2f), 0.0f);
                            gl.glEndConditionalRender();
                        }
                        draw(square(0.0f, 0.0f, 0.06f, 1.0f, 1.0f, 1.0f), 0.0f);
                        if (!kit.check("conditional render", error)) {
                            return false;
                        }
                        return Gx::shot(kit, target, out, error);
                    });
                }
            }
        }

        Cases::single(frag("fragment_layer_read_in_fs"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            GLuint array = 0;
            GLuint fbo = 0;
            gl.glGenTextures(1, &array);
            gl.glGenFramebuffers(1, &fbo);
            kit.defer([&kit, array, fbo] {
                kit.gl.glDeleteTextures(1, &array);
                kit.gl.glDeleteFramebuffers(1, &fbo);
            });
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, array);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 1, glc::RGBA8, 32, 32, 4);
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
            gl.glFramebufferTexture(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, array, 0);
            if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                error = "layered framebuffer incomplete";
                return false;
            }
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec3 in_col;\nout vec3 g_col;\nvoid main() { g_col = in_col; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";
            static const char* gs =
                "#version 460 core\nlayout(triangles, invocations = 4) in;\nlayout(triangle_strip, max_vertices = 3) out;\nin vec3 g_col[];\nout vec3 v_col;\n"
                "void main() {\n"
                "    for (int i = 0; i < 3; ++i) {\n"
                "        gl_Layer = gl_InvocationID;\n"
                "        v_col = g_col[i];\n"
                "        gl_Position = gl_in[i].gl_Position * vec4(1.0 - 0.15 * float(gl_InvocationID), 1.0, 1.0, 1.0);\n"
                "        EmitVertex();\n"
                "    }\n"
                "    EndPrimitive();\n"
                "}\n";
            static const char* fs =
                "#version 460 core\nin vec3 v_col;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { float l = float(gl_Layer); out_color = vec4(v_col.r * (1.0 - 0.2 * l), v_col.g * (0.4 + 0.2 * l), l == 2.0 ? 1.0 : v_col.b, 1.0); }\n";
            const GLuint program = kit.program({{glc::VERTEX_SHADER, vs}, {glc::GEOMETRY_SHADER, gs}, {glc::FRAGMENT_SHADER, fs}}, error);
            if (program == 0) {
                return false;
            }
            gl.glViewport(0, 0, 32, 32);
            gl.glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            gl.glUseProgram(program);
            gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(9, 0.2f, 0.9f)));
            gl.glDrawArrays(glc::TRIANGLES, 0, 9);
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            const GLuint show = rig.program(kit, "void main() { vec2 c = gl_FragCoord.xy / 128.0; int l = int(c.x * 2.0) + 2 * int(c.y * 2.0); out_color = texelFetch(u_tex, ivec3(fract(c * 2.0) * 32.0, l), 0); }", error, "uniform sampler2DArray u_tex;");
            if (show == 0) {
                return false;
            }
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, array);
            kit.u1i(show, "u_tex", 0);
            rig.draw(kit, show);
            if (!kit.check("layer read", error)) {
                return false;
            }
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::single(CaseSpec("compat", "accumulation_buffer_average").min_version(4, 6).compat_profile(), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            gl.glBindFramebuffer(glc::FRAMEBUFFER, 0);
            gl.glViewport(0, 0, 128, 128);
            gl.glMatrixMode(glc::PROJECTION);
            gl.glLoadIdentity();
            gl.glMatrixMode(glc::MODELVIEW);
            gl.glLoadIdentity();
            gl.glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
            gl.glClearAccum(0, 0, 0, 0);
            gl.glClear(glc::COLOR_BUFFER_BIT | glc::ACCUM_BUFFER_BIT);
            for (int pass = 0; pass < 2; ++pass) {
                gl.glClear(glc::COLOR_BUFFER_BIT);
                gl.glBegin(glc::TRIANGLES);
                gl.glColor3f(0.9f, 0.3f, 0.2f);
                gl.glVertex2f(-0.8f + 0.5f * static_cast<float>(pass), -0.7f);
                gl.glColor3f(0.2f, 0.9f, 0.3f);
                gl.glVertex2f(0.4f + 0.3f * static_cast<float>(pass), -0.6f);
                gl.glColor3f(0.2f, 0.3f, 0.9f);
                gl.glVertex2f(0.0f, 0.8f);
                gl.glEnd();
                gl.glAccum(glc::ACCUM, 0.5f);
            }
            gl.glAccum(glc::RETURN, 1.0f);
            Image image;
            if (!kit.read_color(0, glc::BACK, glc::RGBA8, 128, 128, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        }, [](Kit& kit) -> std::string {
            if (kit.integer(glc::ACCUM_RED_BITS) == 0) {
                return "the default framebuffer has no accumulation buffer";
            }
            return {};
        });

        Cases::single(tex("invalidate_tex_sub_image_rewrite"), [](Kit& kit, CaptureSet& out, std::string& error) {
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 32, 32);
            const auto first = TexRig::cells(32, 32, 1, 2);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 32, 32, glc::RGBA, glc::UNSIGNED_BYTE, first.data());
            gl.glInvalidateTexSubImage(t, 0, 8, 4, 0, 16, 12, 1);
            const auto second = TexRig::cells(16, 12, 1, 3);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 8, 4, 16, 12, glc::RGBA, glc::UNSIGNED_BYTE, second.data());
            const GLuint program = rig.program(kit, "void main() { out_color = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 4.0), 0); }", error, "uniform sampler2D u_tex;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            rig.draw(kit, program);
            if (!kit.check("invalidate sub image", error)) {
                return false;
            }
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::single(tex("invalidate_tex_image_level_rewrite"), [](Kit& kit, CaptureSet& out, std::string& error) {
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST_MIPMAP_NEAREST);
            gl.glTexStorage2D(glc::TEXTURE_2D, 3, glc::RGBA8, 32, 32);
            for (unsigned level = 0; level < 3; ++level) {
                const unsigned extent = 32u >> level;
                const auto data = TexRig::cells(extent, extent, 1, 2);
                gl.glTexSubImage2D(glc::TEXTURE_2D, static_cast<GLint>(level), 0, 0, static_cast<GLsizei>(extent), static_cast<GLsizei>(extent), glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            }
            gl.glInvalidateTexImage(t, 1);
            auto replacement = TexRig::cells(16, 16, 1, 4);
            for (std::size_t i = 0; i < replacement.size(); i += 4) {
                replacement[i] = static_cast<std::uint8_t>(255 - replacement[i]);
            }
            gl.glTexSubImage2D(glc::TEXTURE_2D, 1, 0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, replacement.data());
            const GLuint program = rig.program(kit, "void main() { vec2 c = gl_FragCoord.xy / 128.0; int lod = int(c.y * 3.0); ivec2 s = textureSize(u_tex, lod); out_color = texelFetch(u_tex, ivec2(c.x * float(s.x), fract(c.y * 3.0) * float(s.y)), lod); }", error,
                                               "uniform sampler2D u_tex;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            rig.draw(kit, program);
            if (!kit.check("invalidate image", error)) {
                return false;
            }
            return Gx::shot(kit, rig.target, out, error);
        });

        const ReadbackFormat formats[] = {
            {"rgba8", glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, PixelKind::Rgba8Unorm, 4, 1, false},
            {"rgba16", glc::RGBA16, glc::RGBA, glc::UNSIGNED_SHORT, PixelKind::Rgba16Unorm, 4, 2, false},
            {"r16", glc::R16, glc::RED, glc::UNSIGNED_SHORT, PixelKind::R16Unorm, 1, 2, false},
            {"rgba16f", glc::RGBA16F, glc::RGBA, glc::HALF_FLOAT, PixelKind::Rgba16Float, 4, 2, true},
            {"rg16f", glc::RG16F, glc::RG, glc::HALF_FLOAT, PixelKind::Rg16Float, 2, 2, true},
            {"r16f", glc::R16F, glc::RED, glc::HALF_FLOAT, PixelKind::R16Float, 1, 2, true},
            {"rgba32f", glc::RGBA32F, glc::RGBA, glc::FLOAT, PixelKind::Rgba32Float, 4, 4, false},
            {"rg32f", glc::RG32F, glc::RG, glc::FLOAT, PixelKind::Rg32Float, 2, 4, false},
            {"r32f", glc::R32F, glc::RED, glc::FLOAT, PixelKind::R32Float, 1, 4, false},
            {"rgba8ui", glc::RGBA8UI, glc::RGBA_INTEGER, glc::UNSIGNED_BYTE, PixelKind::Rgba8Uint, 4, 1, false},
            {"rgba16ui", glc::RGBA16UI, glc::RGBA_INTEGER, glc::UNSIGNED_SHORT, PixelKind::Rgba16Uint, 4, 2, false},
            {"rgba32ui", glc::RGBA32UI, glc::RGBA_INTEGER, glc::UNSIGNED_INT, PixelKind::Rgba32Uint, 4, 4, false},
            {"r8ui", glc::R8UI, glc::RED_INTEGER, glc::UNSIGNED_BYTE, PixelKind::R8Uint, 1, 1, false},
        };
        unsigned seed_counter = 0;
        for (const ReadbackFormat& f : formats) {
            const unsigned seed = 0x1234u + 77u * (++seed_counter);
            for (const bool sub_image : {false, true}) {
                if (sub_image && f.channels != 4) {
                    continue;
                }
                Cases::single(tex(std::string("get_tex_image_") + (sub_image ? "sub_image_" : "") + f.name), [f, seed, sub_image](Kit& kit, CaptureSet& out, std::string& error) {
                    Api& gl = kit.gl;
                    constexpr unsigned size = 24;
                    std::vector<std::uint8_t> data(static_cast<std::size_t>(size) * size * f.channels * f.bytes);
                    std::uint32_t state = seed;
                    for (std::size_t i = 0; i + f.bytes <= data.size(); i += f.bytes) {
                        const std::uint32_t r = lcg(state) >> 8;
                        if (f.half) {
                            const std::uint16_t h = PixelInfo::float_to_half(std::ldexp(1.0f + static_cast<float>(r & 0x3FF) / 1024.0f, static_cast<int>((r >> 10) % 10) - 6));
                            std::memcpy(&data[i], &h, 2);
                        } else if (f.type == glc::FLOAT) {
                            const float v = std::ldexp(1.0f + static_cast<float>(r & 0x7FFF) / 32768.0f, static_cast<int>((r >> 15) % 10) - 6);
                            std::memcpy(&data[i], &v, 4);
                        } else if (f.bytes == 4) {
                            const std::uint32_t v = (r << 8) ^ lcg(state);
                            std::memcpy(&data[i], &v, 4);
                        } else if (f.bytes == 2) {
                            const std::uint16_t v = static_cast<std::uint16_t>(r);
                            std::memcpy(&data[i], &v, 2);
                        } else {
                            data[i] = static_cast<std::uint8_t>(r);
                        }
                    }
                    GLuint t = 0;
                    if (sub_image) {
                        gl.glCreateTextures(glc::TEXTURE_2D, 1, &t);
                        kit.defer([&kit, t] { kit.gl.glDeleteTextures(1, &t); });
                        gl.glTextureStorage2D(t, 1, f.internal, size, size);
                        gl.glTextureSubImage2D(t, 0, 0, 0, size, size, f.format, f.type, data.data());
                    } else {
                        t = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
                        gl.glTexStorage2D(glc::TEXTURE_2D, 1, f.internal, size, size);
                        gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, size, size, f.format, f.type, data.data());
                    }
                    Image image(sub_image ? 16 : size, sub_image ? 12 : size, f.kind);
                    if (sub_image) {
                        gl.glGetTextureSubImage(t, 0, 5, 7, 0, 16, 12, 1, f.format, f.type, static_cast<GLsizei>(image.bytes().size()), image.bytes().data());
                    } else {
                        gl.glGetTexImage(glc::TEXTURE_2D, 0, f.format, f.type, image.bytes().data());
                    }
                    if (!kit.check("texture readback", error)) {
                        return false;
                    }
                    const std::size_t texel = static_cast<std::size_t>(f.channels) * f.bytes;
                    for (unsigned y = 0; y < image.height(); ++y) {
                        for (unsigned x = 0; x < image.width(); ++x) {
                            const std::size_t source = (static_cast<std::size_t>(y + (sub_image ? 7 : 0)) * size + x + (sub_image ? 5 : 0)) * texel;
                            if (std::memcmp(image.row(y) + static_cast<std::size_t>(x) * texel, &data[source], texel) != 0) {
                                error = "the readback of texel " + std::to_string(x) + "," + std::to_string(y) + " differs from the uploaded bytes";
                                return false;
                            }
                        }
                    }
                    out.add("", std::move(image));
                    return true;
                });
            }
        }

        Cases::single(CaseSpec("compat", "load_transpose_matrix_rotation").min_version(4, 6).compat_profile(), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const float c = std::cos(0.6f);
            const float s = std::sin(0.6f);
            const float rows[16] = {c, -s, 0, 0.2f, s, c, 0, -0.1f, 0, 0, 1, 0, 0, 0, 0, 1};
            gl.glLoadTransposeMatrixf(rows);
            const float scale_rows[16] = {0.7f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            gl.glMultTransposeMatrixf(scale_rows);
            gl.glBegin(glc::QUADS);
            gl.glColor3f(0.9f, 0.4f, 0.2f);
            gl.glVertex2f(-0.9f, -0.9f);
            gl.glColor3f(0.3f, 0.9f, 0.3f);
            gl.glVertex2f(0.9f, -0.9f);
            gl.glColor3f(0.2f, 0.4f, 0.9f);
            gl.glVertex2f(0.9f, 0.9f);
            gl.glColor3f(0.9f, 0.9f, 0.3f);
            gl.glVertex2f(-0.9f, 0.9f);
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(tex("store_pack_swap_bytes_readpixels"), [](Kit& kit, CaptureSet& out, std::string& error) {
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint flat = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (flat == 0) {
                return false;
            }
            kit.u1f(flat, "u_size", 1.0f);
            gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(9, 0.3f, 0.85f)));
            gl.glDrawArrays(glc::TRIANGLES, 0, 9);
            std::vector<std::uint16_t> plain(128 * 128);
            std::vector<std::uint16_t> swapped(128 * 128);
            gl.glPixelStorei(glc::PACK_ALIGNMENT, 2);
            gl.glReadPixels(0, 0, 128, 128, glc::GREEN, glc::UNSIGNED_SHORT, plain.data());
            gl.glPixelStorei(glc::PACK_SWAP_BYTES, 1);
            gl.glReadPixels(0, 0, 128, 128, glc::GREEN, glc::UNSIGNED_SHORT, swapped.data());
            gl.glPixelStorei(glc::PACK_SWAP_BYTES, 0);
            gl.glPixelStorei(glc::PACK_ALIGNMENT, 4);
            for (std::size_t i = 0; i < plain.size(); ++i) {
                const std::uint16_t expected = static_cast<std::uint16_t>((plain[i] >> 8) | (plain[i] << 8));
                if (swapped[i] != expected) {
                    error = "PACK_SWAP_BYTES readback differs from the byte-swapped plain readback at texel " + std::to_string(i);
                    return false;
                }
            }
            static_cast<void>(TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST));
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::R16, 128, 128);
            gl.glPixelStorei(glc::UNPACK_SWAP_BYTES, 1);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 128, 128, glc::RED, glc::UNSIGNED_SHORT, swapped.data());
            gl.glPixelStorei(glc::UNPACK_SWAP_BYTES, 0);
            gl.glClearColor(0, 0, 0, 1);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            const GLuint program = rig.program(kit, "void main() { float v = texelFetch(u_tex, ivec2(gl_FragCoord.xy), 0).r; out_color = vec4(v, fract(v * 8.0), 1.0 - v, 1.0); }", error, "uniform sampler2D u_tex;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_tex", 0);
            rig.draw(kit, program);
            if (!kit.check("swap bytes", error)) {
                return false;
            }
            return Gx::shot(kit, rig.target, out, error);
        });
    }
} kExtraBatch;

}  // namespace

}  // namespace lsw::render::gl
