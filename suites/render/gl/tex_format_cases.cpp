#include "render/gl/texrig.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec tex(const std::string& name) { return CaseSpec("tex", name).min_version(4, 6); }

using Bytes = std::vector<std::uint8_t>;

enum class Cls { Float, Int, Uint };

template <class T>
void put(Bytes& out, T value) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

std::uint32_t texel_hash(unsigned x, unsigned y, unsigned c, unsigned variant) {
    std::uint32_t h = x * 0x9E3779B1u ^ (y + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 1) * 0xC2B2AE35u ^ (variant + 3) * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float unit_value(std::uint32_t h) { return static_cast<float>(h >> 8) / 16777216.0f; }

float spread_value(std::uint32_t h) {
    return std::ldexp(1.0f + static_cast<float>(h & 0x7FFFFF) / 8388608.0f, static_cast<int>((h >> 23) % 12) - 9);
}

std::uint32_t small_float(float v, unsigned mantissa_bits) {
    if (v <= 0.0f) {
        return 0;
    }
    int exponent = 0;
    const float m = std::frexp(v, &exponent);
    exponent += 14;
    if (exponent <= 0) {
        return 0;
    }
    if (exponent > 30) {
        exponent = 30;
    }
    const std::uint32_t mant = static_cast<std::uint32_t>((m * 2.0f - 1.0f) * static_cast<float>(1u << mantissa_bits));
    return (static_cast<std::uint32_t>(exponent) << mantissa_bits) | (mant & ((1u << mantissa_bits) - 1));
}

std::uint32_t shared_exponent(const float* v) {
    const float maxc = std::max(v[0], std::max(v[1], v[2]));
    int exponent = std::max(-16, static_cast<int>(std::floor(std::log2(std::max(maxc, 1e-8f))))) + 1 + 15;
    float denom = std::pow(2.0f, static_cast<float>(exponent - 15 - 9));
    if (static_cast<int>(std::floor(maxc / denom + 0.5f)) == 512) {
        ++exponent;
        denom *= 2.0f;
    }
    const auto m = [&](float c) { return static_cast<std::uint32_t>(std::floor(c / denom + 0.5f)) & 0x1FF; };
    return m(v[0]) | (m(v[1]) << 9) | (m(v[2]) << 18) | (static_cast<std::uint32_t>(exponent) << 27);
}

std::uint32_t pack_fields(const float* v, const unsigned* bits, unsigned count, bool reversed) {
    std::uint32_t out = 0;
    unsigned shift = 0;
    for (unsigned k = 0; k < count; ++k) {
        const unsigned i = reversed ? k : count - 1 - k;
        const std::uint32_t maxv = (1u << bits[i]) - 1;
        const std::uint32_t q = static_cast<std::uint32_t>(std::lround(std::min(std::max(v[i], 0.0f), 1.0f) * static_cast<float>(maxv)));
        out |= q << shift;
        shift += bits[i];
    }
    return out;
}

struct Fmt {
    const char* name;
    GLenum internal;
    GLenum format;
    GLenum type;
    Cls cls;
    unsigned channels;
    bool bgra;
    float scale;
    float bias;
    unsigned variant = 0;
    bool unit = false;
};

unsigned format_channels(GLenum format) {
    switch (format) {
        case glc::RED: case glc::RED_INTEGER: return 1;
        case glc::RG: case glc::RG_INTEGER: return 2;
        case glc::RGB: case glc::RGB_INTEGER: case glc::BGR: return 3;
        default: return 4;
    }
}

Bytes encode_texel(const Fmt& f, unsigned x, unsigned y) {
    Bytes out;
    std::uint32_t h[4];
    for (unsigned i = 0; i < 4; ++i) {
        h[i] = texel_hash(x, y, i, f.variant);
    }
    if (f.bgra) {
        std::swap(h[0], h[2]);
    }
    const unsigned n = f.channels;
    const auto value = [&](unsigned i) { return f.unit ? unit_value(h[i]) : spread_value(h[i]); };
    switch (f.type) {
        case glc::UNSIGNED_BYTE: for (unsigned i = 0; i < n; ++i) put<std::uint8_t>(out, static_cast<std::uint8_t>(h[i] >> 24)); break;
        case glc::BYTE: for (unsigned i = 0; i < n; ++i) put<std::int8_t>(out, static_cast<std::int8_t>(static_cast<int>((h[i] >> 24) % 255) - 127)); break;
        case glc::UNSIGNED_SHORT: for (unsigned i = 0; i < n; ++i) put<std::uint16_t>(out, static_cast<std::uint16_t>(f.unit ? h[i] >> 24 : h[i] >> 16)); break;
        case glc::SHORT: for (unsigned i = 0; i < n; ++i) put<std::int16_t>(out, static_cast<std::int16_t>(static_cast<int>((h[i] >> 16) % 65535) - 32767)); break;
        case glc::UNSIGNED_INT: for (unsigned i = 0; i < n; ++i) put<std::uint32_t>(out, f.unit ? h[i] >> 24 : h[i]); break;
        case glc::INT: for (unsigned i = 0; i < n; ++i) put<std::int32_t>(out, h[i] == 0x80000000u ? -2147483647 : static_cast<std::int32_t>(h[i])); break;
        case glc::HALF_FLOAT: for (unsigned i = 0; i < n; ++i) put<std::uint16_t>(out, PixelInfo::float_to_half(f.unit ? static_cast<float>(h[i] >> 21) / 2048.0f : value(i))); break;
        case glc::FLOAT: for (unsigned i = 0; i < n; ++i) put<float>(out, value(i)); break;
        case glc::UNSIGNED_SHORT_5_6_5: { const unsigned b[3] = {5, 6, 5}; float c[4] = {unit_value(h[0]), unit_value(h[1]), unit_value(h[2]), 0}; put<std::uint16_t>(out, static_cast<std::uint16_t>(pack_fields(c, b, 3, false))); break; }
        case glc::UNSIGNED_SHORT_5_6_5_REV: { const unsigned b[3] = {5, 6, 5}; float c[4] = {unit_value(h[0]), unit_value(h[1]), unit_value(h[2]), 0}; put<std::uint16_t>(out, static_cast<std::uint16_t>(pack_fields(c, b, 3, true))); break; }
        case glc::UNSIGNED_SHORT_4_4_4_4: { const unsigned b[4] = {4, 4, 4, 4}; float c[4] = {unit_value(h[0]), unit_value(h[1]), unit_value(h[2]), unit_value(h[3])}; put<std::uint16_t>(out, static_cast<std::uint16_t>(pack_fields(c, b, 4, false))); break; }
        case glc::UNSIGNED_SHORT_4_4_4_4_REV: { const unsigned b[4] = {4, 4, 4, 4}; float c[4] = {unit_value(h[0]), unit_value(h[1]), unit_value(h[2]), unit_value(h[3])}; put<std::uint16_t>(out, static_cast<std::uint16_t>(pack_fields(c, b, 4, true))); break; }
        case glc::UNSIGNED_SHORT_5_5_5_1: { const unsigned b[4] = {5, 5, 5, 1}; float c[4] = {unit_value(h[0]), unit_value(h[1]), unit_value(h[2]), unit_value(h[3])}; put<std::uint16_t>(out, static_cast<std::uint16_t>(pack_fields(c, b, 4, false))); break; }
        case glc::UNSIGNED_SHORT_1_5_5_5_REV: { const unsigned b[4] = {5, 5, 5, 1}; float c[4] = {unit_value(h[0]), unit_value(h[1]), unit_value(h[2]), unit_value(h[3])}; put<std::uint16_t>(out, static_cast<std::uint16_t>(pack_fields(c, b, 4, true))); break; }
        case glc::UNSIGNED_INT_8_8_8_8: put<std::uint32_t>(out, h[0]); break;
        case glc::UNSIGNED_INT_8_8_8_8_REV: put<std::uint32_t>(out, h[0]); break;
        case glc::UNSIGNED_INT_10_10_10_2: put<std::uint32_t>(out, h[0]); break;
        case glc::UNSIGNED_INT_2_10_10_10_REV: put<std::uint32_t>(out, h[0]); break;
        case glc::UNSIGNED_INT_10F_11F_11F_REV: put<std::uint32_t>(out, small_float(spread_value(h[0]), 6) | (small_float(spread_value(h[1]), 6) << 11) | (small_float(spread_value(h[2]), 5) << 22)); break;
        case glc::UNSIGNED_INT_5_9_9_9_REV: { const float c[3] = {spread_value(h[0]), spread_value(h[1]), spread_value(h[2])}; put<std::uint32_t>(out, shared_exponent(c)); break; }
        default: break;
    }
    return out;
}

Bytes build_image(const Fmt& f, unsigned size = 16) {
    Bytes out;
    for (unsigned y = 0; y < size; ++y) {
        for (unsigned x = 0; x < size; ++x) {
            const Bytes texel = encode_texel(f, x, y);
            out.insert(out.end(), texel.begin(), texel.end());
        }
    }
    return out;
}

std::string display_source(Cls cls) {
    const char* sampler = cls == Cls::Float ? "sampler2D" : (cls == Cls::Int ? "isampler2D" : "usampler2D");
    return std::string("uniform ") + sampler + " u_tex;\n"
           "void main() { out_color = " + (cls == Cls::Float ? "vec4" : "uvec4") + "(texelFetch(u_tex, ivec2(gl_FragCoord.xy / 8.0), 0)); }\n";
}

std::string scaled_display_source(Cls cls, bool alpha_row) {
    const char* sampler = cls == Cls::Float ? "sampler2D" : (cls == Cls::Int ? "isampler2D" : "usampler2D");
    return std::string("uniform ") + sampler + " u_tex;\nuniform float u_scale;\nuniform float u_bias;\n"
           "void main() {\n"
           "    vec2 c = gl_FragCoord.xy / 128.0;\n"
           "    vec4 t = vec4(texelFetch(u_tex, ivec2(c * 16.0), 0));\n"
           "    out_color = vec4(" + (alpha_row ? "c.y < 0.5 ? t.rgb : vec3(t.a)" : "t.rgb") + " * u_scale + u_bias, 1.0);\n"
           "}\n";
}

GLenum display_target(Cls cls) { return cls == Cls::Float ? glc::RGBA32F : glc::RGBA32UI; }

Tolerance display_tolerance(Cls cls) { return cls == Cls::Float ? Tolerance::floats(1e-6f, 1e-6f) : Tolerance::exact(); }

std::string supported(Kit& kit, GLenum internal) {
    GLint ok = 0;
    if (kit.gl.glGetInternalformativ == nullptr) {
        return "glGetInternalformativ is not available";
    }
    kit.gl.glGetInternalformativ(glc::TEXTURE_2D, internal, glc::INTERNALFORMAT_SUPPORTED, 1, &ok);
    while (kit.gl.glGetError() != glc::NO_ERROR_) {
    }
    return ok != 0 ? std::string() : "the internal format is not supported";
}

bool upload_and_show(Kit& kit, TexRig& rig, const Fmt& f, const Bytes& data, std::string& error, bool storage = true) {
    Api& gl = kit.gl;
    const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
    if (storage) {
        gl.glTexStorage2D(glc::TEXTURE_2D, 1, f.internal, 16, 16);
        gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 16, 16, f.format, f.type, data.data());
    } else {
        gl.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<GLint>(f.internal), 16, 16, 0, f.format, f.type, data.data());
    }
    if (!kit.check("texture upload", error)) {
        return false;
    }
    const GLuint program = rig.program(kit, display_source(f.cls), error, "", f.cls == Cls::Float ? "vec4" : "uvec4");
    if (program == 0) {
        return false;
    }
    gl.glActiveTexture(glc::TEXTURE0);
    gl.glBindTexture(glc::TEXTURE_2D, t);
    kit.u1i(program, "u_tex", 0);
    rig.draw(kit, program);
    return true;
}

