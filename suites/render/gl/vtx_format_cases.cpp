#include "render/gl/flat2d.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec vtx(const char* name) { return CaseSpec("vtx", name).min_version(4, 6); }

using Bytes = std::vector<std::uint8_t>;

template <class T>
void put(Bytes& out, T value) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
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

enum class Path { Float, Integer, Long };

struct FormatDef {
    const char* name;
    GLenum type;
    GLint size;
    bool normalized;
    Path path;
    bool pointer;
    float scale;
    std::function<void(const float*, Bytes&)> encode;
};

const char* shader_type(Path path) {
    switch (path) {
        case Path::Integer: return "ivec4";
        case Path::Long: return "dvec4";
        default: return "vec4";
    }
}

std::vector<FormatDef> format_defs() {
    const auto per4 = [](auto fn) {
        return [fn](const float* c, Bytes& out) {
            for (int i = 0; i < 4; ++i) {
                fn(c[i], out);
            }
        };
    };
    std::vector<FormatDef> d;
    const float k = 1.0f / 255.0f;
    d.push_back({"ubyte_norm", glc::UNSIGNED_BYTE, 4, true, Path::Float, false, 1, per4([](float v, Bytes& o) { put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(v * 255))); })});
    d.push_back({"ubyte_norm_pointer", glc::UNSIGNED_BYTE, 4, true, Path::Float, true, 1, per4([](float v, Bytes& o) { put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(v * 255))); })});
    d.push_back({"ubyte_unnorm", glc::UNSIGNED_BYTE, 4, false, Path::Float, false, k, per4([](float v, Bytes& o) { put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(v * 255))); })});
    d.push_back({"byte_norm", glc::BYTE, 4, true, Path::Float, false, 1, per4([](float v, Bytes& o) { put<std::int8_t>(o, static_cast<std::int8_t>(std::lround(v * 127))); })});
    d.push_back({"byte_unnorm_pointer", glc::BYTE, 4, false, Path::Float, true, 1.0f / 127.0f, per4([](float v, Bytes& o) { put<std::int8_t>(o, static_cast<std::int8_t>(std::lround(v * 127))); })});
    d.push_back({"short_norm", glc::SHORT, 4, true, Path::Float, false, 1, per4([](float v, Bytes& o) { put<std::int16_t>(o, static_cast<std::int16_t>(std::lround(v * 32767))); })});
    d.push_back({"short_unnorm", glc::SHORT, 4, false, Path::Float, false, k, per4([](float v, Bytes& o) { put<std::int16_t>(o, static_cast<std::int16_t>(std::lround(v * 255))); })});
    d.push_back({"ushort_norm", glc::UNSIGNED_SHORT, 4, true, Path::Float, false, 1, per4([](float v, Bytes& o) { put<std::uint16_t>(o, static_cast<std::uint16_t>(std::lround(v * 65535))); })});
    d.push_back({"ushort_norm_pointer", glc::UNSIGNED_SHORT, 4, true, Path::Float, true, 1, per4([](float v, Bytes& o) { put<std::uint16_t>(o, static_cast<std::uint16_t>(std::lround(v * 65535))); })});
    d.push_back({"int_norm", glc::INT, 4, true, Path::Float, false, 1, per4([](float v, Bytes& o) { put<std::int32_t>(o, static_cast<std::int32_t>(static_cast<double>(v) * 2147483647.0)); })});
    d.push_back({"uint_norm", glc::UNSIGNED_INT, 4, true, Path::Float, false, 1, per4([](float v, Bytes& o) { put<std::uint32_t>(o, static_cast<std::uint32_t>(static_cast<double>(v) * 4294967295.0)); })});
    d.push_back({"uint_unnorm", glc::UNSIGNED_INT, 4, false, Path::Float, true, k, per4([](float v, Bytes& o) { put<std::uint32_t>(o, static_cast<std::uint32_t>(std::lround(v * 255))); })});
    d.push_back({"half_float", glc::HALF_FLOAT, 4, false, Path::Float, false, 1, per4([](float v, Bytes& o) { put<std::uint16_t>(o, PixelInfo::float_to_half(v)); })});
    d.push_back({"half_float_pointer", glc::HALF_FLOAT, 4, false, Path::Float, true, 1, per4([](float v, Bytes& o) { put<std::uint16_t>(o, PixelInfo::float_to_half(v)); })});
    d.push_back({"float", glc::FLOAT, 4, false, Path::Float, false, 1, per4([](float v, Bytes& o) { put<float>(o, v); })});
    d.push_back({"fixed", glc::FIXED, 4, false, Path::Float, false, 1, per4([](float v, Bytes& o) { put<std::int32_t>(o, static_cast<std::int32_t>(v * 65536.0f)); })});
    d.push_back({"double_converted", glc::DOUBLE, 4, false, Path::Float, false, 1, per4([](float v, Bytes& o) { put<double>(o, static_cast<double>(v)); })});
    d.push_back({"int_2_10_10_10_rev", glc::INT_2_10_10_10_REV, 4, true, Path::Float, false, 1, [](const float* c, Bytes& o) {
                     const std::uint32_t x = static_cast<std::uint32_t>(std::lround(c[0] * 511)) & 0x3FF;
                     const std::uint32_t y = static_cast<std::uint32_t>(std::lround(c[1] * 511)) & 0x3FF;
                     const std::uint32_t z = static_cast<std::uint32_t>(std::lround(c[2] * 511)) & 0x3FF;
                     put<std::uint32_t>(o, x | (y << 10) | (z << 20) | (1u << 30));
                 }});
    d.push_back({"uint_2_10_10_10_rev", glc::UNSIGNED_INT_2_10_10_10_REV, 4, true, Path::Float, true, 1, [](const float* c, Bytes& o) {
                     const std::uint32_t x = static_cast<std::uint32_t>(std::lround(c[0] * 1023));
                     const std::uint32_t y = static_cast<std::uint32_t>(std::lround(c[1] * 1023));
                     const std::uint32_t z = static_cast<std::uint32_t>(std::lround(c[2] * 1023));
                     put<std::uint32_t>(o, x | (y << 10) | (z << 20) | (3u << 30));
                 }});
    d.push_back({"uint_10f_11f_11f_rev", glc::UNSIGNED_INT_10F_11F_11F_REV, 3, false, Path::Float, false, 1, [](const float* c, Bytes& o) {
                     put<std::uint32_t>(o, small_float(c[0], 6) | (small_float(c[1], 6) << 11) | (small_float(c[2], 5) << 22));
                 }});
    d.push_back({"ubyte_bgra", glc::UNSIGNED_BYTE, static_cast<GLint>(glc::BGRA), true, Path::Float, true, 1, [](const float* c, Bytes& o) {
                     put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(c[2] * 255)));
                     put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(c[1] * 255)));
                     put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(c[0] * 255)));
                     put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(c[3] * 255)));
                 }});
    d.push_back({"ubyte_bgra_format", glc::UNSIGNED_BYTE, static_cast<GLint>(glc::BGRA), true, Path::Float, false, 1, [](const float* c, Bytes& o) {
                     put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(c[2] * 255)));
                     put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(c[1] * 255)));
                     put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(c[0] * 255)));
                     put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(c[3] * 255)));
                 }});
    d.push_back({"ubyte_integer", glc::UNSIGNED_BYTE, 4, false, Path::Integer, false, k, per4([](float v, Bytes& o) { put<std::uint8_t>(o, static_cast<std::uint8_t>(std::lround(v * 255))); })});
    d.push_back({"short_integer_pointer", glc::SHORT, 4, false, Path::Integer, true, k, per4([](float v, Bytes& o) { put<std::int16_t>(o, static_cast<std::int16_t>(std::lround(v * 255))); })});
    d.push_back({"int_integer", glc::INT, 4, false, Path::Integer, false, k, per4([](float v, Bytes& o) { put<std::int32_t>(o, static_cast<std::int32_t>(std::lround(v * 255))); })});
    d.push_back({"uint_integer_pointer", glc::UNSIGNED_INT, 4, false, Path::Integer, true, k, per4([](float v, Bytes& o) { put<std::uint32_t>(o, static_cast<std::uint32_t>(std::lround(v * 255))); })});
    d.push_back({"double_long", glc::DOUBLE, 4, false, Path::Long, false, 1, per4([](float v, Bytes& o) { put<double>(o, static_cast<double>(v)); })});
    d.push_back({"double_long_pointer", glc::DOUBLE, 3, false, Path::Long, true, 1, [](const float* c, Bytes& o) {
                     for (int i = 0; i < 3; ++i) {
                         put<double>(o, static_cast<double>(c[i]));
                     }
                 }});
    return d;
}

