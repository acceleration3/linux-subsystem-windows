#include "render/gl/texrig.hpp"

#include <cstring>

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

bool show2d(Kit& kit, TexRig& rig, GLuint t, std::string& error, const std::string& body = "void main() { out_color = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 128.0 * vec2(textureSize(u_tex, 0))), 0); }",
            const char* uniforms = "uniform sampler2D u_tex;", GLenum target = glc::TEXTURE_2D) {
    const GLuint program = rig.program(kit, body, error, uniforms);
    if (program == 0) {
        return false;
    }
    kit.gl.glActiveTexture(glc::TEXTURE0);
    kit.gl.glBindTexture(target, t);
    kit.u1i(program, "u_tex", 0);
    rig.draw(kit, program);
    return true;
}

const struct Batch18 {
    Batch18() {
        tex_case("store_unpack_row_length_skip", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const auto big = TexRig::cells(32, 32, 1, 2);
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 16, 16);
            gl.glPixelStorei(glc::UNPACK_ROW_LENGTH, 32);
            gl.glPixelStorei(glc::UNPACK_SKIP_PIXELS, 5);
            gl.glPixelStorei(glc::UNPACK_SKIP_ROWS, 3);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, big.data());
            gl.glPixelStorei(glc::UNPACK_ROW_LENGTH, 0);
            gl.glPixelStorei(glc::UNPACK_SKIP_PIXELS, 0);
            gl.glPixelStorei(glc::UNPACK_SKIP_ROWS, 0);
            return show2d(kit, rig, t, error);
        });

        for (const int alignment : {1, 2, 4, 8}) {
            tex_case("store_unpack_alignment_" + std::to_string(alignment), [alignment](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const unsigned width = 10 + static_cast<unsigned>(alignment);
                const unsigned height = 9;
                const unsigned stride = ((width * 3 + static_cast<unsigned>(alignment) - 1) / static_cast<unsigned>(alignment)) * static_cast<unsigned>(alignment);
                std::vector<std::uint8_t> data(static_cast<std::size_t>(stride) * height, 0xEE);
                for (unsigned y = 0; y < height; ++y) {
                    for (unsigned x = 0; x < width; ++x) {
                        data[y * stride + x * 3] = static_cast<std::uint8_t>(20 + x * 17);
                        data[y * stride + x * 3 + 1] = static_cast<std::uint8_t>(30 + y * 25);
                        data[y * stride + x * 3 + 2] = static_cast<std::uint8_t>((x + y) % 2 ? 220 : 60);
                    }
                }
                const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
                gl.glPixelStorei(glc::UNPACK_ALIGNMENT, alignment);
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGB8, width, height);
                gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, width, height, glc::RGB, glc::UNSIGNED_BYTE, data.data());
                gl.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
                return show2d(kit, rig, t, error);
            });
        }

        tex_case("store_unpack_image_height_skip_images_3d", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const unsigned row = 10;
            const unsigned image_height = 12;
            std::vector<std::uint8_t> data(static_cast<std::size_t>(row) * image_height * 4 * 4, 0);
            for (unsigned z = 0; z < 4; ++z) {
                for (unsigned y = 0; y < image_height; ++y) {
                    for (unsigned x = 0; x < row; ++x) {
                        std::uint8_t* p = &data[((static_cast<std::size_t>(z) * image_height + y) * row + x) * 4];
                        p[0] = static_cast<std::uint8_t>(z * 70);
                        p[1] = static_cast<std::uint8_t>(x * 25);
                        p[2] = static_cast<std::uint8_t>(y * 20);
                        p[3] = 255;
                    }
                }
            }
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_3D);
            gl.glTexStorage3D(glc::TEXTURE_3D, 1, glc::RGBA8, 8, 8, 3);
            gl.glPixelStorei(glc::UNPACK_ROW_LENGTH, static_cast<GLint>(row));
            gl.glPixelStorei(glc::UNPACK_IMAGE_HEIGHT, static_cast<GLint>(image_height));
            gl.glPixelStorei(glc::UNPACK_SKIP_IMAGES, 1);
            gl.glPixelStorei(glc::UNPACK_SKIP_PIXELS, 1);
            gl.glPixelStorei(glc::UNPACK_SKIP_ROWS, 2);
            gl.glTexSubImage3D(glc::TEXTURE_3D, 0, 0, 0, 0, 8, 8, 3, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            gl.glPixelStorei(glc::UNPACK_ROW_LENGTH, 0);
            gl.glPixelStorei(glc::UNPACK_IMAGE_HEIGHT, 0);
            gl.glPixelStorei(glc::UNPACK_SKIP_IMAGES, 0);
            gl.glPixelStorei(glc::UNPACK_SKIP_PIXELS, 0);
            gl.glPixelStorei(glc::UNPACK_SKIP_ROWS, 0);
            return show2d(kit, rig, t,
                          error,
                          "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = texelFetch(u_tex, ivec3(fract(c * vec2(3.0, 1.0)) * 8.0 * vec2(1.0, 1.0), int(c.x * 3.0)), 0); }",
                          "uniform sampler3D u_tex;", glc::TEXTURE_3D);
        });

        tex_case("store_unpack_swap_bytes_16bit", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            std::vector<std::uint16_t> data(16 * 16);
            for (unsigned i = 0; i < data.size(); ++i) {
                const std::uint16_t v = static_cast<std::uint16_t>(((i % 16) * 4000 + (i / 16) * 1000) & 0xFFFF);
                data[i] = static_cast<std::uint16_t>((v >> 8) | (v << 8));
            }
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::R16, 16, 16);
            gl.glPixelStorei(glc::UNPACK_SWAP_BYTES, 1);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 16, 16, glc::RED, glc::UNSIGNED_SHORT, data.data());
            gl.glPixelStorei(glc::UNPACK_SWAP_BYTES, 0);
            return show2d(kit, rig, t, error, "void main() { float v = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 8.0), 0).r; out_color = vec4(v, fract(v * 4.0), 1.0 - v, 1.0); }");
        });

        tex_case("store_pack_row_length_alignment_readpixels", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint flat = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (flat == 0) {
                return false;
            }
            kit.u1f(flat, "u_size", 1.0f);
            gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(9, 0.3f, 0.85f)));
            gl.glDrawArrays(glc::TRIANGLES, 0, 9);
            const unsigned row_pixels = 40;
            std::vector<std::uint8_t> data(static_cast<std::size_t>(row_pixels) * 3 * 20 * 2, 0x55);
            gl.glPixelStorei(glc::PACK_ALIGNMENT, 4);
            gl.glPixelStorei(glc::PACK_ROW_LENGTH, static_cast<GLint>(row_pixels));
            gl.glPixelStorei(glc::PACK_SKIP_PIXELS, 3);
            gl.glPixelStorei(glc::PACK_SKIP_ROWS, 2);
            gl.glReadPixels(40, 40, 32, 16, glc::RGB, glc::UNSIGNED_BYTE, data.data());
            gl.glPixelStorei(glc::PACK_ALIGNMENT, 1);
            gl.glPixelStorei(glc::PACK_ROW_LENGTH, 0);
            gl.glPixelStorei(glc::PACK_SKIP_PIXELS, 0);
            gl.glPixelStorei(glc::PACK_SKIP_ROWS, 0);
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGB8, 32, 16);
            gl.glPixelStorei(glc::UNPACK_ALIGNMENT, 4);
            gl.glPixelStorei(glc::UNPACK_ROW_LENGTH, static_cast<GLint>(row_pixels));
            gl.glPixelStorei(glc::UNPACK_SKIP_PIXELS, 3);
            gl.glPixelStorei(glc::UNPACK_SKIP_ROWS, 2);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 32, 16, glc::RGB, glc::UNSIGNED_BYTE, data.data());
            gl.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
            gl.glPixelStorei(glc::UNPACK_ROW_LENGTH, 0);
            gl.glPixelStorei(glc::UNPACK_SKIP_PIXELS, 0);
            gl.glPixelStorei(glc::UNPACK_SKIP_ROWS, 0);
            gl.glClearColor(0, 0, 0, 1);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            return show2d(kit, rig, t, error);
        });

        tex_case("pbo_unpack_with_offset", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const auto data = TexRig::cells(16, 16);
            std::vector<std::uint8_t> padded(256, 0xAB);
            padded.insert(padded.end(), data.begin(), data.end());
            kit.buffer(glc::PIXEL_UNPACK_BUFFER, padded.data(), padded.size(), glc::STATIC_DRAW);
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 16, 16);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(256)));
            gl.glBindBuffer(glc::PIXEL_UNPACK_BUFFER, 0);
            return show2d(kit, rig, t, error);
        });

        tex_case("pbo_pack_readback_reupload", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLuint flat = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (flat == 0) {
                return false;
            }
            kit.u1f(flat, "u_size", 1.0f);
            gl.glBindVertexArray(Flat2d::upload(kit, Flat2d::ring(12, 0.3f, 0.9f)));
            gl.glDrawArrays(glc::TRIANGLES, 0, 12);
            const GLuint pbo = kit.buffer(glc::PIXEL_PACK_BUFFER, nullptr, 128 * 128 * 4, glc::STREAM_READ);
            gl.glReadPixels(0, 0, 128, 128, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
            gl.glBindBuffer(glc::PIXEL_PACK_BUFFER, 0);
            gl.glBindBuffer(glc::PIXEL_UNPACK_BUFFER, pbo);
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 128, 128);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 128, 128, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
            gl.glBindBuffer(glc::PIXEL_UNPACK_BUFFER, 0);
            gl.glClearColor(0.3f, 0.0f, 0.3f, 1);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            return show2d(kit, rig, t, error,
                          "void main() { ivec2 p = ivec2(gl_FragCoord.xy); out_color = texelFetch(u_tex, ivec2(p.y, p.x), 0); }");
        });

        Cases::multi(tex("pbo_streaming_upload_frames").frames(8, {0, 3, 7}).tolerance(Tolerance::tight(1)), [] {
            struct State {
                TexRig rig;
                GLuint pbo = 0;
                GLuint texture = 0;
                GLuint program = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                if (!s->rig.open(kit, error)) {
                    return false;
                }
                s->pbo = kit.buffer(glc::PIXEL_UNPACK_BUFFER, nullptr, 3 * 16 * 16 * 4, glc::STREAM_DRAW);
                kit.gl.glBindBuffer(glc::PIXEL_UNPACK_BUFFER, 0);
                s->texture = TexRig::texture(kit, glc::TEXTURE_2D);
                kit.gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 16, 16);
                s->program = s->rig.program(kit, "void main() { out_color = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 8.0), 0); }", error, "uniform sampler2D u_tex;");
                return s->program != 0;
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                const unsigned slot = frame % 3;
                gl.glBindBuffer(glc::PIXEL_UNPACK_BUFFER, s->pbo);
                auto* mapped = static_cast<std::uint8_t*>(gl.glMapBufferRange(glc::PIXEL_UNPACK_BUFFER, slot * 1024, 1024, glc::MAP_WRITE_BIT | glc::MAP_INVALIDATE_RANGE_BIT));
                if (mapped == nullptr) {
                    error = "glMapBufferRange returned null";
                    return false;
                }
                for (unsigned i = 0; i < 256; ++i) {
                    mapped[i * 4] = static_cast<std::uint8_t>((i % 16) * 16 + frame * 20);
                    mapped[i * 4 + 1] = static_cast<std::uint8_t>((i / 16) * 16);
                    mapped[i * 4 + 2] = static_cast<std::uint8_t>(255 - frame * 30);
                    mapped[i * 4 + 3] = 255;
                }
                gl.glUnmapBuffer(glc::PIXEL_UNPACK_BUFFER);
                gl.glBindTexture(glc::TEXTURE_2D, s->texture);
                gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(slot * 1024)));
                gl.glBindBuffer(glc::PIXEL_UNPACK_BUFFER, 0);
                s->rig.target.bind(kit);
                kit.u1i(s->program, "u_tex", 0);
                s->rig.draw(kit, s->program);
                return kit.check("pbo stream", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->rig.target, out, error); };
            return fns;
        });

        tex_case("mipmap_generate_formats", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const GLenum internals[3] = {glc::RGBA8, glc::RGBA16F, glc::SRGB8_ALPHA8};
            const auto data = TexRig::cells(32, 32, 1, 2);
            GLuint ids[3];
            for (int i = 0; i < 3; ++i) {
                ids[i] = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST_MIPMAP_NEAREST);
                gl.glTexStorage2D(glc::TEXTURE_2D, 6, internals[i], 32, 32);
                gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 32, 32, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
                gl.glGenerateMipmap(glc::TEXTURE_2D);
            }
            const GLuint program = rig.program(kit,
                                               "void main() { vec2 c = gl_FragCoord.xy / 128.0; int i = int(c.x * 3.0); float lod = c.y * 3.0; vec4 v = i == 0 ? textureLod(u_a, vec2(fract(c.x * 3.0), c.y), lod) : (i == 1 ? textureLod(u_b, vec2(fract(c.x * 3.0), c.y), lod) : textureLod(u_c, vec2(fract(c.x * 3.0), c.y), lod)); out_color = v; }",
                                               error, "uniform sampler2D u_a;\nuniform sampler2D u_b;\nuniform sampler2D u_c;");
            if (program == 0) {
                return false;
            }
            for (int i = 0; i < 3; ++i) {
                gl.glActiveTexture(glc::TEXTURE0 + static_cast<GLenum>(i));
                gl.glBindTexture(glc::TEXTURE_2D, ids[i]);
            }
            kit.u1i(program, "u_a", 0);
            kit.u1i(program, "u_b", 1);
            kit.u1i(program, "u_c", 2);
            rig.draw(kit, program);
            return true;
        }, Tolerance::filter(4, 0.001));

        tex_case("mipmap_base_level_max_level", [](Kit& kit, TexRig& rig, std::string& error) {
            Api& gl = kit.gl;
            const auto data = TexRig::cells(32, 32, 1, 2);
            const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::LINEAR_MIPMAP_LINEAR);
            gl.glTexStorage2D(glc::TEXTURE_2D, 6, glc::RGBA8, 32, 32);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 32, 32, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            gl.glGenerateMipmap(glc::TEXTURE_2D);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_BASE_LEVEL, 1);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAX_LEVEL, 3);
            return show2d(kit, rig, t, error, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = texture(u_tex, c * (1.0 + 4.0 * c.y)); out_color.b += float(textureQueryLevels(u_tex)) * 0.1; }");
        }, Tolerance::filter(4, 0.001));

        struct LodDef {
            const char* name;
            float bias;
            float min_lod;
            float max_lod;
        };
        for (const LodDef& d : {LodDef{"sampler_lod_bias", 2.0f, -1000.0f, 1000.0f}, LodDef{"sampler_min_lod", 0.0f, 2.5f, 1000.0f}, LodDef{"sampler_max_lod", 0.0f, -1000.0f, 1.0f}}) {
            tex_case(std::string("mipmap_") + d.name, [d](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const auto data = TexRig::cells(64, 64, 1, 4);
                const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::LINEAR_MIPMAP_LINEAR);
                gl.glTexStorage2D(glc::TEXTURE_2D, 7, glc::RGBA8, 64, 64);
                gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 64, 64, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
                gl.glGenerateMipmap(glc::TEXTURE_2D);
                GLuint sampler = 0;
                gl.glGenSamplers(1, &sampler);
                kit.defer([&kit, sampler] { kit.gl.glDeleteSamplers(1, &sampler); });
                gl.glSamplerParameteri(sampler, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR_MIPMAP_LINEAR));
                gl.glSamplerParameteri(sampler, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
                gl.glSamplerParameterf(sampler, glc::TEXTURE_LOD_BIAS, d.bias);
                gl.glSamplerParameterf(sampler, glc::TEXTURE_MIN_LOD, d.min_lod);
                gl.glSamplerParameterf(sampler, glc::TEXTURE_MAX_LOD, d.max_lod);
                gl.glBindSampler(0, sampler);
                const bool ok = show2d(kit, rig, t, error, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = texture(u_tex, c * (0.5 + 3.5 * c.x)); }");
                gl.glBindSampler(0, 0);
                return ok;
            }, Tolerance::filter(4, 0.001));
        }

        struct SwizzleDef {
            const char* name;
            GLint rgba[4];
        };
        for (const SwizzleDef& d : {SwizzleDef{"reverse", {static_cast<GLint>(glc::ALPHA), static_cast<GLint>(glc::BLUE), static_cast<GLint>(glc::GREEN), static_cast<GLint>(glc::RED)}},
                                    SwizzleDef{"zero_one", {static_cast<GLint>(glc::GREEN), static_cast<GLint>(glc::ZERO), static_cast<GLint>(glc::RED), static_cast<GLint>(glc::ONE)}},
                                    SwizzleDef{"broadcast_red", {static_cast<GLint>(glc::RED), static_cast<GLint>(glc::RED), static_cast<GLint>(glc::RED), static_cast<GLint>(glc::ONE)}}}) {
            tex_case(std::string("swizzle_") + d.name, [d](Kit& kit, TexRig& rig, std::string& error) {
                Api& gl = kit.gl;
                const auto data = TexRig::cells(16, 16);
                const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D);
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 16, 16);
                gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_SWIZZLE_R, d.rgba[0]);
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_SWIZZLE_G, d.rgba[1]);
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_SWIZZLE_B, d.rgba[2]);
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_SWIZZLE_A, d.rgba[3]);
                return show2d(kit, rig, t, error, "void main() { vec4 v = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 8.0), 0); out_color = vec4(v.rgb, 1.0) * (gl_FragCoord.y < 64.0 ? 1.0 : 0.0) + vec4(vec3(v.a), 1.0) * (gl_FragCoord.y < 64.0 ? 0.0 : 1.0); }");
            });
        }
    }
} kBatch18;

}  // namespace

}  // namespace lsw::render::gl
