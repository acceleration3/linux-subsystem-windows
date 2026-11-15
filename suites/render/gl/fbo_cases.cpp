#include "render/gl/flat2d.hpp"
#include "render/gl/texrig.hpp"

#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec fbo(const std::string& name) { return CaseSpec("fbo", name).min_version(4, 6); }

struct Flat {
    GLuint program = 0;

    bool open(Kit& kit, std::string& error) {
        program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
        if (program == 0) {
            return false;
        }
        kit.u1f(program, "u_size", 1.0f);
        return true;
    }

    void shape(Kit& kit, unsigned n, float inner, float outer, float phase = 0.0f) {
        kit.gl.glUseProgram(program);
        kit.gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(n, inner, outer, phase)));
        kit.gl.glDrawArrays(glc::TRIANGLES, 0, static_cast<GLsizei>(n));
    }
};

GLuint make_fbo(Kit& kit) {
    GLuint id = 0;
    kit.gl.glGenFramebuffers(1, &id);
    kit.gl.glBindFramebuffer(glc::FRAMEBUFFER, id);
    kit.defer([&kit, id] { kit.gl.glDeleteFramebuffers(1, &id); });
    return id;
}

const struct Batch20 {
    Batch20() {
        Cases::single(fbo("attach_array_layer_volume_slice_cube_face"), [](Kit& kit, CaptureSet& out, std::string& error) {
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            Flat flat;
            if (!flat.open(kit, error)) {
                return false;
            }
            const GLuint array = TexRig::texture(kit, glc::TEXTURE_2D_ARRAY);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 1, glc::RGBA8, 32, 32, 3);
            const GLuint volume = TexRig::texture(kit, glc::TEXTURE_3D);
            gl.glTexStorage3D(glc::TEXTURE_3D, 1, glc::RGBA8, 32, 32, 3);
            const GLuint cube = TexRig::texture(kit, glc::TEXTURE_CUBE_MAP);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::RGBA8, 32, 32);
            make_fbo(kit);
            gl.glViewport(0, 0, 32, 32);
            for (int i = 0; i < 3; ++i) {
                gl.glFramebufferTextureLayer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, array, 0, i);
                gl.glClearColor(0.1f * static_cast<float>(i), 0.1f, 0.2f, 1);
                gl.glClear(glc::COLOR_BUFFER_BIT);
                flat.shape(kit, 3 + static_cast<unsigned>(i) * 3, 0.3f, 0.8f);
                gl.glFramebufferTextureLayer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, volume, 0, i);
                gl.glClearColor(0.2f, 0.1f * static_cast<float>(i), 0.1f, 1);
                gl.glClear(glc::COLOR_BUFFER_BIT);
                flat.shape(kit, 6, 0.3f + 0.2f * static_cast<float>(i), 0.9f, 0.4f * static_cast<float>(i));
            }
            gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_CUBE_MAP_NEGATIVE_Y, cube, 0);
            gl.glClearColor(0.3f, 0.3f, 0.1f, 1);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            flat.shape(kit, 9, 0.2f, 0.9f);
            if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                error = "cube face framebuffer incomplete";
                return false;
            }
            rig.target.bind(kit);
            const GLuint program = rig.program(kit,
                                               "void main() { vec2 c = gl_FragCoord.xy / 128.0; int i = int(c.x * 3.0); vec2 p = fract(vec2(c.x * 3.0, c.y * 2.0)); "
                                               "out_color = c.y < 0.5 ? texture(u_a, vec3(p, float(i))) : (i < 2 ? texture(u_v, vec3(p, (float(i) + 0.5) / 3.0)) : texture(u_c, vec3(p.x * 2.0 - 1.0, -1.0, p.y * 2.0 - 1.0))); }",
                                               error, "uniform sampler2DArray u_a;\nuniform sampler3D u_v;\nuniform samplerCube u_c;");
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_a", 0);
            kit.u1i(program, "u_v", 1);
            kit.u1i(program, "u_c", 2);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, array);
            gl.glActiveTexture(glc::TEXTURE1);
            gl.glBindTexture(glc::TEXTURE_3D, volume);
            gl.glActiveTexture(glc::TEXTURE2);
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, cube);
            rig.draw(kit, program);
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::single(fbo("renderbuffer_color_depth_stencil_readback"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            TargetDesc desc;
            desc.depth = glc::DEPTH32F_STENCIL8;
            if (!target.create(kit, desc, error)) {
                return false;
            }
            target.bind(kit);
            Api& gl = kit.gl;
            Gx::clear(kit, 0.1f, 0.1f, 0.1f, 1.0f, true, true);
            Flat flat;
            if (!flat.open(kit, error)) {
                return false;
            }
            gl.glEnable(glc::DEPTH_TEST);
            gl.glEnable(glc::STENCIL_TEST);
            gl.glStencilFunc(glc::ALWAYS, 3, 0xFF);
            gl.glStencilOp(glc::KEEP, glc::KEEP, glc::REPLACE);
            flat.shape(kit, 9, 0.3f, 0.9f);
            return Gx::shot(kit, target, out, error) && Gx::shot_depth(kit, target, out, error) && Gx::shot_stencil(kit, target, out, error);
        });

        Cases::single(fbo("draw_buffers_eight_mixed_with_none"), [](Kit& kit, CaptureSet& out, std::string& error) {
            if (kit.integer(glc::MAX_COLOR_ATTACHMENTS) < 8 || kit.integer(glc::MAX_DRAW_BUFFERS) < 8) {
                error = "fewer than eight draw buffers";
                return false;
            }
            Target target;
            TargetDesc desc;
            desc.width = 32;
            desc.height = 32;
            desc.colors = std::vector<GLenum>(8, glc::RGBA8);
            if (!target.create(kit, desc, error)) {
                return false;
            }
            target.bind(kit);
            Api& gl = kit.gl;
            gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            std::string fs = "#version 460 core\nin vec3 v_col;\n";
            for (int i = 0; i < 8; ++i) {
                fs += "layout(location = " + std::to_string(i) + ") out vec4 o" + std::to_string(i) + ";\n";
            }
            fs += "void main() {\n";
            for (int i = 0; i < 8; ++i) {
                fs += "    o" + std::to_string(i) + " = vec4(v_col * " + std::to_string(0.2 + 0.1 * i) + ", 1.0);\n";
            }
            fs += "}\n";
            const GLuint program = kit.program(Flat2d::kVertex, fs.c_str(), error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            const GLenum first[8] = {glc::COLOR_ATTACHMENT0, glc::NONE, glc::COLOR_ATTACHMENT0 + 2, glc::COLOR_ATTACHMENT0 + 3, glc::NONE, glc::COLOR_ATTACHMENT0 + 5, glc::NONE, glc::COLOR_ATTACHMENT0 + 7};
            gl.glDrawBuffers(8, first);
            gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(3, 0.8f, 0.8f)));
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            const GLenum second[8] = {glc::NONE, glc::COLOR_ATTACHMENT0 + 1, glc::NONE, glc::NONE, glc::COLOR_ATTACHMENT0 + 4, glc::NONE, glc::COLOR_ATTACHMENT0 + 6, glc::NONE};
            gl.glDrawBuffers(8, second);
            gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(6, 0.3f, 0.7f, 0.5f)));
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            if (!kit.check("draw buffers", error)) {
                return false;
            }
            for (std::size_t i = 0; i < 8; ++i) {
                Image image;
                if (!target.read_color(kit, i, image, error)) {
                    return false;
                }
                out.add("a" + std::to_string(i), std::move(image));
            }
            return true;
        });

        struct BlitDef {
            const char* name;
            GLenum filter;
            bool flip_x;
            bool flip_y;
        };
        for (const BlitDef& d : {BlitDef{"blit_color_scaled_nearest", glc::NEAREST, false, false}, BlitDef{"blit_color_scaled_linear_flipped_xy", glc::LINEAR, true, true},
                                 BlitDef{"blit_color_region_flip_y_nearest", glc::NEAREST, false, true}}) {
            Cases::single(fbo(d.name).tolerance(Tolerance::filter(4, 0.002)), [d](Kit& kit, CaptureSet& out, std::string& error) {
                Target source;
                TargetDesc sd;
                sd.width = 48;
                sd.height = 32;
                if (!source.create(kit, sd, error)) {
                    return false;
                }
                source.bind(kit);
                Gx::clear(kit, 0.1f, 0.2f, 0.3f);
                Flat flat;
                if (!flat.open(kit, error)) {
                    return false;
                }
                flat.shape(kit, 9, 0.3f, 0.9f);
                Target dest;
                if (!dest.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                gl.glBindFramebuffer(glc::DRAW_FRAMEBUFFER, dest.fbo);
                gl.glClearColor(0.5f, 0.1f, 0.1f, 1);
                gl.glClear(glc::COLOR_BUFFER_BIT);
                gl.glBindFramebuffer(glc::READ_FRAMEBUFFER, source.fbo);
                const int dx0 = d.flip_x ? 120 : 8;
                const int dx1 = d.flip_x ? 8 : 120;
                const int dy0 = d.flip_y ? 100 : 20;
                const int dy1 = d.flip_y ? 20 : 100;
                gl.glBlitFramebuffer(4, 2, 44, 30, dx0, dy0, dx1, dy1, glc::COLOR_BUFFER_BIT, d.filter);
                if (!kit.check("blit", error)) {
                    return false;
                }
                return Gx::shot(kit, dest, out, error);
            });
        }

        Cases::single(fbo("blit_depth_stencil_between_framebuffers"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            Target source;
            TargetDesc desc;
            desc.depth = glc::DEPTH24_STENCIL8;
            if (!source.create(kit, desc, error)) {
                return false;
            }
            source.bind(kit);
            Gx::clear(kit, 0, 0, 0, 1, true, true);
            Flat flat;
            if (!flat.open(kit, error)) {
                return false;
            }
            gl.glEnable(glc::STENCIL_TEST);
            gl.glStencilFunc(glc::ALWAYS, 7, 0xFF);
            gl.glStencilOp(glc::KEEP, glc::KEEP, glc::REPLACE);
            gl.glEnable(glc::DEPTH_TEST);
            flat.shape(kit, 9, 0.3f, 0.8f);
            gl.glDisable(glc::STENCIL_TEST);
            Target dest;
            if (!dest.create(kit, desc, error)) {
                return false;
            }
            dest.bind(kit);
            Gx::clear(kit, 0.2f, 0.1f, 0.1f, 1, true, true);
            gl.glBindFramebuffer(glc::READ_FRAMEBUFFER, source.fbo);
            gl.glBindFramebuffer(glc::DRAW_FRAMEBUFFER, dest.fbo);
            gl.glBlitFramebuffer(0, 0, 128, 128, 0, 0, 128, 128, glc::DEPTH_BUFFER_BIT | glc::STENCIL_BUFFER_BIT, glc::NEAREST);
            gl.glBindFramebuffer(glc::FRAMEBUFFER, dest.fbo);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glDepthFunc(glc::LEQUAL);
            gl.glDepthMask(0);
            gl.glEnable(glc::STENCIL_TEST);
            gl.glStencilFunc(glc::EQUAL, 7, 0xFF);
            gl.glStencilOp(glc::KEEP, glc::KEEP, glc::KEEP);
            const std::vector<V2> quad = {{-1, -1, 0.9f, 0.6f, 0.2f}, {1, -1, 0.2f, 0.8f, 0.9f}, {1, 1, 0.9f, 0.2f, 0.7f}, {-1, -1, 0.9f, 0.6f, 0.2f}, {1, 1, 0.9f, 0.2f, 0.7f}, {-1, 1, 0.3f, 0.9f, 0.3f}};
            gl.glUseProgram(flat.program);
            gl.glBindVertexArray(Flat2d::upload(kit, quad));
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            gl.glDepthMask(1);
            return Gx::shot(kit, dest, out, error) && Gx::shot_depth(kit, dest, out, error) && Gx::shot_stencil(kit, dest, out, error);
        });

        for (const int samples : {2, 4, 8, 16}) {
            Cases::single(fbo("msaa_renderbuffer_resolve_" + std::to_string(samples) + "x").tolerance(Tolerance::tight(1)), [samples](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                TargetDesc desc;
                desc.samples = samples;
                desc.depth = glc::DEPTH24_STENCIL8;
                if (!target.create(kit, desc, error)) {
                    return false;
                }
                target.bind(kit);
                Gx::clear(kit, 0.1f, 0.1f, 0.15f, 1, true, true);
                Flat flat;
                if (!flat.open(kit, error)) {
                    return false;
                }
                flat.shape(kit, 9, 0.2f, 0.9f);
                flat.shape(kit, 5, 0.5f, 0.5f, 0.3f);
                std::vector<V2> spokes;
                for (unsigned k = 0; k < 14; ++k) {
                    const float a = 0.2f + 0.43f * static_cast<float>(k);
                    const float b = a + 0.035f + 0.004f * static_cast<float>(k);
                    spokes.push_back({0.02f * static_cast<float>(k) - 0.13f, 0.0f, 0.9f, 0.9f, 0.9f});
                    spokes.push_back({0.95f * std::cos(a), 0.95f * std::sin(a), 0.9f, 0.3f + 0.05f * static_cast<float>(k), 0.2f});
                    spokes.push_back({0.95f * std::cos(b), 0.95f * std::sin(b), 0.2f, 0.9f, 0.3f + 0.05f * static_cast<float>(k)});
                }
                kit.gl.glBindVertexArray(Flat2d::upload(kit, spokes));
                kit.gl.glDrawArrays(glc::TRIANGLES, 0, static_cast<GLsizei>(spokes.size()));
                return Gx::shot(kit, target, out, error);
            }, [samples](Kit& kit) -> std::string { return kit.integer(glc::MAX_SAMPLES) < samples ? std::string("the sample count is not supported") : std::string(); });
        }

        Cases::single(fbo("clear_buffer_float_int_uint_depth_stencil"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            TargetDesc desc;
            desc.width = 32;
            desc.height = 32;
            desc.colors = {glc::RGBA8, glc::RGBA8UI, glc::RGBA16UI};
            desc.depth = glc::DEPTH24_STENCIL8;
            if (!target.create(kit, desc, error)) {
                return false;
            }
            target.bind(kit);
            Api& gl = kit.gl;
            const GLfloat color[4] = {0.9f, 0.3f, 0.5f, 1.0f};
            gl.glClearBufferfv(glc::COLOR, 0, color);
            const GLuint ucolor[4] = {10, 200, 30, 255};
            gl.glClearBufferuiv(glc::COLOR, 1, ucolor);
            const GLuint wide[4] = {1000, 2000, 3000, 4000};
            gl.glClearBufferuiv(glc::COLOR, 2, wide);
            gl.glClearBufferfi(glc::DEPTH_STENCIL, 0, 0.25f, 9);
            gl.glEnable(glc::SCISSOR_TEST);
            gl.glScissor(8, 8, 16, 16);
            const GLfloat inner[4] = {0.1f, 0.8f, 0.2f, 1.0f};
            gl.glClearBufferfv(glc::COLOR, 0, inner);
            const GLuint uinner[4] = {99, 98, 97, 96};
            gl.glClearBufferuiv(glc::COLOR, 1, uinner);
            const GLfloat depth = 0.75f;
            gl.glClearBufferfv(glc::DEPTH, 0, &depth);
            const GLuint winner[4] = {5000, 6000, 7000, 8000};
            gl.glClearBufferuiv(glc::COLOR, 2, winner);
            gl.glClearBufferfi(glc::DEPTH_STENCIL, 0, 0.6f, 33);
            gl.glDisable(glc::SCISSOR_TEST);
            if (!kit.check("clear buffers", error)) {
                return false;
            }
            for (std::size_t i = 0; i < 3; ++i) {
                Image image;
                if (!target.read_color(kit, i, image, error)) {
                    return false;
                }
                out.add("c" + std::to_string(i), std::move(image));
            }
            return Gx::shot_depth(kit, target, out, error) && Gx::shot_stencil(kit, target, out, error);
        });

        Cases::single(fbo("invalidate_sub_framebuffer_then_redraw"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            TargetDesc desc;
            desc.depth = glc::DEPTH24_STENCIL8;
            if (!target.create(kit, desc, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.1f, 0.1f, 0.15f, 1, true, true);
            Flat flat;
            if (!flat.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            flat.shape(kit, 9, 0.3f, 0.9f);
            const GLenum attachments[2] = {glc::DEPTH_ATTACHMENT, glc::STENCIL_ATTACHMENT};
            gl.glInvalidateFramebuffer(glc::FRAMEBUFFER, 2, attachments);
            gl.glInvalidateSubFramebuffer(glc::FRAMEBUFFER, 1, attachments, 0, 0, 64, 64);
            gl.glClear(glc::DEPTH_BUFFER_BIT | glc::STENCIL_BUFFER_BIT);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glDepthFunc(glc::LESS);
            flat.shape(kit, 5, 0.6f, 0.6f, 0.3f);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(fbo("no_attachments_default_size_image_store"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            GLuint image = 0;
            gl.glGenTextures(1, &image);
            kit.defer([&kit, image] { kit.gl.glDeleteTextures(1, &image); });
            gl.glBindTexture(glc::TEXTURE_2D, image);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 64, 64);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glBindImageTexture(0, image, 0, glc::FALSE_, 0, glc::WRITE_ONLY, glc::RGBA8);
            const GLuint empty = make_fbo(kit);
            static_cast<void>(empty);
            gl.glFramebufferParameteri(glc::FRAMEBUFFER, glc::FRAMEBUFFER_DEFAULT_WIDTH, 64);
            gl.glFramebufferParameteri(glc::FRAMEBUFFER, glc::FRAMEBUFFER_DEFAULT_HEIGHT, 64);
            if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                error = "attachment-less framebuffer incomplete";
                return false;
            }
            gl.glViewport(0, 0, 64, 64);
            const GLuint store = rig.program(kit, "layout(rgba8, binding = 0) writeonly uniform image2D u_image;\nvoid main() { ivec2 p = ivec2(gl_FragCoord.xy); imageStore(u_image, p, vec4(vec2(p) / 64.0, float((p.x / 8 + p.y / 8) % 2), 1.0)); }", error);
            if (store == 0) {
                return false;
            }
            gl.glUseProgram(store);
            gl.glBindVertexArray(rig.vao);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            gl.glMemoryBarrier(glc::TEXTURE_FETCH_BARRIER_BIT);
            rig.target.bind(kit);
            const GLuint show = rig.program(kit, "uniform sampler2D u_tex;\nvoid main() { out_color = texelFetch(u_tex, ivec2(gl_FragCoord.xy * 0.5), 0); }", error);
            if (show == 0) {
                return false;
            }
            kit.u1i(show, "u_tex", 0);
            rig.draw(kit, show);
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::single(fbo("readpixels_float_hdr_into_pbo").tolerance(Tolerance::floats(2e-3f, 2e-3f)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            TargetDesc desc;
            desc.width = 32;
            desc.height = 32;
            desc.colors = {glc::RGBA16F};
            if (!target.create(kit, desc, error)) {
                return false;
            }
            target.bind(kit);
            Api& gl = kit.gl;
            gl.glClearColor(0.5f, 2.5f, 8.0f, 1.0f);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            static const char* fs = "#version 460 core\nin vec3 v_col;\nlayout(location = 0) out vec4 o;\nvoid main() { o = vec4(v_col * 6.0 - 1.0, 0.5); }\n";
            const GLuint program = kit.program(Flat2d::kVertex, fs, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(9, 0.3f, 0.9f)));
            gl.glDrawArrays(glc::TRIANGLES, 0, 9);
            const GLuint pbo = kit.buffer(glc::PIXEL_PACK_BUFFER, nullptr, 32 * 32 * 16, glc::STREAM_READ);
            static_cast<void>(pbo);
            gl.glReadPixels(0, 0, 32, 32, glc::RGBA, glc::FLOAT, nullptr);
            auto* mapped = static_cast<const float*>(gl.glMapBufferRange(glc::PIXEL_PACK_BUFFER, 0, 32 * 32 * 16, glc::MAP_READ_BIT));
            if (mapped == nullptr) {
                error = "mapping the pack buffer failed";
                return false;
            }
            Image image(32, 32, PixelKind::Rgba32Float);
            std::memcpy(image.bytes().data(), mapped, 32 * 32 * 16);
            gl.glUnmapBuffer(glc::PIXEL_PACK_BUFFER);
            gl.glBindBuffer(glc::PIXEL_PACK_BUFFER, 0);
            image.flip_vertical();
            out.add("", std::move(image));
            return kit.check("float pbo readback", error);
        });

        Cases::single(fbo("copy_tex_image_and_sub_image_from_framebuffer"), [](Kit& kit, CaptureSet& out, std::string& error) {
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            Flat flat;
            if (!flat.open(kit, error)) {
                return false;
            }
            flat.shape(kit, 12, 0.2f, 0.9f);
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
            gl.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<GLint>(glc::RGBA8), 32, 32, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
            gl.glCopyTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 20, 20, 32, 32);
            const GLuint u = TexRig::texture(kit, glc::TEXTURE_2D);
            gl.glCopyTexImage2D(glc::TEXTURE_2D, 0, glc::RGBA8, 64, 64, 48, 48, 0);
            gl.glCopyTexSubImage2D(glc::TEXTURE_2D, 0, 8, 8, 0, 0, 16, 16);
            gl.glClearColor(0.2f, 0.0f, 0.2f, 1);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            const GLuint program = rig.program(kit, "uniform sampler2D u_a;\nuniform sampler2D u_b;\nvoid main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = c.x < 0.5 ? texture(u_a, c * 2.0) : texture(u_b, c * 2.0 - vec2(1.0, 0.0)); }", error);
            if (program == 0) {
                return false;
            }
            kit.u1i(program, "u_a", 0);
            kit.u1i(program, "u_b", 1);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_2D, t);
            gl.glActiveTexture(glc::TEXTURE1);
            gl.glBindTexture(glc::TEXTURE_2D, u);
            rig.draw(kit, program);
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::single(fbo("dsa_named_framebuffer_clear_blit"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            GLuint f[2] = {};
            gl.glCreateFramebuffers(2, f);
            GLuint t[2] = {};
            gl.glCreateTextures(glc::TEXTURE_2D, 2, t);
            kit.defer([&kit, f0 = f[0], f1 = f[1], t0 = t[0], t1 = t[1]] {
                kit.gl.glDeleteFramebuffers(1, &f0);
                kit.gl.glDeleteFramebuffers(1, &f1);
                kit.gl.glDeleteTextures(1, &t0);
                kit.gl.glDeleteTextures(1, &t1);
            });
            gl.glTextureStorage2D(t[0], 1, glc::RGBA8, 32, 32);
            gl.glTextureStorage2D(t[1], 1, glc::RGBA8, 128, 128);
            gl.glNamedFramebufferTexture(f[0], glc::COLOR_ATTACHMENT0, t[0], 0);
            gl.glNamedFramebufferTexture(f[1], glc::COLOR_ATTACHMENT0, t[1], 0);
            const GLenum draw = glc::COLOR_ATTACHMENT0;
            gl.glNamedFramebufferDrawBuffers(f[0], 1, &draw);
            gl.glNamedFramebufferDrawBuffers(f[1], 1, &draw);
            const GLfloat a[4] = {0.9f, 0.5f, 0.1f, 1};
            const GLfloat b[4] = {0.1f, 0.2f, 0.6f, 1};
            static_cast<void>(a);
            const auto pattern = TexRig::cells(32, 32, 1, 2);
            gl.glTextureSubImage2D(t[0], 0, 0, 0, 32, 32, glc::RGBA, glc::UNSIGNED_BYTE, pattern.data());
            gl.glClearNamedFramebufferfv(f[1], glc::COLOR, 0, b);
            if (gl.glCheckNamedFramebufferStatus(f[0], glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                error = "named framebuffer incomplete";
                return false;
            }
            gl.glBlitNamedFramebuffer(f[0], f[1], 0, 0, 32, 32, 10, 20, 90, 100, glc::COLOR_BUFFER_BIT, glc::LINEAR);
            gl.glBlitNamedFramebuffer(f[0], f[1], 0, 0, 16, 16, 100, 100, 120, 70, glc::COLOR_BUFFER_BIT, glc::NEAREST);
            gl.glNamedFramebufferReadBuffer(f[1], glc::COLOR_ATTACHMENT0);
            Image image;
            if (!kit.read_color(f[1], glc::COLOR_ATTACHMENT0, glc::RGBA8, 128, 128, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        });

        Cases::single(fbo("completeness_status_matrix"), [](Kit& kit, CaptureSet& out, std::string& error) {
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            std::vector<GLenum> statuses;
            {
                const GLuint f = make_fbo(kit);
                static_cast<void>(f);
                statuses.push_back(gl.glCheckFramebufferStatus(glc::FRAMEBUFFER));
                const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 16, 16);
                gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, t, 0);
                statuses.push_back(gl.glCheckFramebufferStatus(glc::FRAMEBUFFER));
                const GLuint d = TexRig::texture(kit, glc::TEXTURE_2D);
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::DEPTH_COMPONENT24, 32, 32);
                gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, glc::TEXTURE_2D, d, 0);
                statuses.push_back(gl.glCheckFramebufferStatus(glc::FRAMEBUFFER));
                gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, glc::TEXTURE_2D, 0, 0);
                gl.glDrawBuffer(glc::COLOR_ATTACHMENT1);
                statuses.push_back(gl.glCheckFramebufferStatus(glc::FRAMEBUFFER));
                gl.glDrawBuffer(glc::COLOR_ATTACHMENT0);
                GLuint rb = 0;
                gl.glGenRenderbuffers(1, &rb);
                kit.defer([&kit, rb] { kit.gl.glDeleteRenderbuffers(1, &rb); });
                gl.glBindRenderbuffer(glc::RENDERBUFFER, rb);
                gl.glRenderbufferStorageMultisample(glc::RENDERBUFFER, 4, glc::RGBA8, 16, 16);
                gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT1, glc::RENDERBUFFER, rb);
                statuses.push_back(gl.glCheckFramebufferStatus(glc::FRAMEBUFFER));
                gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT1, glc::RENDERBUFFER, 0);
                statuses.push_back(gl.glCheckFramebufferStatus(glc::FRAMEBUFFER));
            }
            while (gl.glGetError() != glc::NO_ERROR_) {
            }
            rig.target.bind(kit);
            std::string table = "const uint s[6] = uint[6](";
            for (std::size_t i = 0; i < 6; ++i) {
                table += std::to_string(statuses[i]) + "u" + (i + 1 < 6 ? "," : "");
            }
            table += ");\n";
            const GLuint program = rig.program(kit, table + "void main() { vec2 c = gl_FragCoord.xy / 128.0; uint v = s[int(c.x * 3.0) + 3 * int(c.y * 2.0)]; "
                                                          "out_color = vec4(float(v & 255u) / 255.0, float((v >> 4) & 255u) / 255.0, float(v == 36053u), 1.0); }", error);
            if (program == 0) {
                return false;
            }
            rig.draw(kit, program);
            return Gx::shot(kit, rig.target, out, error);
        });
    }
} kBatch20;

}  // namespace

}  // namespace lsw::render::gl