const struct Batch7 {
    Batch7() {
        unsigned format_index = 0;
        for (const FormatDef& def : format_defs()) {
            const std::string name = std::string("format_") + def.name;
            const float phase = 0.23f * static_cast<float>(++format_index);
            Cases::single(vtx(name.c_str()), [def, phase](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f);
                Api& gl = kit.gl;
                const std::string vertex = std::string("#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in ") +
                                           shader_type(def.path) +
                                           " in_col;\nuniform float u_scale;\nout vec3 v_col;\nvoid main() {\n    v_col = vec3(in_col.xyz) * u_scale;\n    gl_Position = vec4(in_pos, 0.0, 1.0);\n}\n";
                const GLuint program = kit.program(vertex.c_str(), Flat2d::kFragment, error);
                if (program == 0) {
                    return false;
                }
                const std::vector<V2> verts = Flat2d::ring(9, 0.45f, 0.85f, phase);
                Bytes positions;
                Bytes colors;
                for (const V2& v : verts) {
                    put<float>(positions, v.x);
                    put<float>(positions, v.y);
                    const float c[4] = {v.r, v.g, v.b, 1.0f};
                    def.encode(c, colors);
                }
                const GLsizei color_stride = static_cast<GLsizei>(colors.size() / verts.size());
                kit.vertex_array();
                const GLuint position_buffer = kit.buffer(glc::ARRAY_BUFFER, positions.data(), positions.size(), glc::STATIC_DRAW);
                const GLuint color_buffer = kit.buffer(glc::ARRAY_BUFFER, colors.data(), colors.size(), glc::STATIC_DRAW);
                if (def.pointer) {
                    gl.glBindBuffer(glc::ARRAY_BUFFER, position_buffer);
                    gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, 0, nullptr);
                    gl.glBindBuffer(glc::ARRAY_BUFFER, color_buffer);
                    if (def.path == Path::Float) {
                        gl.glVertexAttribPointer(1, def.size, def.type, def.normalized ? glc::TRUE_ : glc::FALSE_, 0, nullptr);
                    } else if (def.path == Path::Integer) {
                        gl.glVertexAttribIPointer(1, def.size, def.type, 0, nullptr);
                    } else {
                        gl.glVertexAttribLPointer(1, def.size, def.type, 0, nullptr);
                    }
                } else {
                    gl.glVertexAttribFormat(0, 2, glc::FLOAT, glc::FALSE_, 0);
                    gl.glVertexAttribBinding(0, 0);
                    gl.glBindVertexBuffer(0, position_buffer, 0, 8);
                    if (def.path == Path::Float) {
                        gl.glVertexAttribFormat(1, def.size, def.type, def.normalized ? glc::TRUE_ : glc::FALSE_, 0);
                    } else if (def.path == Path::Integer) {
                        gl.glVertexAttribIFormat(1, def.size, def.type, 0);
                    } else {
                        gl.glVertexAttribLFormat(1, def.size, def.type, 0);
                    }
                    gl.glVertexAttribBinding(1, 1);
                    gl.glBindVertexBuffer(1, color_buffer, 0, color_stride);
                }
                gl.glEnableVertexAttribArray(0);
                gl.glEnableVertexAttribArray(1);
                kit.u1f(program, "u_scale", def.scale);
                gl.glDrawArrays(glc::TRIANGLES, 0, 9);
                return Gx::shot(kit, target, out, error);
            });
        }
    }
} kBatch7;

}  // namespace

}  // namespace lsw::render::gl