struct CompressedFmt {
    const char* name;
    GLenum internal;
    unsigned block_bytes;
    const char* extension;
    float scale;
};

const struct Batch17 {
    Batch17() {
        const float k8 = 1.0f / 255.0f;
        unsigned variant_counter = 0;
        const Fmt formats[] = {
            {"r8", glc::R8, glc::RED, glc::UNSIGNED_BYTE, Cls::Float, 1, false, 1, 0},
            {"rg8", glc::RG8, glc::RG, glc::UNSIGNED_BYTE, Cls::Float, 2, false, 1, 0},
            {"rgb8", glc::RGB8, glc::RGB, glc::UNSIGNED_BYTE, Cls::Float, 3, false, 1, 0},
            {"rgba8", glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, Cls::Float, 4, false, 1, 0},
            {"srgb8", glc::SRGB8, glc::RGB, glc::UNSIGNED_BYTE, Cls::Float, 3, false, 1, 0},
            {"srgb8_alpha8", glc::SRGB8_ALPHA8, glc::RGBA, glc::UNSIGNED_BYTE, Cls::Float, 4, false, 1, 0},
            {"r8_snorm", glc::R8_SNORM, glc::RED, glc::BYTE, Cls::Float, 1, false, 0.5f, 0.5f},
            {"rg8_snorm", glc::RG8_SNORM, glc::RG, glc::BYTE, Cls::Float, 2, false, 0.5f, 0.5f},
            {"rgba8_snorm", glc::RGBA8_SNORM, glc::RGBA, glc::BYTE, Cls::Float, 4, false, 0.5f, 0.5f},
            {"r16", glc::R16, glc::RED, glc::UNSIGNED_SHORT, Cls::Float, 1, false, 1, 0},
            {"rg16", glc::RG16, glc::RG, glc::UNSIGNED_SHORT, Cls::Float, 2, false, 1, 0},
            {"rgba16", glc::RGBA16, glc::RGBA, glc::UNSIGNED_SHORT, Cls::Float, 4, false, 1, 0},
            {"r16_snorm", glc::R16_SNORM, glc::RED, glc::SHORT, Cls::Float, 1, false, 0.5f, 0.5f},
            {"rgba16_snorm", glc::RGBA16_SNORM, glc::RGBA, glc::SHORT, Cls::Float, 4, false, 0.5f, 0.5f},
            {"r16f", glc::R16F, glc::RED, glc::HALF_FLOAT, Cls::Float, 1, false, 1, 0},
            {"rg16f", glc::RG16F, glc::RG, glc::HALF_FLOAT, Cls::Float, 2, false, 1, 0},
            {"rgba16f", glc::RGBA16F, glc::RGBA, glc::HALF_FLOAT, Cls::Float, 4, false, 1, 0},
            {"r32f", glc::R32F, glc::RED, glc::FLOAT, Cls::Float, 1, false, 1, 0},
            {"rg32f", glc::RG32F, glc::RG, glc::FLOAT, Cls::Float, 2, false, 1, 0},
            {"rgba32f", glc::RGBA32F, glc::RGBA, glc::FLOAT, Cls::Float, 4, false, 1, 0},
            {"r8i", glc::R8I, glc::RED_INTEGER, glc::BYTE, Cls::Int, 1, false, 1.0f / 127.0f, 0},
            {"r8ui", glc::R8UI, glc::RED_INTEGER, glc::UNSIGNED_BYTE, Cls::Uint, 1, false, k8, 0},
            {"rg16i", glc::RG16I, glc::RG_INTEGER, glc::SHORT, Cls::Int, 2, false, 1.0f / 127.0f, 0},
            {"rg16ui", glc::RG16UI, glc::RG_INTEGER, glc::UNSIGNED_SHORT, Cls::Uint, 2, false, k8, 0},
            {"rgba8ui", glc::RGBA8UI, glc::RGBA_INTEGER, glc::UNSIGNED_BYTE, Cls::Uint, 4, false, k8, 0},
            {"rgba16i", glc::RGBA16I, glc::RGBA_INTEGER, glc::SHORT, Cls::Int, 4, false, 1.0f / 127.0f, 0},
            {"r32i", glc::R32I, glc::RED_INTEGER, glc::INT, Cls::Int, 1, false, 1.0f / 127.0f, 0},
            {"r32ui", glc::R32UI, glc::RED_INTEGER, glc::UNSIGNED_INT, Cls::Uint, 1, false, k8, 0},
            {"rgba32i", glc::RGBA32I, glc::RGBA_INTEGER, glc::INT, Cls::Int, 4, false, 1.0f / 127.0f, 0},
            {"rgba32ui", glc::RGBA32UI, glc::RGBA_INTEGER, glc::UNSIGNED_INT, Cls::Uint, 4, false, k8, 0},
            {"rgb10_a2", glc::RGB10_A2, glc::RGBA, glc::UNSIGNED_INT_2_10_10_10_REV, Cls::Float, 4, false, 1, 0},
            {"rgb10_a2ui", glc::RGB10_A2UI, glc::RGBA_INTEGER, glc::UNSIGNED_INT_2_10_10_10_REV, Cls::Uint, 4, false, 1.0f / 1023.0f, 0},
            {"r11f_g11f_b10f", glc::R11F_G11F_B10F, glc::RGB, glc::UNSIGNED_INT_10F_11F_11F_REV, Cls::Float, 3, false, 1, 0},
            {"rgb9_e5", glc::RGB9_E5, glc::RGB, glc::UNSIGNED_INT_5_9_9_9_REV, Cls::Float, 3, false, 1, 0},
            {"rgb565", glc::RGB565, glc::RGB, glc::UNSIGNED_SHORT_5_6_5, Cls::Float, 3, false, 1, 0},
            {"rgb5_a1", glc::RGB5_A1, glc::RGBA, glc::UNSIGNED_SHORT_5_5_5_1, Cls::Float, 4, false, 1, 0},
            {"rgba4", glc::RGBA4, glc::RGBA, glc::UNSIGNED_SHORT_4_4_4_4, Cls::Float, 4, false, 1, 0},
        };
        for (Fmt f : formats) {
            f.variant = ++variant_counter;
            const GLenum internal = f.internal;
            Cases::single(tex(std::string("format_") + f.name).tolerance(display_tolerance(f.cls)), [f](Kit& kit, CaptureSet& out, std::string& error) {
                TexRig rig;
                if (!rig.open(kit, error, 128, display_target(f.cls))) {
                    return false;
                }
                if (!upload_and_show(kit, rig, f, build_image(f), error)) {
                    return false;
                }
                return Gx::shot(kit, rig.target, out, error);
            }, [internal](Kit& kit) { return supported(kit, internal); });
        }

        unsigned stream_counter = 0;
        static_cast<void>(stream_counter);
        const char* streamed[] = {"rgba8", "r8", "rg8", "rgba16f", "rgba32f", "r11f_g11f_b10f", "rgb10_a2", "rgb565", "rgba8ui", "srgb8_alpha8", "rgba4", "rg16ui"};
        for (const char* wanted : streamed) {
            for (Fmt f : formats) {
                if (std::string(f.name) != wanted) {
                    continue;
                }
                f.variant = 40 + (++variant_counter);
                const GLenum internal = f.internal;
                Cases::multi(tex(std::string("stream_rows_") + f.name).frames(8, {0, 3, 7}).tolerance(display_tolerance(f.cls)), [f] {
                    struct State {
                        TexRig rig;
                        GLuint texture = 0;
                        GLuint program = 0;
                        Bytes image;
                        unsigned row_bytes = 0;
                    };
                    auto s = std::make_shared<State>();
                    Fns fns;
                    fns.setup = [s, f](Kit& kit, std::string& error) {
                        if (!s->rig.open(kit, error, 128, display_target(f.cls))) {
                            return false;
                        }
                        s->image = build_image(f);
                        s->row_bytes = static_cast<unsigned>(s->image.size() / 16);
                        s->texture = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
                        kit.gl.glTexStorage2D(glc::TEXTURE_2D, 1, f.internal, 16, 16);
                        kit.gl.glClearTexImage(s->texture, 0, f.format, f.type, nullptr);
                        s->program = s->rig.program(kit, display_source(f.cls), error, "", f.cls == Cls::Float ? "vec4" : "uvec4");
                        return s->program != 0 && kit.check("stream setup", error);
                    };
                    fns.frame = [s, f](Kit& kit, unsigned frame, std::string& error) {
                        Api& gl = kit.gl;
                        gl.glBindTexture(glc::TEXTURE_2D, s->texture);
                        const unsigned first = (frame * 2) % 16;
                        gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, static_cast<GLint>(first), 16, 2, f.format, f.type, s->image.data() + static_cast<std::size_t>(first) * s->row_bytes);
                        s->rig.target.bind(kit);
                        kit.u1i(s->program, "u_tex", 0);
                        s->rig.draw(kit, s->program);
                        return kit.check("stream rows", error);
                    };
                    fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->rig.target, out, error); };
                    return fns;
                });
                static_cast<void>(internal);
            }
        }

        const Fmt transfers[] = {
            {"bgra_ubyte", glc::RGBA8, glc::BGRA, glc::UNSIGNED_BYTE, Cls::Float, 4, true, 1, 0},
            {"ushort_4444_rev", glc::RGBA8, glc::RGBA, glc::UNSIGNED_SHORT_4_4_4_4_REV, Cls::Float, 4, false, 1, 0},
            {"ushort_5551_rev", glc::RGB5_A1, glc::RGBA, glc::UNSIGNED_SHORT_1_5_5_5_REV, Cls::Float, 4, false, 1, 0},
            {"ushort_565_rev", glc::RGB8, glc::RGB, glc::UNSIGNED_SHORT_5_6_5_REV, Cls::Float, 3, false, 1, 0},
            {"uint_8888_rev", glc::RGBA8, glc::RGBA, glc::UNSIGNED_INT_8_8_8_8_REV, Cls::Float, 4, false, 1, 0},
            {"uint_8888", glc::RGBA8, glc::RGBA, glc::UNSIGNED_INT_8_8_8_8, Cls::Float, 4, false, 1, 0},
            {"uint_1010102", glc::RGB10_A2, glc::RGBA, glc::UNSIGNED_INT_10_10_10_2, Cls::Float, 4, false, 1, 0},
            {"float_into_rgba8", glc::RGBA8, glc::RGBA, glc::FLOAT, Cls::Float, 4, false, 1, 0, 0, true},
            {"half_into_rgba16", glc::RGBA16, glc::RGBA, glc::HALF_FLOAT, Cls::Float, 4, false, 1, 0, 0, true},
            {"ushort_into_rgba8ui", glc::RGBA8UI, glc::RGBA_INTEGER, glc::UNSIGNED_SHORT, Cls::Uint, 4, false, k8, 0, 0, true},
            {"uint_into_rg32ui", glc::RG32UI, glc::RG_INTEGER, glc::UNSIGNED_INT, Cls::Uint, 2, false, k8, 0},
            {"rgb_ubyte_into_rgba16f", glc::RGBA16F, glc::RGB, glc::UNSIGNED_BYTE, Cls::Float, 3, false, 1, 0},
        };
        for (Fmt f : transfers) {
            f.variant = ++variant_counter;
            const GLenum internal = f.internal;
            Cases::single(tex(std::string("transfer_") + f.name).tolerance(display_tolerance(f.cls)), [f](Kit& kit, CaptureSet& out, std::string& error) {
                TexRig rig;
                if (!rig.open(kit, error, 128, display_target(f.cls))) {
                    return false;
                }
                if (!upload_and_show(kit, rig, f, build_image(f), error, false)) {
                    return false;
                }
                return Gx::shot(kit, rig.target, out, error);
            }, [internal](Kit& kit) { return supported(kit, internal); });
        }

        const CompressedFmt compressed[] = {
            {"dxt1_rgb", glc::COMPRESSED_RGB_S3TC_DXT1_EXT, 8, "GL_EXT_texture_compression_s3tc", 1},
            {"dxt1_rgba", glc::COMPRESSED_RGBA_S3TC_DXT1_EXT, 8, "GL_EXT_texture_compression_s3tc", 1},
            {"dxt3_rgba", glc::COMPRESSED_RGBA_S3TC_DXT3_EXT, 16, "GL_EXT_texture_compression_s3tc", 1},
            {"dxt5_rgba", glc::COMPRESSED_RGBA_S3TC_DXT5_EXT, 16, "GL_EXT_texture_compression_s3tc", 1},
            {"rgtc1_red", glc::COMPRESSED_RED_RGTC1, 8, nullptr, 1},
            {"rgtc2_rg", glc::COMPRESSED_RG_RGTC2, 16, nullptr, 1},
            {"bptc_bc7_unorm", glc::COMPRESSED_RGBA_BPTC_UNORM, 16, nullptr, 1},
            {"bptc_bc6h_float", glc::COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, 16, nullptr, 0.25f},
            {"etc2_rgb8", glc::COMPRESSED_RGB8_ETC2, 8, nullptr, 1},
            {"etc2_rgba8_eac", glc::COMPRESSED_RGBA8_ETC2_EAC, 16, nullptr, 1},
            {"eac_r11", glc::COMPRESSED_R11_EAC, 8, nullptr, 1},
        };
        for (const CompressedFmt& f : compressed) {
            Cases::single(tex(std::string("compressed_") + f.name).tolerance(Tolerance::tight(1)), [f](Kit& kit, CaptureSet& out, std::string& error) {
                TexRig rig;
                if (!rig.open(kit, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                std::uint32_t state = 12345;
                Bytes blocks;
                for (unsigned b = 0; b < 16; ++b) {
                    for (unsigned i = 0; i < f.block_bytes; ++i) {
                        state = state * 1664525u + 1013904223u;
                        blocks.push_back(static_cast<std::uint8_t>(state >> 24));
                    }
                    if (f.internal == glc::COMPRESSED_RGBA_BPTC_UNORM) {
                        blocks[blocks.size() - f.block_bytes] = static_cast<std::uint8_t>((blocks[blocks.size() - f.block_bytes] & 0xF0) | 0x04);
                    } else if (f.internal == glc::COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT) {
                        blocks[blocks.size() - f.block_bytes] = static_cast<std::uint8_t>((blocks[blocks.size() - f.block_bytes] & 0xFC) | 0x00);
                    }
                }
                const GLuint t = TexRig::texture(kit, glc::TEXTURE_2D, glc::NEAREST);
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, f.internal, 16, 16);
                gl.glCompressedTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 16, 16, f.internal, static_cast<GLsizei>(blocks.size()), blocks.data());
                if (!kit.check("compressed upload", error)) {
                    return false;
                }
                const GLuint program = rig.program(kit, scaled_display_source(Cls::Float, true), error);
                if (program == 0) {
                    return false;
                }
                gl.glBindTexture(glc::TEXTURE_2D, t);
                kit.u1i(program, "u_tex", 0);
                kit.u1f(program, "u_scale", f.scale);
                kit.u1f(program, "u_bias", 0.0f);
                rig.draw(kit, program);
                return Gx::shot(kit, rig.target, out, error);
            }, [f](Kit& kit) -> std::string {
                if (f.extension != nullptr && !kit.has_extension(f.extension)) {
                    return std::string(f.extension) + " is not exposed";
                }
                return supported(kit, f.internal);
            });
        }
    }
} kBatch17;

}  // namespace

}  // namespace lsw::render::gl
