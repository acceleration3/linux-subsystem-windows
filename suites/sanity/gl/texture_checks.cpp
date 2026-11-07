#include "sanity/gl/texture_checks.hpp"

#include "sanity/gl/kit.hpp"
#include "support/gl_fetch.hpp"
#include "support/gl_scene.hpp"
#include "support/gl_target.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::gl {

namespace {

std::vector<std::uint8_t> read_image(const GlApi& api, unsigned target, int level, std::size_t size, unsigned format = glc::RGBA, unsigned type = glc::UNSIGNED_BYTE) {
    std::vector<std::uint8_t> data(size, 0xCD);
    api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
    api.glGetTexImage(target, level, format, type, data.data());
    return data;
}

std::string first_difference(const std::vector<std::uint8_t>& expected, const std::vector<std::uint8_t>& actual) {
    if (expected.size() != actual.size()) {
        return "sizes " + std::to_string(expected.size()) + " and " + std::to_string(actual.size());
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            return "byte " + std::to_string(i) + " is 0x" + Text::hex(actual[i], 2) + ", expected 0x" + Text::hex(expected[i], 2);
        }
    }
    return {};
}

unsigned make_texture(const GlApi& api, unsigned target) {
    unsigned texture = 0;
    api.glGenTextures(1, &texture);
    api.glBindTexture(target, texture);
    if (target != glc::TEXTURE_2D_MULTISAMPLE && target != glc::TEXTURE_BUFFER) {
        api.glTexParameteri(target, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
        api.glTexParameteri(target, glc::TEXTURE_MAG_FILTER, static_cast<int>(glc::NEAREST));
    }
    return texture;
}

int level_param(const GlApi& api, unsigned target, int level, unsigned name) {
    int value = -1;
    api.glGetTexLevelParameteriv(target, level, name, &value);
    return value;
}

int level_count(int w, int h, int d) {
    int largest = std::max(w, std::max(h, d));
    int levels = 1;
    while (largest > 1) {
        largest >>= 1;
        ++levels;
    }
    return levels;
}

}  // namespace

Outcome TextureChecks::all_targets_roundtrip(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    Problems problems;
    int tested = 0;
    std::string skipped;
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    auto report = [&](const char* name, const std::vector<std::uint8_t>& expected, const std::vector<std::uint8_t>& actual, const std::string& extra = {}) {
        const unsigned failure = api.drain_errors();
        const std::string difference = first_difference(expected, actual);
        if (failure != 0 || !difference.empty()) {
            problems.add(std::string(name) + " " + extra + ": " + (failure != 0 ? GlApi::error_name(failure) + " " : std::string()) + difference);
        }
        ++tested;
    };
    {
        const std::vector<std::uint8_t> data = Kit::bytes(120, 32 * 4);
        const unsigned t = make_texture(api, glc::TEXTURE_1D);
        api.glTexImage1D(glc::TEXTURE_1D, 0, static_cast<int>(glc::RGBA8), 32, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        report("TEXTURE_1D", data, read_image(api, glc::TEXTURE_1D, 0, data.size()));
        api.glDeleteTextures(1, &t);
    }
    {
        const std::vector<std::uint8_t> data = Kit::bytes(121, 24 * 20 * 4);
        const unsigned t = make_texture(api, glc::TEXTURE_2D);
        api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), 24, 20, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        report("TEXTURE_2D", data, read_image(api, glc::TEXTURE_2D, 0, data.size()));
        api.glDeleteTextures(1, &t);
    }
    {
        const std::vector<std::uint8_t> data = Kit::bytes(122, 8 * 6 * 5 * 4);
        const unsigned t = make_texture(api, glc::TEXTURE_3D);
        api.glTexImage3D(glc::TEXTURE_3D, 0, static_cast<int>(glc::RGBA8), 8, 6, 5, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        report("TEXTURE_3D", data, read_image(api, glc::TEXTURE_3D, 0, data.size()));
        api.glDeleteTextures(1, &t);
    }
    {
        const unsigned t = make_texture(api, glc::TEXTURE_CUBE_MAP);
        for (unsigned face = 0; face < 6; ++face) {
            const std::vector<std::uint8_t> data = Kit::bytes(130 + face, 8 * 8 * 4);
            api.glTexImage2D(glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, static_cast<int>(glc::RGBA8), 8, 8, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            report("TEXTURE_CUBE_MAP", data, read_image(api, glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, data.size()), "face " + std::to_string(face));
        }
        api.glDeleteTextures(1, &t);
    }
    {
        const std::vector<std::uint8_t> data = Kit::bytes(123, 16 * 4 * 4);
        const unsigned t = make_texture(api, glc::TEXTURE_1D_ARRAY);
        api.glTexImage2D(glc::TEXTURE_1D_ARRAY, 0, static_cast<int>(glc::RGBA8), 16, 4, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        report("TEXTURE_1D_ARRAY", data, read_image(api, glc::TEXTURE_1D_ARRAY, 0, data.size()));
        api.glDeleteTextures(1, &t);
    }
    {
        const std::vector<std::uint8_t> data = Kit::bytes(124, 8 * 8 * 3 * 4);
        const unsigned t = make_texture(api, glc::TEXTURE_2D_ARRAY);
        api.glTexImage3D(glc::TEXTURE_2D_ARRAY, 0, static_cast<int>(glc::RGBA8), 8, 8, 3, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        report("TEXTURE_2D_ARRAY", data, read_image(api, glc::TEXTURE_2D_ARRAY, 0, data.size()));
        api.glDeleteTextures(1, &t);
    }
    if (api.supports(4, 0, "GL_ARB_texture_cube_map_array")) {
        const std::vector<std::uint8_t> data = Kit::bytes(125, 8 * 8 * 12 * 4);
        const unsigned t = make_texture(api, glc::TEXTURE_CUBE_MAP_ARRAY);
        api.glTexImage3D(glc::TEXTURE_CUBE_MAP_ARRAY, 0, static_cast<int>(glc::RGBA8), 8, 8, 12, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        report("TEXTURE_CUBE_MAP_ARRAY", data, read_image(api, glc::TEXTURE_CUBE_MAP_ARRAY, 0, data.size()));
        api.glDeleteTextures(1, &t);
    } else {
        skipped += " CUBE_MAP_ARRAY";
    }
    if (api.supports(3, 1, "GL_ARB_texture_rectangle")) {
        const std::vector<std::uint8_t> data = Kit::bytes(126, 20 * 12 * 4);
        const unsigned t = make_texture(api, glc::TEXTURE_RECTANGLE);
        api.glTexImage2D(glc::TEXTURE_RECTANGLE, 0, static_cast<int>(glc::RGBA8), 20, 12, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        report("TEXTURE_RECTANGLE", data, read_image(api, glc::TEXTURE_RECTANGLE, 0, data.size()));
        api.glDeleteTextures(1, &t);
    } else {
        skipped += " RECTANGLE";
    }
    if (api.supports(3, 1, "GL_ARB_texture_buffer_object") && api.glTexBuffer != nullptr) {
        const std::vector<std::uint8_t> data = Kit::bytes(127, 256 * 4);
        unsigned buffer = 0;
        api.glGenBuffers(1, &buffer);
        api.glBindBuffer(glc::ARRAY_BUFFER, buffer);
        api.glBufferData(glc::ARRAY_BUFFER, static_cast<std::ptrdiff_t>(data.size()), data.data(), glc::STATIC_DRAW);
        const unsigned t = make_texture(api, glc::TEXTURE_BUFFER);
        api.glTexBuffer(glc::TEXTURE_BUFFER, glc::RGBA8, buffer);
        GlFetchRequest request;
        request.sampler = "samplerBuffer";
        request.expression = "texelFetch(tex, p.x + p.y * 16)";
        request.target = glc::TEXTURE_BUFFER;
        request.texture = t;
        request.width = 16;
        request.height = 16;
        std::vector<float> values;
        if (!GlFetch::run(api, request, values, error)) {
            problems.add("TEXTURE_BUFFER: " + error);
        } else {
            for (std::size_t i = 0; i < data.size(); ++i) {
                if (std::fabs(values[i] - static_cast<float>(data[i]) / 255.0f) > 0.002f) {
                    problems.add("TEXTURE_BUFFER: texel component " + std::to_string(i) + " samples as " + std::to_string(values[i]) + ", expected " + std::to_string(data[i]) + "/255");
                    break;
                }
            }
            ++tested;
        }
        api.glDeleteTextures(1, &t);
        api.glDeleteBuffers(1, &buffer);
    } else {
        skipped += " BUFFER";
    }
    if (api.supports(3, 2, "GL_ARB_texture_multisample") && api.glTexImage2DMultisample != nullptr) {
        int samples = std::min(4, api.integer(glc::MAX_SAMPLES));
        const unsigned t = make_texture(api, glc::TEXTURE_2D_MULTISAMPLE);
        (void)api.drain_errors();
        api.glTexImage2DMultisample(glc::TEXTURE_2D_MULTISAMPLE, samples, glc::RGBA8, 16, 16, 1);
        unsigned fbo = 0;
        api.glGenFramebuffers(1, &fbo);
        api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
        api.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D_MULTISAMPLE, t, 0);
        if (api.drain_errors() != 0 || api.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
            problems.add("TEXTURE_2D_MULTISAMPLE with " + std::to_string(samples) + " samples cannot be attached to a complete framebuffer");
        } else {
            GlScene scene(16, 16);
            scene.clear_color(GlTools::rgba(0, 255, 0, 255));
            scene.add({0, 0, 8, 16, GlTools::rgba(255, 0, 255, 255)});
            if (!scene.draw(api, error)) {
                problems.add("TEXTURE_2D_MULTISAMPLE: " + error);
            } else {
                for (int sample = 0; sample < samples; ++sample) {
                    GlFetchRequest request;
                    request.sampler = "sampler2DMS";
                    request.expression = "texelFetch(tex, p, params.x)";
                    request.target = glc::TEXTURE_2D_MULTISAMPLE;
                    request.texture = t;
                    request.width = 16;
                    request.height = 16;
                    request.first = sample;
                    std::vector<float> values;
                    if (!GlFetch::run(api, request, values, error)) {
                        problems.add("TEXTURE_2D_MULTISAMPLE sample " + std::to_string(sample) + ": " + error);
                        break;
                    }
                    for (int y = 0; y < 16 && !values.empty(); ++y) {
                        for (int x = 0; x < 16; ++x) {
                            const float* px = &values[static_cast<std::size_t>(y * 16 + x) * 4];
                            const bool left = x < 8;
                            if (std::fabs(px[0] - (left ? 1.0f : 0.0f)) > 0.01f || std::fabs(px[1] - (left ? 0.0f : 1.0f)) > 0.01f || std::fabs(px[2] - (left ? 1.0f : 0.0f)) > 0.01f) {
                                problems.add("TEXTURE_2D_MULTISAMPLE sample " + std::to_string(sample) + " pixel (" + std::to_string(x) + "," + std::to_string(y) + ") fetches (" +
                                             std::to_string(px[0]) + "," + std::to_string(px[1]) + "," + std::to_string(px[2]) + ")");
                                y = 16;
                                break;
                            }
                        }
                    }
                }
                ++tested;
            }
            scene.release();
        }
        api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        api.glDeleteFramebuffers(1, &fbo);
        api.glDeleteTextures(1, &t);
    } else {
        skipped += " MULTISAMPLE";
    }
    context.measure("targets_tested", tested);
    if (!skipped.empty()) {
        context.note("targets not advertised by this context:" + skipped);
    }
    return problems.outcome();
}

namespace {

void color565(unsigned c, int out[3]) {
    const int r = static_cast<int>((c >> 11) & 31);
    const int g = static_cast<int>((c >> 5) & 63);
    const int b = static_cast<int>(c & 31);
    out[0] = (r << 3) | (r >> 2);
    out[1] = (g << 2) | (g >> 4);
    out[2] = (b << 3) | (b >> 2);
}

void decode_bc1(const std::uint8_t* block, bool opaque_mode_always, int out[16][4]) {
    const unsigned c0 = block[0] | (static_cast<unsigned>(block[1]) << 8);
    const unsigned c1 = block[2] | (static_cast<unsigned>(block[3]) << 8);
    int palette[4][4];
    color565(c0, palette[0]);
    color565(c1, palette[1]);
    palette[0][3] = palette[1][3] = palette[2][3] = palette[3][3] = 255;
    if (c0 > c1 || opaque_mode_always) {
        for (int k = 0; k < 3; ++k) {
            palette[2][k] = (2 * palette[0][k] + palette[1][k]) / 3;
            palette[3][k] = (palette[0][k] + 2 * palette[1][k]) / 3;
        }
    } else {
        for (int k = 0; k < 3; ++k) {
            palette[2][k] = (palette[0][k] + palette[1][k]) / 2;
            palette[3][k] = 0;
        }
        palette[3][3] = 0;
    }
    const unsigned indices = block[4] | (static_cast<unsigned>(block[5]) << 8) | (static_cast<unsigned>(block[6]) << 16) | (static_cast<unsigned>(block[7]) << 24);
    for (int i = 0; i < 16; ++i) {
        const int index = static_cast<int>((indices >> (2 * i)) & 3);
        for (int k = 0; k < 4; ++k) {
            out[i][k] = palette[index][k];
        }
    }
}

void decode_alpha_block(const std::uint8_t* block, int out[16]) {
    int a[8];
    a[0] = block[0];
    a[1] = block[1];
    if (a[0] > a[1]) {
        for (int i = 1; i < 7; ++i) {
            a[1 + i] = ((7 - i) * a[0] + i * a[1]) / 7;
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            a[1 + i] = ((5 - i) * a[0] + i * a[1]) / 5;
        }
        a[6] = 0;
        a[7] = 255;
    }
    unsigned long long bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<unsigned long long>(block[2 + i]) << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        out[i] = a[(bits >> (3 * i)) & 7];
    }
}

bool decode_block(unsigned format, const std::uint8_t* block, bool& rg_only, int out[16][4]) {
    rg_only = false;
    switch (format) {
    case glc::COMPRESSED_RGB_S3TC_DXT1_EXT:
    case glc::COMPRESSED_RGBA_S3TC_DXT1_EXT:
        decode_bc1(block, false, out);
        if (format == glc::COMPRESSED_RGB_S3TC_DXT1_EXT) {
            for (int i = 0; i < 16; ++i) {
                out[i][3] = 255;
            }
        }
        return true;
    case glc::COMPRESSED_RGBA_S3TC_DXT3_EXT: {
        decode_bc1(block + 8, true, out);
        for (int i = 0; i < 16; ++i) {
            const int nibble = (block[i / 2] >> (4 * (i % 2))) & 15;
            out[i][3] = nibble * 17;
        }
        return true;
    }
    case glc::COMPRESSED_RGBA_S3TC_DXT5_EXT: {
        decode_bc1(block + 8, true, out);
        int alpha[16];
        decode_alpha_block(block, alpha);
        for (int i = 0; i < 16; ++i) {
            out[i][3] = alpha[i];
        }
        return true;
    }
    case glc::COMPRESSED_RED_RGTC1:
    case glc::COMPRESSED_RG_RGTC2: {
        int red[16];
        decode_alpha_block(block, red);
        int green[16] = {};
        if (format == glc::COMPRESSED_RG_RGTC2) {
            decode_alpha_block(block + 8, green);
            rg_only = true;
        }
        for (int i = 0; i < 16; ++i) {
            out[i][0] = red[i];
            out[i][1] = green[i];
            out[i][2] = 0;
            out[i][3] = 255;
        }
        return true;
    }
    default: return false;
    }
}

}  // namespace

Outcome TextureChecks::compressed_formats(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    const int count = api.integer(glc::NUM_COMPRESSED_TEXTURE_FORMATS);
    if (count <= 0) {
        return Kit::not_advertised("any compressed texture format");
    }
    std::vector<int> formats(static_cast<std::size_t>(count));
    api.glGetIntegerv(glc::COMPRESSED_TEXTURE_FORMATS, formats.data());
    constexpr int kSide = 120;
    const std::vector<std::uint8_t> source = Kit::bytes(140, static_cast<std::size_t>(kSide) * kSide * 4);
    Problems problems;
    int decoded_formats = 0;
    context.measure("advertised_formats", count);
    for (const int signed_format : formats) {
        const unsigned format = static_cast<unsigned>(signed_format);
        const std::string name = "format 0x" + Text::hex(format, 4);
        (void)api.drain_errors();
        const unsigned t = make_texture(api, glc::TEXTURE_2D);
        api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
        if (api.glTexStorage2D != nullptr) {
            api.glTexStorage2D(glc::TEXTURE_2D, 1, format, kSide, kSide);
        } else {
            api.glTexImage2D(glc::TEXTURE_2D, 0, signed_format, kSide, kSide, 0, glc::RGBA, glc::UNSIGNED_BYTE, source.data());
        }
        const unsigned upload_error = api.drain_errors();
        if (upload_error != 0) {
            problems.add(name + ": allocating storage in an advertised compressed internal format gave " + GlApi::error_name(upload_error));
            api.glDeleteTextures(1, &t);
            continue;
        }
        const int compressed = level_param(api, glc::TEXTURE_2D, 0, glc::TEXTURE_COMPRESSED);
        const int size = level_param(api, glc::TEXTURE_2D, 0, glc::TEXTURE_COMPRESSED_IMAGE_SIZE);
        if (compressed != 1 || size <= 0) {
            problems.add(name + ": the level reports TEXTURE_COMPRESSED " + std::to_string(compressed) + " and size " + std::to_string(size));
            api.glDeleteTextures(1, &t);
            continue;
        }
        api.glDeleteTextures(1, &t);
        const unsigned t2 = make_texture(api, glc::TEXTURE_2D);
        std::vector<std::uint8_t> blocks = Kit::bytes(150 + format, static_cast<std::size_t>(size));
        api.glCompressedTexImage2D(glc::TEXTURE_2D, 0, format, kSide, kSide, 0, size, blocks.data());
        const unsigned compressed_error = api.drain_errors();
        std::vector<std::uint8_t> back(static_cast<std::size_t>(size), 0xCD);
        api.glGetCompressedTexImage(glc::TEXTURE_2D, 0, back.data());
        const unsigned read_error = api.drain_errors();
        if (compressed_error != 0 || read_error != 0 || back != blocks) {
            problems.add(name + ": CompressedTexImage2D then GetCompressedTexImage of " + std::to_string(size) + " bytes gave " + GlApi::error_name(compressed_error) + "/" +
                         GlApi::error_name(read_error) + (back == blocks ? std::string() : ", " + first_difference(blocks, back)));
            api.glDeleteTextures(1, &t2);
            continue;
        }
        int probe[16][4];
        bool rg_only = false;
        if (decode_block(format, blocks.data(), rg_only, probe)) {
            GlFetchRequest request;
            request.sampler = "sampler2D";
            request.expression = "texelFetch(tex, p, 0)";
            request.target = glc::TEXTURE_2D;
            request.texture = t2;
            request.width = kSide;
            request.height = kSide;
            std::vector<float> values;
            if (!GlFetch::run(api, request, values, error)) {
                problems.add(name + ": " + error);
            } else {
                const int block_bytes = (format == glc::COMPRESSED_RGB_S3TC_DXT1_EXT || format == glc::COMPRESSED_RGBA_S3TC_DXT1_EXT || format == glc::COMPRESSED_RED_RGTC1) ? 8 : 16;
                const float tolerance = 4.0f / 255.0f;
                bool bad = false;
                for (int by = 0; by < kSide / 4 && !bad; ++by) {
                    for (int bx = 0; bx < kSide / 4 && !bad; ++bx) {
                        int block[16][4];
                        bool dummy = false;
                        (void)decode_block(format, &blocks[static_cast<std::size_t>(by * (kSide / 4) + bx) * static_cast<std::size_t>(block_bytes)], dummy, block);
                        for (int i = 0; i < 16 && !bad; ++i) {
                            const int x = bx * 4 + i % 4;
                            const int y = by * 4 + i / 4;
                            const float* px = &values[static_cast<std::size_t>(y * kSide + x) * 4];
                            const bool transparent = (format == glc::COMPRESSED_RGBA_S3TC_DXT1_EXT) && block[i][3] == 0;
                            for (int k = 0; k < 4; ++k) {
                                if (transparent && k < 3) {
                                    continue;
                                }
                                if (std::fabs(px[k] - static_cast<float>(block[i][k]) / 255.0f) > tolerance) {
                                    problems.add(name + ": texel (" + std::to_string(x) + "," + std::to_string(y) + ") channel " + std::to_string(k) + " samples as " +
                                                 std::to_string(px[k] * 255.0f) + ", the CPU decoder gives " + std::to_string(block[i][k]));
                                    bad = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                ++decoded_formats;
            }
        }
        api.glDeleteTextures(1, &t2);
    }
    context.measure("formats_with_cpu_decoder", decoded_formats);
    return problems.outcome();
}

Outcome TextureChecks::generate_mipmap(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 0, "GL_ARB_framebuffer_object") || api.glGenerateMipmap == nullptr) {
        return Kit::not_advertised("glGenerateMipmap");
    }
    struct Case {
        unsigned target;
        const char* name;
        int w;
        int h;
        int d;
        int layers;
    };
    const Case cases[] = {{glc::TEXTURE_1D, "TEXTURE_1D", 32, 1, 1, 1},       {glc::TEXTURE_2D, "TEXTURE_2D", 32, 16, 1, 1},
                          {glc::TEXTURE_3D, "TEXTURE_3D", 8, 8, 8, 1},        {glc::TEXTURE_CUBE_MAP, "TEXTURE_CUBE_MAP", 16, 16, 1, 6},
                          {glc::TEXTURE_1D_ARRAY, "TEXTURE_1D_ARRAY", 16, 3, 1, 1}, {glc::TEXTURE_2D_ARRAY, "TEXTURE_2D_ARRAY", 16, 16, 3, 1}};
    Problems problems;
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    for (const Case& item : cases) {
        const unsigned t = make_texture(api, item.target);
        const bool array1d = item.target == glc::TEXTURE_1D_ARRAY;
        const bool array2d = item.target == glc::TEXTURE_2D_ARRAY;
        const std::size_t level0 = static_cast<std::size_t>(item.w) * static_cast<std::size_t>(item.h) * static_cast<std::size_t>(item.d) * 4;
        (void)api.drain_errors();
        std::vector<std::vector<std::uint8_t>> faces;
        for (int face = 0; face < item.layers; ++face) {
            std::vector<std::uint8_t> data = Kit::bytes(static_cast<std::uint64_t>(170 + face), level0);
            faces.push_back(data);
            const unsigned target = item.target == glc::TEXTURE_CUBE_MAP ? glc::TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<unsigned>(face) : item.target;
            if (item.target == glc::TEXTURE_1D) {
                api.glTexImage1D(target, 0, static_cast<int>(glc::RGBA8), item.w, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            } else if (item.target == glc::TEXTURE_3D || array2d) {
                api.glTexImage3D(target, 0, static_cast<int>(glc::RGBA8), item.w, item.h, item.d, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            } else {
                api.glTexImage2D(target, 0, static_cast<int>(glc::RGBA8), item.w, item.h, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            }
        }
        api.glGenerateMipmap(item.target);
        const unsigned failure = api.drain_errors();
        if (failure != 0) {
            problems.add(std::string(item.name) + ": upload and GenerateMipmap gave " + GlApi::error_name(failure));
            api.glDeleteTextures(1, &t);
            continue;
        }
        const unsigned probe_target = item.target == glc::TEXTURE_CUBE_MAP ? glc::TEXTURE_CUBE_MAP_POSITIVE_X : item.target;
        const int sizes_d = (item.target == glc::TEXTURE_3D) ? item.d : 1;
        const int expected_levels = level_count(item.w, (array1d ? 1 : item.h), sizes_d);
        int levels = 0;
        while (levels < 20 && level_param(api, probe_target, levels, glc::TEXTURE_WIDTH) > 0) {
            ++levels;
        }
        (void)api.drain_errors();
        if (levels != expected_levels) {
            problems.add(std::string(item.name) + ": GenerateMipmap produced " + std::to_string(levels) + " levels, expected " + std::to_string(expected_levels));
            api.glDeleteTextures(1, &t);
            continue;
        }
        for (int face = 0; face < item.layers; ++face) {
            const unsigned target = item.target == glc::TEXTURE_CUBE_MAP ? glc::TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<unsigned>(face) : item.target;
            int pw = item.w;
            int ph = item.h;
            int pd = item.d;
            std::vector<std::uint8_t> previous = faces[static_cast<std::size_t>(face)];
            for (int level = 1; level < levels; ++level) {
                const int w = std::max(1, pw / 2);
                const int h = (array1d || item.target == glc::TEXTURE_1D) ? ph : std::max(1, ph / 2);
                const int d = (item.target == glc::TEXTURE_3D) ? std::max(1, pd / 2) : pd;
                const int gw = level_param(api, probe_target, level, glc::TEXTURE_WIDTH);
                const int gh = level_param(api, probe_target, level, glc::TEXTURE_HEIGHT);
                const int gd = level_param(api, probe_target, level, glc::TEXTURE_DEPTH);
                if (gw != w || (item.target != glc::TEXTURE_1D && gh != h) || (item.target == glc::TEXTURE_3D && gd != d)) {
                    problems.add(std::string(item.name) + " level " + std::to_string(level) + " is " + std::to_string(gw) + "x" + std::to_string(gh) + "x" + std::to_string(gd) +
                                 ", expected " + std::to_string(w) + "x" + std::to_string(h) + "x" + std::to_string(d));
                    break;
                }
                const std::size_t size = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(d) * 4;
                const std::vector<std::uint8_t> got = read_image(api, target, level, size);
                std::vector<std::uint8_t> expected(size);
                const int sx = pw / w;
                const int sy = (array1d || item.target == glc::TEXTURE_1D) ? 1 : ph / h;
                const int sz = (item.target == glc::TEXTURE_3D) ? pd / d : 1;
                for (int z = 0; z < d; ++z) {
                    for (int y = 0; y < h; ++y) {
                        for (int x = 0; x < w; ++x) {
                            for (int c = 0; c < 4; ++c) {
                                int sum = 0;
                                int n = 0;
                                for (int oz = 0; oz < sz; ++oz) {
                                    for (int oy = 0; oy < sy; ++oy) {
                                        for (int ox = 0; ox < sx; ++ox) {
                                            const std::size_t at = ((static_cast<std::size_t>(z * sz + oz) * static_cast<std::size_t>(ph) + static_cast<std::size_t>(y * sy + oy)) *
                                                                        static_cast<std::size_t>(pw) + static_cast<std::size_t>(x * sx + ox)) * 4 + static_cast<std::size_t>(c);
                                            sum += previous[at];
                                            ++n;
                                        }
                                    }
                                }
                                expected[((static_cast<std::size_t>(z) * static_cast<std::size_t>(h) + static_cast<std::size_t>(y)) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4 +
                                         static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((sum + n / 2) / n);
                            }
                        }
                    }
                }
                for (std::size_t i = 0; i < size; ++i) {
                    if (std::abs(static_cast<int>(got[i]) - static_cast<int>(expected[i])) > 1) {
                        problems.add(std::string(item.name) + (item.layers > 1 ? " face " + std::to_string(face) : std::string()) + " level " + std::to_string(level) + " byte " + std::to_string(i) +
                                     " is " + std::to_string(got[i]) + ", the box filter of the previous level gives " + std::to_string(expected[i]));
                        break;
                    }
                }
                previous = got;
                pw = w;
                ph = h;
                pd = d;
            }
        }
        api.glDeleteTextures(1, &t);
    }
    return problems.outcome();
}

Outcome TextureChecks::views(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(4, 3, "GL_ARB_texture_view") || api.glTextureView == nullptr || api.glTexStorage3D == nullptr) {
        return Kit::not_advertised("OpenGL 4.3 or GL_ARB_texture_view with texture storage");
    }
    Problems problems;
    constexpr int kSide = 16;
    constexpr int kLayers = 4;
    constexpr int kLevels = 5;
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    const unsigned parent = make_texture(api, glc::TEXTURE_2D_ARRAY);
    api.glTexStorage3D(glc::TEXTURE_2D_ARRAY, kLevels, glc::RGBA8, kSide, kSide, kLayers);
    for (int level = 0; level < kLevels; ++level) {
        for (int layer = 0; layer < kLayers; ++layer) {
            const int size = std::max(1, kSide >> level);
            const std::vector<std::uint8_t> data = Kit::bytes(static_cast<std::uint64_t>(200 + level * 10 + layer), static_cast<std::size_t>(size * size * 4));
            api.glTexSubImage3D(glc::TEXTURE_2D_ARRAY, level, 0, 0, layer, size, size, 1, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        }
    }
    GlTools::expect_clean(problems, api, "filling the parent");
    auto layer_bytes = [&](unsigned texture, int level, int layer, int layers_in, int size_level) {
        api.glBindTexture(glc::TEXTURE_2D_ARRAY, texture);
        const int size = std::max(1, kSide >> size_level);
        const std::size_t layer_size = static_cast<std::size_t>(size * size * 4);
        const std::vector<std::uint8_t> all = read_image(api, glc::TEXTURE_2D_ARRAY, level, layer_size * static_cast<std::size_t>(layers_in));
        return std::vector<std::uint8_t>(all.begin() + static_cast<std::ptrdiff_t>(layer_size * static_cast<std::size_t>(layer)),
                                         all.begin() + static_cast<std::ptrdiff_t>(layer_size * static_cast<std::size_t>(layer + 1)));
    };
    unsigned view = 0;
    api.glGenTextures(1, &view);
    api.glTextureView(view, glc::TEXTURE_2D_ARRAY, parent, glc::RGBA8, 1, 2, 1, 2);
    if (api.drain_errors() != 0) {
        problems.add("glTextureView over levels 1-2 and layers 1-2 failed");
    } else {
        for (int level = 0; level < 2; ++level) {
            for (int layer = 0; layer < 2; ++layer) {
                if (layer_bytes(view, level, layer, 2, level + 1) != layer_bytes(parent, level + 1, layer + 1, kLayers, level + 1)) {
                    problems.add("view level " + std::to_string(level) + " layer " + std::to_string(layer) + " differs from parent level " + std::to_string(level + 1) + " layer " +
                                 std::to_string(layer + 1));
                }
            }
        }
        GlFetchRequest request;
        request.sampler = "sampler2DArray";
        request.expression = "texelFetch(tex, ivec3(p, params.y), params.x)";
        request.target = glc::TEXTURE_2D_ARRAY;
        request.texture = view;
        request.width = 8;
        request.height = 8;
        request.first = 0;
        request.second = 1;
        std::vector<float> values;
        if (!GlFetch::run(api, request, values, error)) {
            problems.add("sampling the view: " + error);
        } else {
            const std::vector<std::uint8_t> expected = layer_bytes(parent, 1, 2, kLayers, 1);
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (std::fabs(values[i] - static_cast<float>(expected[i]) / 255.0f) > 0.002f) {
                    problems.add("sampling view layer 1 level 0 gives " + std::to_string(values[i] * 255.0f) + " at component " + std::to_string(i) + ", parent texel is " + std::to_string(expected[i]));
                    break;
                }
            }
        }
        const std::vector<std::uint8_t> fresh = Kit::bytes(777, 8 * 8 * 4);
        api.glBindTexture(glc::TEXTURE_2D_ARRAY, parent);
        api.glTexSubImage3D(glc::TEXTURE_2D_ARRAY, 1, 0, 0, 1, 8, 8, 1, glc::RGBA, glc::UNSIGNED_BYTE, fresh.data());
        if (layer_bytes(view, 0, 0, 2, 1) != fresh) {
            problems.add("a parent write through glTexSubImage3D is not visible through the view");
        }
    }
    unsigned integer_view = 0;
    api.glGenTextures(1, &integer_view);
    api.glTextureView(integer_view, glc::TEXTURE_2D_ARRAY, parent, glc::RGBA8UI, 0, kLevels, 0, kLayers);
    if (api.drain_errors() != 0) {
        problems.add("glTextureView of an RGBA8 texture as RGBA8UI failed");
    } else {
        api.glBindTexture(glc::TEXTURE_2D_ARRAY, integer_view);
        const std::size_t size = static_cast<std::size_t>(kSide * kSide * 4 * kLayers);
        const std::vector<std::uint8_t> through_view = read_image(api, glc::TEXTURE_2D_ARRAY, 0, size, glc::RGBA_INTEGER, glc::UNSIGNED_BYTE);
        api.glBindTexture(glc::TEXTURE_2D_ARRAY, parent);
        const std::vector<std::uint8_t> direct = read_image(api, glc::TEXTURE_2D_ARRAY, 0, size);
        if (through_view != direct) {
            problems.add("the RGBA8UI view reads different bytes than the RGBA8 parent: " + first_difference(direct, through_view));
        }
    }
    unsigned single = 0;
    api.glGenTextures(1, &single);
    api.glTextureView(single, glc::TEXTURE_2D, parent, glc::RGBA8, 0, 1, 2, 1);
    if (api.drain_errors() != 0) {
        problems.add("glTextureView of one layer as TEXTURE_2D failed");
    } else {
        api.glBindTexture(glc::TEXTURE_2D, single);
        const std::vector<std::uint8_t> got = read_image(api, glc::TEXTURE_2D, 0, static_cast<std::size_t>(kSide * kSide * 4));
        if (got != layer_bytes(parent, 0, 2, kLayers, 0)) {
            problems.add("the single-layer view differs from layer 2 of the parent");
        }
    }
    GlTools::expect_clean(problems, api, "the view reads");
    api.glDeleteTextures(1, &view);
    api.glDeleteTextures(1, &integer_view);
    api.glDeleteTextures(1, &single);
    api.glDeleteTextures(1, &parent);
    return problems.outcome();
}

Outcome TextureChecks::immutable_storage(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(4, 2, "GL_ARB_texture_storage") || api.glTexStorage2D == nullptr) {
        return Kit::not_advertised("OpenGL 4.2 or GL_ARB_texture_storage");
    }
    Problems problems;
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    struct Size {
        int w;
        int h;
    };
    const Size sizes[] = {{64, 64}, {37, 11}, {1, 1}, {128, 4}};
    for (const Size& size : sizes) {
        const int full = level_count(size.w, size.h, 1);
        const std::string text = std::to_string(size.w) + "x" + std::to_string(size.h);
        for (const int levels : {1, full, full + 1, 0}) {
            const unsigned t = make_texture(api, glc::TEXTURE_2D);
            (void)api.drain_errors();
            api.glTexStorage2D(glc::TEXTURE_2D, levels, glc::RGBA8, size.w, size.h);
            const unsigned failure = api.drain_errors();
            const unsigned wanted = levels == 0 ? glc::INVALID_VALUE : levels > full ? glc::INVALID_OPERATION : glc::NO_ERROR_;
            if (failure != wanted) {
                problems.add("TexStorage2D(" + std::to_string(levels) + " levels, " + text + ") gave " + GlApi::error_name(failure) + ", expected " + GlApi::error_name(wanted));
            }
            api.glDeleteTextures(1, &t);
        }
        const unsigned t = make_texture(api, glc::TEXTURE_2D);
        api.glTexStorage2D(glc::TEXTURE_2D, full, glc::RGBA8, size.w, size.h);
        (void)api.drain_errors();
        int immutable = 0;
        int immutable_levels = 0;
        api.glGetTexParameteriv(glc::TEXTURE_2D, glc::TEXTURE_IMMUTABLE_FORMAT, &immutable);
        api.glGetTexParameteriv(glc::TEXTURE_2D, glc::TEXTURE_IMMUTABLE_LEVELS, &immutable_levels);
        if (immutable != 1 || immutable_levels != full) {
            problems.add(text + ": TEXTURE_IMMUTABLE_FORMAT " + std::to_string(immutable) + ", TEXTURE_IMMUTABLE_LEVELS " + std::to_string(immutable_levels) + ", expected 1 and " + std::to_string(full));
        }
        for (int level = 0; level < full; ++level) {
            const int w = std::max(1, size.w >> level);
            const int h = std::max(1, size.h >> level);
            if (level_param(api, glc::TEXTURE_2D, level, glc::TEXTURE_WIDTH) != w || level_param(api, glc::TEXTURE_2D, level, glc::TEXTURE_HEIGHT) != h) {
                problems.add(text + ": level " + std::to_string(level) + " is not " + std::to_string(w) + "x" + std::to_string(h));
            }
            const std::vector<std::uint8_t> data = Kit::bytes(static_cast<std::uint64_t>(260 + level), static_cast<std::size_t>(w * h * 4));
            api.glTexSubImage2D(glc::TEXTURE_2D, level, 0, 0, w, h, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
            const unsigned sub_error = api.drain_errors();
            const std::string difference = first_difference(data, read_image(api, glc::TEXTURE_2D, level, data.size()));
            if (sub_error != 0 || !difference.empty()) {
                problems.add(text + ": TexSubImage2D at level " + std::to_string(level) + " gave " + GlApi::error_name(sub_error) + " " + difference);
            }
        }
        api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), size.w, size.h, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
        GlTools::expect_error(problems, api, text + ": TexImage2D on an immutable texture", glc::INVALID_OPERATION);
        api.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, size.w, size.h);
        GlTools::expect_error(problems, api, text + ": a second TexStorage2D", glc::INVALID_OPERATION);
        api.glCopyTexImage2D(glc::TEXTURE_2D, 0, glc::RGBA8, 0, 0, 1, 1, 0);
        GlTools::expect_error(problems, api, text + ": CopyTexImage2D on an immutable texture", glc::INVALID_OPERATION);
        api.glDeleteTextures(1, &t);
    }
    return problems.outcome();
}

Outcome TextureChecks::max_size_limits(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    Problems problems;
    const int max2d = api.integer(glc::MAX_TEXTURE_SIZE);
    const int max3d = api.integer(glc::MAX_3D_TEXTURE_SIZE);
    const int max_layers = api.integer(glc::MAX_ARRAY_TEXTURE_LAYERS);
    const int max_cube = api.integer(glc::MAX_CUBE_MAP_TEXTURE_SIZE);
    context.measure("max_texture_size", max2d);
    context.measure("max_3d_texture_size", max3d);
    context.measure("max_array_layers", max_layers);
    context.measure("max_cube_size", max_cube);
    if (max2d <= 0 || max3d <= 0 || max_layers <= 0 || max_cube <= 0) {
        return Outcome::fail("a MAX_* texture limit is not positive: " + std::to_string(max2d) + ", " + std::to_string(max3d) + ", " + std::to_string(max_layers) + ", " + std::to_string(max_cube));
    }
    auto proxy_width = [&](unsigned proxy, int w, int h, int d) {
        (void)api.drain_errors();
        if (proxy == glc::PROXY_TEXTURE_3D || proxy == glc::PROXY_TEXTURE_2D_ARRAY) {
            api.glTexImage3D(proxy, 0, static_cast<int>(glc::RGBA8), w, h, d, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
        } else {
            api.glTexImage2D(proxy, 0, static_cast<int>(glc::RGBA8), w, h, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
        }
        const unsigned failure = api.drain_errors();
        return failure == 0 ? level_param(api, proxy, 0, glc::TEXTURE_WIDTH) : -static_cast<int>(failure);
    };
    struct Proxy {
        const char* name;
        unsigned proxy;
        int limit;
        int w;
        int h;
        int d;
        int over_w;
        int over_h;
        int over_d;
    };
    const Proxy proxies[] = {
        {"2D", glc::PROXY_TEXTURE_2D, max2d, max2d, 1, 1, max2d + 1, 1, 1},
        {"3D", glc::PROXY_TEXTURE_3D, max3d, max3d, 1, 1, max3d + 1, 1, 1},
        {"cube map", glc::PROXY_TEXTURE_CUBE_MAP, 0, 0, 0, 1, max_cube + 1, max_cube + 1, 1},
        {"2D array layers", glc::PROXY_TEXTURE_2D_ARRAY, max_layers, 1, 1, max_layers, 1, 1, max_layers + 1},
    };
    for (const Proxy& proxy : proxies) {
        const int ok = proxy.limit == 0 ? 1 : proxy_width(proxy.proxy, proxy.w, proxy.h, proxy.d);
        if (ok <= 0) {
            problems.add(std::string("the proxy query for a ") + proxy.name + " texture at the limit " + std::to_string(proxy.limit) + " reports " + std::to_string(ok));
        }
        const int over = proxy_width(proxy.proxy, proxy.over_w, proxy.over_h, proxy.over_d);
        if (over != 0) {
            problems.add(std::string("the proxy query for a ") + proxy.name + " texture one past the limit reports width " + std::to_string(over) + " instead of 0");
        }
    }
    const std::size_t line = static_cast<std::size_t>(max2d) * 4;
    {
        const std::vector<std::uint8_t> data = Kit::bytes(300, line);
        api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
        const unsigned wide = make_texture(api, glc::TEXTURE_2D);
        api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), max2d, 1, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        const unsigned wide_error = api.drain_errors();
        const std::vector<std::uint8_t> got = read_image(api, glc::TEXTURE_2D, 0, line);
        if (wide_error != 0 || !first_difference(data, got).empty()) {
            problems.add("a " + std::to_string(max2d) + "x1 texture: " + GlApi::error_name(wide_error) + " " + first_difference(data, got));
        }
        api.glDeleteTextures(1, &wide);
        const unsigned tall = make_texture(api, glc::TEXTURE_2D);
        api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), 1, max2d, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        const unsigned tall_error = api.drain_errors();
        const std::vector<std::uint8_t> back = read_image(api, glc::TEXTURE_2D, 0, line);
        if (tall_error != 0 || !first_difference(data, back).empty()) {
            problems.add("a 1x" + std::to_string(max2d) + " texture: " + GlApi::error_name(tall_error) + " " + first_difference(data, back));
        }
        api.glDeleteTextures(1, &tall);
        const unsigned over = make_texture(api, glc::TEXTURE_2D);
        api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), max2d + 1, 1, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
        GlTools::expect_error(problems, api, "TexImage2D one past GL_MAX_TEXTURE_SIZE", glc::INVALID_VALUE);
        api.glDeleteTextures(1, &over);
    }
    {
        const std::size_t layer_bytes = static_cast<std::size_t>(max_layers) * 4;
        const std::vector<std::uint8_t> data = Kit::bytes(301, layer_bytes);
        const unsigned layers = make_texture(api, glc::TEXTURE_2D_ARRAY);
        api.glTexImage3D(glc::TEXTURE_2D_ARRAY, 0, static_cast<int>(glc::RGBA8), 1, 1, max_layers, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        const unsigned layer_error = api.drain_errors();
        const std::vector<std::uint8_t> got = read_image(api, glc::TEXTURE_2D_ARRAY, 0, layer_bytes);
        if (layer_error != 0 || !first_difference(data, got).empty()) {
            problems.add("a 1x1x" + std::to_string(max_layers) + " array texture: " + GlApi::error_name(layer_error) + " " + first_difference(data, got));
        }
        api.glDeleteTextures(1, &layers);
        const unsigned over = make_texture(api, glc::TEXTURE_2D_ARRAY);
        api.glTexImage3D(glc::TEXTURE_2D_ARRAY, 0, static_cast<int>(glc::RGBA8), 1, 1, max_layers + 1, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
        GlTools::expect_error(problems, api, "TexImage3D with one layer past GL_MAX_ARRAY_TEXTURE_LAYERS", glc::INVALID_VALUE);
        api.glDeleteTextures(1, &over);
    }
    {
        const std::size_t volume_bytes = static_cast<std::size_t>(max3d) * 4;
        const std::vector<std::uint8_t> data = Kit::bytes(302, volume_bytes);
        const unsigned volume = make_texture(api, glc::TEXTURE_3D);
        api.glTexImage3D(glc::TEXTURE_3D, 0, static_cast<int>(glc::RGBA8), max3d, 1, 1, 0, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        const unsigned volume_error = api.drain_errors();
        const std::vector<std::uint8_t> got = read_image(api, glc::TEXTURE_3D, 0, volume_bytes);
        if (volume_error != 0 || !first_difference(data, got).empty()) {
            problems.add("a " + std::to_string(max3d) + "x1x1 3D texture: " + GlApi::error_name(volume_error) + " " + first_difference(data, got));
        }
        api.glDeleteTextures(1, &volume);
        const unsigned over = make_texture(api, glc::TEXTURE_3D);
        api.glTexImage3D(glc::TEXTURE_3D, 0, static_cast<int>(glc::RGBA8), max3d + 1, 1, 1, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
        GlTools::expect_error(problems, api, "TexImage3D one past GL_MAX_3D_TEXTURE_SIZE", glc::INVALID_VALUE);
        api.glDeleteTextures(1, &over);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
