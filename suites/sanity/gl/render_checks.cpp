#include "sanity/gl/render_checks.hpp"

#include "sanity/gl/kit.hpp"
#include "support/gl_program.hpp"
#include "support/gl_scene.hpp"
#include "support/gl_target.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::gl {

namespace {

struct ColorFormat {
    unsigned internal;
    const char* name;
    int kind;
    float tolerance;
};

const ColorFormat kColorFormats[] = {
    {glc::RGBA8, "RGBA8", 0, 1.5f / 255.0f},        {glc::R8, "R8", 0, 1.5f / 255.0f},
    {glc::RG8, "RG8", 0, 1.5f / 255.0f},            {glc::RGBA16F, "RGBA16F", 0, 0.002f},
    {glc::RGBA32F, "RGBA32F", 0, 0.0001f},          {glc::RGB10_A2, "RGB10_A2", 0, 2.0f / 1023.0f},
    {glc::SRGB8_ALPHA8, "SRGB8_ALPHA8", 0, 1.5f / 255.0f}, {glc::R11F_G11F_B10F, "R11F_G11F_B10F", 0, 0.01f},
    {glc::RGBA8UI, "RGBA8UI", 1, 0.0f},             {glc::R32UI, "R32UI", 1, 0.0f},
    {glc::RGBA8I, "RGBA8I", 2, 0.0f},
};

int channels_of(unsigned internal) {
    switch (internal) {
    case glc::R8:
    case glc::R32UI: return 1;
    case glc::RG8: return 2;
    case glc::R11F_G11F_B10F: return 3;
    default: return 4;
    }
}

unsigned upload_format(unsigned internal) {
    switch (internal) {
    case glc::RGBA8UI:
    case glc::RGBA8I: return glc::RGBA_INTEGER;
    case glc::R32UI: return glc::RED_INTEGER;
    case glc::R8: return glc::RED;
    case glc::RG8: return glc::RG;
    default: return glc::RGBA;
    }
}

unsigned upload_type(unsigned internal) {
    switch (internal) {
    case glc::RGBA16F:
    case glc::RGBA32F:
    case glc::R11F_G11F_B10F: return glc::FLOAT;
    case glc::R32UI: return glc::UNSIGNED_INT;
    case glc::RGB10_A2: return glc::UNSIGNED_BYTE;
    default: return glc::UNSIGNED_BYTE;
    }
}

const char* status_name(unsigned status) {
    switch (status) {
    case glc::FRAMEBUFFER_COMPLETE: return "GL_FRAMEBUFFER_COMPLETE";
    case glc::FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
    case glc::FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
    case glc::FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
    case glc::FRAMEBUFFER_INCOMPLETE_READ_BUFFER: return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
    case glc::FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
    case glc::FRAMEBUFFER_UNSUPPORTED: return "GL_FRAMEBUFFER_UNSUPPORTED";
    default: return "framebuffer status";
    }
}

std::vector<GlRect> tile_rects() {
    std::vector<GlRect> rects;
    for (int i = 0; i < 16; ++i) {
        const int x = (i % 4) * 16 + 2;
        const int y = (i / 4) * 16 + 2;
        rects.push_back({x, y, x + 12, y + 12, GlTools::rgba(40 + i * 12, 255 - i * 14, i * 16, 255)});
    }
    return rects;
}

}  // namespace

Outcome RenderChecks::completeness_matrix(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 3, "GL_ARB_framebuffer_object")) {
        return Kit::not_advertised("OpenGL 3.3 framebuffer objects");
    }
    Problems problems;
    constexpr int kSide = 16;
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    auto attach_texture = [&](unsigned attachment, unsigned internal, unsigned format, unsigned type, unsigned& texture, int samples = 0) {
        api.glGenTextures(1, &texture);
        api.glBindTexture(glc::TEXTURE_2D, texture);
        api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
        (void)samples;
        api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(internal), kSide, kSide, 0, format, type, nullptr);
        api.glFramebufferTexture2D(glc::FRAMEBUFFER, attachment, glc::TEXTURE_2D, texture, 0);
    };
    auto attach_renderbuffer = [&](unsigned attachment, unsigned internal, unsigned& rb, int samples = 0) {
        api.glGenRenderbuffers(1, &rb);
        api.glBindRenderbuffer(glc::RENDERBUFFER, rb);
        if (samples > 0) {
            api.glRenderbufferStorageMultisample(glc::RENDERBUFFER, samples, internal, kSide, kSide);
        } else {
            api.glRenderbufferStorage(glc::RENDERBUFFER, internal, kSide, kSide);
        }
        api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, attachment, glc::RENDERBUFFER, rb);
    };
    int complete = 0;
    for (const ColorFormat& format : kColorFormats) {
        for (int as_renderbuffer = 0; as_renderbuffer < 2; ++as_renderbuffer) {
            const std::string text = std::string(format.name) + (as_renderbuffer != 0 ? " renderbuffer" : " texture");
            (void)api.drain_errors();
            unsigned fbo = 0;
            unsigned texture = 0;
            unsigned rb = 0;
            api.glGenFramebuffers(1, &fbo);
            api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
            if (as_renderbuffer != 0) {
                attach_renderbuffer(glc::COLOR_ATTACHMENT0, format.internal, rb);
            } else {
                attach_texture(glc::COLOR_ATTACHMENT0, format.internal, upload_format(format.internal), upload_type(format.internal), texture);
            }
            const unsigned status = api.glCheckFramebufferStatus(glc::FRAMEBUFFER);
            const unsigned setup_error = api.drain_errors();
            if (status != glc::FRAMEBUFFER_COMPLETE || setup_error != 0) {
                problems.add(text + ": glCheckFramebufferStatus returned " + status_name(status) + " (0x" + Text::hex(status, 4) + ") with " + GlApi::error_name(setup_error) +
                             ", the format is required to be color-renderable");
            } else {
                ++complete;
                api.glViewport(0, 0, kSide, kSide);
                api.glDisable(glc::FRAMEBUFFER_SRGB);
                if (format.kind == 0) {
                    const float clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};
                    api.glClearBufferfv(glc::COLOR, 0, clear);
                    float pixel[4] = {-1, -1, -1, -1};
                    api.glReadPixels(5, 6, 1, 1, glc::RGBA, glc::FLOAT, pixel);
                    const int n = channels_of(format.internal);
                    for (int c = 0; c < 4; ++c) {
                        const float want = c < n ? clear[c] : (c == 3 ? 1.0f : 0.0f);
                        if (std::fabs(pixel[c] - want) > format.tolerance) {
                            problems.add(text + ": clear channel " + std::to_string(c) + " reads " + std::to_string(pixel[c]) + ", expected " + std::to_string(want));
                            break;
                        }
                    }
                } else if (format.kind == 1) {
                    const unsigned clear[4] = {7, 9, 11, 13};
                    api.glClearBufferuiv(glc::COLOR, 0, clear);
                    unsigned pixel[4] = {99, 99, 99, 99};
                    api.glReadPixels(5, 6, 1, 1, glc::RGBA_INTEGER, glc::UNSIGNED_INT, pixel);
                    const int n = channels_of(format.internal);
                    for (int c = 0; c < 4; ++c) {
                        const unsigned want = c < n ? clear[c] : (c == 3 ? 1u : 0u);
                        if (pixel[c] != want) {
                            problems.add(text + ": integer clear channel " + std::to_string(c) + " reads " + std::to_string(pixel[c]) + ", expected " + std::to_string(want));
                            break;
                        }
                    }
                } else {
                    const int clear[4] = {-5, 6, -7, 8};
                    api.glClearBufferiv(glc::COLOR, 0, clear);
                    int pixel[4] = {99, 99, 99, 99};
                    api.glReadPixels(5, 6, 1, 1, glc::RGBA_INTEGER, glc::INT, pixel);
                    for (int c = 0; c < 4; ++c) {
                        if (pixel[c] != clear[c]) {
                            problems.add(text + ": signed integer clear channel " + std::to_string(c) + " reads " + std::to_string(pixel[c]) + ", expected " + std::to_string(clear[c]));
                            break;
                        }
                    }
                }
                if (format.internal == glc::RGBA8 || format.internal == glc::RGBA16F || format.internal == glc::SRGB8_ALPHA8) {
                    GlScene scene(kSide, kSide);
                    scene.clear_color(GlTools::rgba(10, 20, 30, 255));
                    scene.add({2, 3, 11, 9, GlTools::rgba(200, 100, 50, 255)});
                    std::string draw_error;
                    if (!scene.draw(api, draw_error)) {
                        problems.add(text + ": " + draw_error);
                    } else {
                        std::vector<std::uint8_t> image(kSide * kSide * 4);
                        api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
                        api.glReadPixels(0, 0, kSide, kSide, glc::RGBA, glc::UNSIGNED_BYTE, image.data());
                        std::string detail;
                        if (!GlTools::same_image(context, "fbo_" + std::string(format.name) + (as_renderbuffer != 0 ? "_rb" : "_tex"), scene.expected(), image, kSide, kSide, 1, detail)) {
                            problems.add(text + ": " + detail);
                        }
                    }
                    scene.release();
                }
                GlTools::expect_clean(problems, api, text + " rendering");
            }
            api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
            api.glDeleteFramebuffers(1, &fbo);
            api.glDeleteTextures(1, &texture);
            api.glDeleteRenderbuffers(1, &rb);
        }
    }
    struct DepthCase {
        unsigned internal;
        unsigned attachment;
        unsigned format;
        unsigned type;
        const char* name;
    };
    const DepthCase depths[] = {{glc::DEPTH_COMPONENT16, glc::DEPTH_ATTACHMENT, glc::DEPTH_COMPONENT, glc::FLOAT, "DEPTH_COMPONENT16"},
                                {glc::DEPTH_COMPONENT24, glc::DEPTH_ATTACHMENT, glc::DEPTH_COMPONENT, glc::FLOAT, "DEPTH_COMPONENT24"},
                                {glc::DEPTH_COMPONENT32F, glc::DEPTH_ATTACHMENT, glc::DEPTH_COMPONENT, glc::FLOAT, "DEPTH_COMPONENT32F"},
                                {glc::DEPTH24_STENCIL8, glc::DEPTH_STENCIL_ATTACHMENT, glc::DEPTH_STENCIL, glc::UNSIGNED_INT_24_8, "DEPTH24_STENCIL8"},
                                {glc::DEPTH32F_STENCIL8, glc::DEPTH_STENCIL_ATTACHMENT, glc::DEPTH_STENCIL, glc::FLOAT_32_UNSIGNED_INT_24_8_REV, "DEPTH32F_STENCIL8"},
                                {glc::STENCIL_INDEX8, glc::STENCIL_ATTACHMENT, glc::STENCIL_INDEX, glc::UNSIGNED_BYTE, "STENCIL_INDEX8"}};
    for (const DepthCase& depth : depths) {
        for (int as_renderbuffer = 0; as_renderbuffer < 2; ++as_renderbuffer) {
            if (as_renderbuffer == 0 && depth.internal == glc::STENCIL_INDEX8) {
                continue;
            }
            const std::string text = std::string(depth.name) + (as_renderbuffer != 0 ? " renderbuffer" : " texture") + " with an RGBA8 texture";
            (void)api.drain_errors();
            unsigned fbo = 0;
            unsigned color = 0;
            unsigned texture = 0;
            unsigned rb = 0;
            api.glGenFramebuffers(1, &fbo);
            api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
            attach_texture(glc::COLOR_ATTACHMENT0, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, color);
            if (as_renderbuffer != 0) {
                attach_renderbuffer(depth.attachment, depth.internal, rb);
            } else {
                attach_texture(depth.attachment, depth.internal, depth.format, depth.type, texture);
            }
            const unsigned status = api.glCheckFramebufferStatus(glc::FRAMEBUFFER);
            const unsigned setup_error = api.drain_errors();
            if (status != glc::FRAMEBUFFER_COMPLETE || setup_error != 0) {
                problems.add(text + ": status " + status_name(status) + " (0x" + Text::hex(status, 4) + ") with " + GlApi::error_name(setup_error));
            } else {
                ++complete;
                api.glViewport(0, 0, kSide, kSide);
                api.glClearColor(0, 0, 0, 1);
                api.glClearDepth(1.0);
                api.glClearStencil(0);
                api.glClear(glc::COLOR_BUFFER_BIT | glc::DEPTH_BUFFER_BIT | glc::STENCIL_BUFFER_BIT);
                GlTools::expect_clean(problems, api, text + " clear");
            }
            api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
            api.glDeleteFramebuffers(1, &fbo);
            api.glDeleteTextures(1, &color);
            api.glDeleteTextures(1, &texture);
            api.glDeleteRenderbuffers(1, &rb);
        }
    }
    auto expect_status = [&](const char* what, unsigned wanted, auto&& setup) {
        (void)api.drain_errors();
        unsigned fbo = 0;
        unsigned objects[4] = {};
        unsigned rbs[2] = {};
        api.glGenFramebuffers(1, &fbo);
        api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
        setup(objects, rbs);
        const unsigned status = api.glCheckFramebufferStatus(glc::FRAMEBUFFER);
        (void)api.drain_errors();
        if (status != wanted) {
            problems.add(std::string(what) + ": status 0x" + Text::hex(status, 4) + " (" + status_name(status) + "), expected " + status_name(wanted) + " (0x" + Text::hex(wanted, 4) + ")");
        } else if (wanted != glc::FRAMEBUFFER_COMPLETE) {
            api.glClear(glc::COLOR_BUFFER_BIT);
            GlTools::expect_error(problems, api, std::string(what) + ": glClear on the incomplete framebuffer", glc::INVALID_FRAMEBUFFER_OPERATION);
            std::uint8_t pixel[4];
            api.glReadPixels(0, 0, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, pixel);
            GlTools::expect_error(problems, api, std::string(what) + ": glReadPixels on the incomplete framebuffer", glc::INVALID_FRAMEBUFFER_OPERATION);
        }
        api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        api.glDeleteFramebuffers(1, &fbo);
        api.glDeleteTextures(4, objects);
        api.glDeleteRenderbuffers(2, rbs);
    };
    const bool no_default = !api.supports(4, 3, "GL_ARB_framebuffer_no_attachments");
    if (no_default) {
        expect_status("a framebuffer without attachments", glc::FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT, [&](unsigned*, unsigned*) {});
    }
    expect_status("a depth texture on COLOR_ATTACHMENT0", glc::FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                  [&](unsigned* t, unsigned*) { attach_texture(glc::COLOR_ATTACHMENT0, glc::DEPTH_COMPONENT24, glc::DEPTH_COMPONENT, glc::FLOAT, t[0]); });
    expect_status("an RGBA8 renderbuffer on DEPTH_ATTACHMENT", glc::FRAMEBUFFER_INCOMPLETE_ATTACHMENT, [&](unsigned* t, unsigned* r) {
        attach_texture(glc::COLOR_ATTACHMENT0, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, t[0]);
        attach_renderbuffer(glc::DEPTH_ATTACHMENT, glc::RGBA8, r[0]);
    });
    const bool es2_rules = api.supports(4, 1, "GL_ARB_ES2_compatibility");
    expect_status("a texture with no level 0 image", glc::FRAMEBUFFER_INCOMPLETE_ATTACHMENT, [&](unsigned* t, unsigned*) {
        api.glGenTextures(1, &t[0]);
        api.glBindTexture(glc::TEXTURE_2D, t[0]);
        api.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, t[0], 0);
    });
    expect_status("DrawBuffers naming an empty attachment", es2_rules ? glc::FRAMEBUFFER_COMPLETE : glc::FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER, [&](unsigned* t, unsigned*) {
        attach_texture(glc::COLOR_ATTACHMENT0, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, t[0]);
        const unsigned buffers[1] = {glc::COLOR_ATTACHMENT0 + 1};
        api.glDrawBuffers(1, buffers);
    });
    expect_status("ReadBuffer naming an empty attachment", es2_rules ? glc::FRAMEBUFFER_COMPLETE : glc::FRAMEBUFFER_INCOMPLETE_READ_BUFFER, [&](unsigned* t, unsigned*) {
        attach_texture(glc::COLOR_ATTACHMENT0, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, t[0]);
        api.glReadBuffer(glc::COLOR_ATTACHMENT0 + 1);
    });
    if (api.integer(glc::MAX_COLOR_ATTACHMENTS) >= 2 && api.integer(glc::MAX_SAMPLES) >= 2) {
        expect_status("a multisample renderbuffer next to a single-sample texture", glc::FRAMEBUFFER_INCOMPLETE_MULTISAMPLE, [&](unsigned* t, unsigned* r) {
            attach_texture(glc::COLOR_ATTACHMENT0, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, t[0]);
            attach_renderbuffer(glc::COLOR_ATTACHMENT0 + 1, glc::RGBA8, r[0], 2);
        });
    }
    expect_status("attachments of different sizes", glc::FRAMEBUFFER_COMPLETE, [&](unsigned* t, unsigned* r) {
        attach_texture(glc::COLOR_ATTACHMENT0, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, t[0]);
        api.glGenRenderbuffers(1, &r[0]);
        api.glBindRenderbuffer(glc::RENDERBUFFER, r[0]);
        api.glRenderbufferStorage(glc::RENDERBUFFER, glc::DEPTH_COMPONENT24, kSide * 2, kSide / 2);
        api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, glc::RENDERBUFFER, r[0]);
    });
    context.measure("complete_configurations", complete);
    return problems.outcome();
}

Outcome RenderChecks::blit(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 128, 128)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 0, "GL_ARB_framebuffer_object") || api.glBlitFramebuffer == nullptr) {
        return Kit::not_advertised("glBlitFramebuffer");
    }
    Problems problems;
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    auto upload_source = [&](int side, std::uint64_t seed, unsigned& texture, unsigned& fbo) {
        const std::vector<std::uint8_t> pixels = Kit::bytes(seed, static_cast<std::size_t>(side * side * 4));
        api.glGenTextures(1, &texture);
        api.glBindTexture(glc::TEXTURE_2D, texture);
        api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), side, side, 0, glc::RGBA, glc::UNSIGNED_BYTE, pixels.data());
        api.glGenFramebuffers(1, &fbo);
        api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
        api.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, texture, 0);
        return pixels;
    };
    auto read_target = [&](GlTarget& target) {
        std::vector<std::uint8_t> image;
        std::string read_error;
        if (!target.read_rgba(image, read_error)) {
            problems.add(read_error);
        }
        return image;
    };
    auto blit = [&](unsigned from, unsigned to, int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1, unsigned filter) {
        api.glBindFramebuffer(glc::READ_FRAMEBUFFER, from);
        api.glBindFramebuffer(glc::DRAW_FRAMEBUFFER, to);
        api.glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, glc::COLOR_BUFFER_BIT, filter);
    };
    unsigned source_texture = 0;
    unsigned source_fbo = 0;
    const std::vector<std::uint8_t> source = upload_source(16, 400, source_texture, source_fbo);
    auto at = [&](const std::vector<std::uint8_t>& image, int side, int x, int y, int c) {
        return static_cast<int>(image[(static_cast<std::size_t>(y) * static_cast<std::size_t>(side) + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)]);
    };
    struct Mapping {
        const char* name;
        int dx0;
        int dy0;
        int dx1;
        int dy1;
    };
    const Mapping mappings[] = {{"1:1", 0, 0, 16, 16}, {"flip x", 16, 0, 0, 16}, {"flip y", 0, 16, 16, 0}, {"flip both", 16, 16, 0, 0}};
    for (const Mapping& mapping : mappings) {
        GlTarget target;
        if (!target.create(api, 16, 16, glc::RGBA8, false, error)) {
            return Outcome::fail(error);
        }
        blit(source_fbo, target.fbo(), 0, 0, 16, 16, mapping.dx0, mapping.dy0, mapping.dx1, mapping.dy1, glc::NEAREST);
        const std::vector<std::uint8_t> image = read_target(target);
        std::vector<std::uint8_t> expected(image.size());
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const int sx = mapping.dx0 > mapping.dx1 ? 15 - x : x;
                const int sy = mapping.dy0 > mapping.dy1 ? 15 - y : y;
                for (int c = 0; c < 4; ++c) {
                    expected[(static_cast<std::size_t>(y) * 16 + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(at(source, 16, sx, sy, c));
                }
            }
        }
        std::string detail;
        if (!image.empty() && !GlTools::same_image(context, std::string("blit_") + mapping.name, expected, image, 16, 16, 0, detail)) {
            problems.add(std::string(mapping.name) + ": " + detail);
        }
    }
    {
        GlTarget target;
        if (!target.create(api, 32, 32, glc::RGBA8, false, error)) {
            return Outcome::fail(error);
        }
        blit(source_fbo, target.fbo(), 0, 0, 16, 16, 0, 0, 32, 32, glc::NEAREST);
        const std::vector<std::uint8_t> image = read_target(target);
        std::vector<std::uint8_t> expected(image.size());
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                for (int c = 0; c < 4; ++c) {
                    expected[(static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(at(source, 16, x / 2, y / 2, c));
                }
            }
        }
        std::string detail;
        if (!image.empty() && !GlTools::same_image(context, "blit_scale2", expected, image, 32, 32, 0, detail)) {
            problems.add("nearest 2x upscale: " + detail);
        }
        GlTarget linear;
        if (!linear.create(api, 32, 32, glc::RGBA8, false, error)) {
            return Outcome::fail(error);
        }
        blit(source_fbo, linear.fbo(), 0, 0, 16, 16, 0, 0, 32, 32, glc::LINEAR);
        const std::vector<std::uint8_t> smooth = read_target(linear);
        std::vector<std::uint8_t> reference(smooth.size());
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const float fx = (static_cast<float>(x) + 0.5f) / 2.0f - 0.5f;
                const float fy = (static_cast<float>(y) + 0.5f) / 2.0f - 0.5f;
                const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, 15);
                const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, 15);
                const int x1 = std::min(x0 + 1, 15);
                const int y1 = std::min(y0 + 1, 15);
                const float wx = std::clamp(fx - static_cast<float>(std::floor(fx)), 0.0f, 1.0f) * (fx < 0.0f ? 0.0f : 1.0f);
                const float wy = std::clamp(fy - static_cast<float>(std::floor(fy)), 0.0f, 1.0f) * (fy < 0.0f ? 0.0f : 1.0f);
                for (int c = 0; c < 4; ++c) {
                    const float top = static_cast<float>(at(source, 16, x0, y0, c)) * (1.0f - wx) + static_cast<float>(at(source, 16, x1, y0, c)) * wx;
                    const float bottom = static_cast<float>(at(source, 16, x0, y1, c)) * (1.0f - wx) + static_cast<float>(at(source, 16, x1, y1, c)) * wx;
                    reference[(static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)] =
                        static_cast<std::uint8_t>(std::lround(top * (1.0f - wy) + bottom * wy));
                }
            }
        }
        if (!smooth.empty() && !GlTools::same_image(context, "blit_linear2", reference, smooth, 32, 32, 1, detail)) {
            problems.add("linear 2x upscale: " + detail);
        }
    }
    {
        unsigned big_texture = 0;
        unsigned big_fbo = 0;
        const std::vector<std::uint8_t> big = upload_source(32, 401, big_texture, big_fbo);
        GlTarget target;
        if (!target.create(api, 16, 16, glc::RGBA8, false, error)) {
            return Outcome::fail(error);
        }
        blit(big_fbo, target.fbo(), 0, 0, 32, 32, 0, 0, 16, 16, glc::NEAREST);
        const std::vector<std::uint8_t> image = read_target(target);
        for (int y = 0; y < 16 && !image.empty(); ++y) {
            for (int x = 0; x < 16; ++x) {
                bool found = false;
                for (int oy = 0; oy < 2 && !found; ++oy) {
                    for (int ox = 0; ox < 2 && !found; ++ox) {
                        found = true;
                        for (int c = 0; c < 4; ++c) {
                            found = found && at(image, 16, x, y, c) == at(big, 32, x * 2 + ox, y * 2 + oy, c);
                        }
                    }
                }
                if (!found) {
                    problems.add("nearest 2x downscale: pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is none of the four source pixels of its block");
                    y = 16;
                    break;
                }
            }
        }
        api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        api.glDeleteFramebuffers(1, &big_fbo);
        api.glDeleteTextures(1, &big_texture);
    }
    {
        GlTarget target;
        if (!target.create(api, 16, 16, glc::RGBA8, false, error)) {
            return Outcome::fail(error);
        }
        api.glBindFramebuffer(glc::FRAMEBUFFER, target.fbo());
        api.glViewport(0, 0, 16, 16);
        api.glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        api.glClear(glc::COLOR_BUFFER_BIT);
        blit(source_fbo, target.fbo(), 4, 4, 12, 12, 0, 0, 8, 8, glc::NEAREST);
        const std::vector<std::uint8_t> image = read_target(target);
        std::vector<std::uint8_t> expected(image.size());
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                for (int c = 0; c < 4; ++c) {
                    expected[(static_cast<std::size_t>(y) * 16 + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)] =
                        (x < 8 && y < 8) ? static_cast<std::uint8_t>(at(source, 16, 4 + x, 4 + y, c)) : static_cast<std::uint8_t>(c == 1 || c == 3 ? 255 : 0);
                }
            }
        }
        std::string detail;
        if (!image.empty() && !GlTools::same_image(context, "blit_partial", expected, image, 16, 16, 0, detail)) {
            problems.add("partial blit: " + detail);
        }
    }
    const unsigned float_formats[] = {glc::RGBA16F, glc::RGBA32F};
    for (const unsigned format : float_formats) {
        GlTarget target;
        if (!target.create(api, 16, 16, format, false, error)) {
            problems.add(error);
            continue;
        }
        blit(source_fbo, target.fbo(), 0, 0, 16, 16, 0, 0, 16, 16, glc::NEAREST);
        GlTools::expect_clean(problems, api, "converting blit to a floating-point target");
        api.glBindFramebuffer(glc::READ_FRAMEBUFFER, target.fbo());
        std::vector<std::uint8_t> bytes(16 * 16 * 4, 0xCD);
        api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
        api.glReadPixels(0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, bytes.data());
        std::string detail;
        if (!GlTools::same_image(context, "blit_to_float", source, bytes, 16, 16, 1, detail)) {
            problems.add(std::string("blit RGBA8 to ") + (format == glc::RGBA16F ? "RGBA16F" : "RGBA32F") + ": " + detail);
        }
    }
    {
        api.glBindFramebuffer(glc::DRAW_FRAMEBUFFER, 0);
        api.glBindFramebuffer(glc::READ_FRAMEBUFFER, source_fbo);
        api.glDrawBuffer(glc::BACK);
        api.glBlitFramebuffer(0, 0, 16, 16, 0, 0, 16, 16, glc::COLOR_BUFFER_BIT, glc::NEAREST);
        api.glBindFramebuffer(glc::READ_FRAMEBUFFER, 0);
        api.glReadBuffer(glc::BACK);
        std::vector<std::uint8_t> bytes(16 * 16 * 4, 0xCD);
        api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
        api.glReadPixels(0, 0, 16, 16, glc::RGBA, glc::UNSIGNED_BYTE, bytes.data());
        std::string detail;
        GlTools::expect_clean(problems, api, "blit to the default framebuffer");
        const bool has_alpha = rig.pfd.cAlphaBits > 0;
        std::vector<std::uint8_t> expected = source;
        if (!has_alpha) {
            for (std::size_t i = 3; i < expected.size(); i += 4) {
                expected[i] = 255;
            }
        }
        if (!GlTools::same_image(context, "blit_default", expected, bytes, 16, 16, 0, detail)) {
            problems.add("blit to the default framebuffer: " + detail);
        }
    }
    if (api.integer(glc::MAX_SAMPLES) >= 4) {
        unsigned ms_fbo = 0;
        unsigned ms_rb = 0;
        api.glGenFramebuffers(1, &ms_fbo);
        api.glBindFramebuffer(glc::FRAMEBUFFER, ms_fbo);
        api.glGenRenderbuffers(1, &ms_rb);
        api.glBindRenderbuffer(glc::RENDERBUFFER, ms_rb);
        api.glRenderbufferStorageMultisample(glc::RENDERBUFFER, 4, glc::RGBA8, 16, 16);
        api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::RENDERBUFFER, ms_rb);
        GlScene scene(16, 16);
        scene.clear_color(GlTools::rgba(30, 60, 90, 255));
        scene.add({4, 2, 12, 14, GlTools::rgba(250, 120, 20, 255)});
        std::string draw_error;
        if (api.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE || !scene.draw(api, draw_error)) {
            problems.add("multisample render target: " + draw_error);
        } else {
            GlTarget resolved;
            if (!resolved.create(api, 16, 16, glc::RGBA8, false, error)) {
                return Outcome::fail(error);
            }
            blit(ms_fbo, resolved.fbo(), 0, 0, 16, 16, 0, 0, 16, 16, glc::NEAREST);
            GlTools::expect_clean(problems, api, "multisample resolve");
            const std::vector<std::uint8_t> image = read_target(resolved);
            std::string detail;
            if (!image.empty() && !GlTools::same_image(context, "blit_resolve", scene.expected(), image, 16, 16, 0, detail)) {
                problems.add("multisample resolve: " + detail);
            }
        }
        scene.release();
        api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        api.glDeleteFramebuffers(1, &ms_fbo);
        api.glDeleteRenderbuffers(1, &ms_rb);
    }
    api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
    api.glDeleteFramebuffers(1, &source_fbo);
    api.glDeleteTextures(1, &source_texture);
    return problems.outcome();
}

Outcome RenderChecks::core_vs_compat_equal(Context& context) {
    constexpr int kSide = 64;
    GlScene reference(kSide, kSide);
    reference.clear_color(GlTools::rgba(12, 34, 56, 255));
    reference.add({4, 4, 28, 20, GlTools::rgba(250, 40, 10, 255)});
    reference.add({20, 12, 60, 36, GlTools::rgba(10, 220, 90, 255)});
    reference.add({0, 40, 64, 48, GlTools::rgba(30, 30, 240, 255)});
    reference.add({50, 50, 63, 63, GlTools::rgba(255, 255, 0, 255)});
    const std::vector<std::uint8_t> expected = reference.expected();
    Problems problems;
    std::string error;
    std::vector<std::uint8_t> core_image;
    {
        GlRig probe;
        if (!Kit::open(probe, context, error)) {
            return Outcome::fail(error);
        }
        if (!Kit::core_profile_available(probe)) {
            return Kit::not_advertised("WGL_ARB_create_context_profile");
        }
    }
    {
        GlRig core;
        GlRigOptions options;
        options.width = 128;
        options.height = 128;
        options.request.major = 3;
        options.request.minor = 3;
        options.request.profile = wglc::CONTEXT_CORE_PROFILE_BIT;
        if (!core.open(options, error)) {
            return Outcome::fail("a 3.3 core context cannot be opened: " + error);
        }
        GlTarget target;
        GlScene scene(kSide, kSide);
        scene.clear_color(GlTools::rgba(12, 34, 56, 255));
        for (const GlRect& rect : reference.rects()) {
            scene.add(rect);
        }
        if (!target.create(core.gl, kSide, kSide, glc::RGBA8, false, error) || !scene.draw(core.gl, error) || !target.read_rgba(core_image, error)) {
            problems.add("core profile: " + error);
        }
        scene.release();
    }
    GlRig compat;
    if (!Kit::open(compat, context, error, 128, 128)) {
        return Outcome::fail(error);
    }
    const GlApi& api = compat.gl;
    std::string detail;
    if (!core_image.empty() && !GlTools::same_image(context, "core_scene", expected, core_image, kSide, kSide, 0, detail)) {
        problems.add("core VAO/VBO path: " + detail);
    }
    GlTarget target;
    if (!target.create(api, kSide, kSide, glc::RGBA8, false, error)) {
        return Outcome::fail(error);
    }
    auto render_compat = [&](bool arrays) {
        api.glBindFramebuffer(glc::FRAMEBUFFER, target.fbo());
        api.glViewport(0, 0, kSide, kSide);
        api.glClearColor(12.0f / 255.0f, 34.0f / 255.0f, 56.0f / 255.0f, 1.0f);
        api.glClear(glc::COLOR_BUFFER_BIT);
        api.glMatrixMode(glc::PROJECTION);
        api.glLoadIdentity();
        api.glMatrixMode(glc::MODELVIEW);
        api.glLoadIdentity();
        api.glUseProgram(0);
        std::vector<float> positions;
        std::vector<std::uint8_t> colors;
        for (const GlRect& rect : reference.rects()) {
            const float x0 = static_cast<float>(rect.x0) / 32.0f - 1.0f;
            const float x1 = static_cast<float>(rect.x1) / 32.0f - 1.0f;
            const float y0 = static_cast<float>(rect.y0) / 32.0f - 1.0f;
            const float y1 = static_cast<float>(rect.y1) / 32.0f - 1.0f;
            const float corners[12] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
            for (int v = 0; v < 6; ++v) {
                positions.push_back(corners[v * 2]);
                positions.push_back(corners[v * 2 + 1]);
                colors.push_back(static_cast<std::uint8_t>(rect.rgba & 0xFF));
                colors.push_back(static_cast<std::uint8_t>((rect.rgba >> 8) & 0xFF));
                colors.push_back(static_cast<std::uint8_t>((rect.rgba >> 16) & 0xFF));
                colors.push_back(255);
            }
        }
        if (arrays) {
            api.glEnableClientState(glc::VERTEX_ARRAY);
            api.glEnableClientState(glc::COLOR_ARRAY);
            api.glVertexPointer(2, glc::FLOAT, 0, positions.data());
            api.glColorPointer(4, glc::UNSIGNED_BYTE, 0, colors.data());
            api.glDrawArrays(glc::TRIANGLES, 0, static_cast<int>(positions.size() / 2));
            api.glDisableClientState(glc::VERTEX_ARRAY);
            api.glDisableClientState(glc::COLOR_ARRAY);
        } else {
            api.glBegin(glc::TRIANGLES);
            for (std::size_t v = 0; v < positions.size() / 2; ++v) {
                api.glColor4ub(colors[v * 4], colors[v * 4 + 1], colors[v * 4 + 2], 255);
                api.glVertex2f(positions[v * 2], positions[v * 2 + 1]);
            }
            api.glEnd();
        }
        api.glFinish();
    };
    for (int arrays = 0; arrays < 2; ++arrays) {
        render_compat(arrays != 0);
        std::vector<std::uint8_t> image;
        if (!target.read_rgba(image, error)) {
            problems.add(error);
            continue;
        }
        std::string compat_detail;
        if (!GlTools::same_image(context, arrays != 0 ? "compat_arrays" : "compat_immediate", expected, image, kSide, kSide, 0, compat_detail)) {
            problems.add(std::string(arrays != 0 ? "compatibility client vertex arrays" : "compatibility immediate mode") + ": " + compat_detail);
        } else if (!core_image.empty() && image != core_image) {
            problems.add("the compatibility render differs from the core render");
        }
    }
    return problems.outcome();
}

Outcome RenderChecks::instanced_indirect(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 128, 128)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.at_least(3, 3)) {
        return Kit::not_advertised("OpenGL 3.3");
    }
    constexpr int kSide = 64;
    const std::vector<GlRect> rects = tile_rects();
    GlScene reference(kSide, kSide);
    reference.clear_color(GlTools::rgba(8, 8, 8, 255));
    for (const GlRect& rect : rects) {
        reference.add(rect);
    }
    const std::vector<std::uint8_t> expected = reference.expected();
    const std::vector<GlScene::Vertex> vertices = reference.vertices();
    Problems problems;
    GlTarget target;
    if (!target.create(api, kSide, kSide, glc::RGBA8, false, error)) {
        return Outcome::fail(error);
    }
    std::string skipped;
    int ran = 0;
    auto check = [&](const char* name) {
        api.glFinish();
        std::vector<std::uint8_t> image;
        std::string read_error;
        const unsigned failure = api.drain_errors();
        if (failure != 0) {
            problems.add(std::string(name) + " gave " + GlApi::error_name(failure));
            return;
        }
        if (!target.read_rgba(image, read_error)) {
            problems.add(read_error);
            return;
        }
        std::string detail;
        if (!GlTools::same_image(context, std::string("draw_") + name, expected, image, kSide, kSide, 0, detail)) {
            problems.add(std::string(name) + ": " + detail);
        }
        ++ran;
    };
    auto begin_pass = [&]() {
        target.bind();
        api.glViewport(0, 0, kSide, kSide);
        api.glDisable(glc::BLEND);
        api.glDisable(glc::DEPTH_TEST);
        api.glClearColor(8.0f / 255.0f, 8.0f / 255.0f, 8.0f / 255.0f, 1.0f);
        api.glClear(glc::COLOR_BUFFER_BIT);
    };
    GlProgram flat;
    if (!flat.build(api, GlScene::vertex_source(), GlScene::fragment_source(), error)) {
        return Outcome::fail(error);
    }
    unsigned vao = 0;
    api.glGenVertexArrays(1, &vao);
    api.glBindVertexArray(vao);
    unsigned vbo = 0;
    api.glGenBuffers(1, &vbo);
    api.glBindBuffer(glc::ARRAY_BUFFER, vbo);
    api.glBufferData(glc::ARRAY_BUFFER, static_cast<std::ptrdiff_t>(vertices.size() * sizeof(GlScene::Vertex)), vertices.data(), glc::STATIC_DRAW);
    auto plain_layout = [&]() {
        api.glBindBuffer(glc::ARRAY_BUFFER, vbo);
        api.glEnableVertexAttribArray(0);
        api.glVertexAttribPointer(0, 2, glc::FLOAT, 0, sizeof(GlScene::Vertex), nullptr);
        api.glEnableVertexAttribArray(1);
        api.glVertexAttribPointer(1, 4, glc::UNSIGNED_BYTE, 1, sizeof(GlScene::Vertex), reinterpret_cast<const void*>(8));
        api.glVertexAttribDivisor(0, 0);
        api.glVertexAttribDivisor(1, 0);
    };
    plain_layout();
    begin_pass();
    flat.use();
    api.glDrawArrays(glc::TRIANGLES, 0, static_cast<int>(vertices.size()));
    check("plain");
    {
        GlProgram instanced;
        const std::string vertex = "#version 330 core\nlayout(location = 0) in vec2 corner;\nlayout(location = 1) in vec4 offset_size;\nlayout(location = 2) in vec4 color;\n"
                                   "out vec4 v_color;\nvoid main() { gl_Position = vec4(offset_size.xy + corner * offset_size.zw, 0.0, 1.0); v_color = color; }\n";
        if (!instanced.build(api, vertex, GlScene::fragment_source(), error)) {
            problems.add(error);
        } else {
            struct Instance {
                float x;
                float y;
                float w;
                float h;
                std::uint32_t rgba;
            };
            std::vector<Instance> data;
            for (const GlRect& rect : rects) {
                data.push_back({static_cast<float>(rect.x0) / 32.0f - 1.0f, static_cast<float>(rect.y0) / 32.0f - 1.0f, static_cast<float>(rect.x1 - rect.x0) / 32.0f,
                                static_cast<float>(rect.y1 - rect.y0) / 32.0f, rect.rgba});
            }
            const float corners[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
            unsigned corner_buffer = 0;
            unsigned instance_buffer = 0;
            api.glGenBuffers(1, &corner_buffer);
            api.glBindBuffer(glc::ARRAY_BUFFER, corner_buffer);
            api.glBufferData(glc::ARRAY_BUFFER, sizeof(corners), corners, glc::STATIC_DRAW);
            api.glEnableVertexAttribArray(0);
            api.glVertexAttribPointer(0, 2, glc::FLOAT, 0, 8, nullptr);
            api.glVertexAttribDivisor(0, 0);
            api.glGenBuffers(1, &instance_buffer);
            api.glBindBuffer(glc::ARRAY_BUFFER, instance_buffer);
            api.glBufferData(glc::ARRAY_BUFFER, static_cast<std::ptrdiff_t>(data.size() * sizeof(Instance)), data.data(), glc::STATIC_DRAW);
            api.glEnableVertexAttribArray(1);
            api.glVertexAttribPointer(1, 4, glc::FLOAT, 0, sizeof(Instance), nullptr);
            api.glVertexAttribDivisor(1, 1);
            api.glEnableVertexAttribArray(2);
            api.glVertexAttribPointer(2, 4, glc::UNSIGNED_BYTE, 1, sizeof(Instance), reinterpret_cast<const void*>(16));
            api.glVertexAttribDivisor(2, 1);
            begin_pass();
            instanced.use();
            api.glDrawArraysInstanced(glc::TRIANGLES, 0, 6, 16);
            check("DrawArraysInstanced");
            api.glVertexAttribDivisor(1, 0);
            api.glVertexAttribDivisor(2, 0);
            api.glDisableVertexAttribArray(2);
            api.glDeleteBuffers(1, &corner_buffer);
            api.glDeleteBuffers(1, &instance_buffer);
        }
    }
    plain_layout();
    flat.use();
    if (api.supports(3, 2, "GL_ARB_draw_elements_base_vertex") && api.glDrawElementsBaseVertex != nullptr) {
        std::vector<GlScene::Vertex> padded(8, GlScene::Vertex{9.0f, 9.0f, 0u});
        std::vector<std::uint32_t> indices;
        for (std::size_t i = 0; i < rects.size(); ++i) {
            const GlScene::Vertex* v = &vertices[i * 6];
            const std::uint32_t base = static_cast<std::uint32_t>(padded.size() - 8);
            padded.push_back(v[0]);
            padded.push_back(v[1]);
            padded.push_back(v[2]);
            padded.push_back(v[5]);
            const std::uint32_t quad[6] = {0, 1, 2, 0, 2, 3};
            for (const std::uint32_t q : quad) {
                indices.push_back(base + q);
            }
        }
        unsigned vertex_buffer = 0;
        unsigned index_buffer = 0;
        api.glGenBuffers(1, &vertex_buffer);
        api.glBindBuffer(glc::ARRAY_BUFFER, vertex_buffer);
        api.glBufferData(glc::ARRAY_BUFFER, static_cast<std::ptrdiff_t>(padded.size() * sizeof(GlScene::Vertex)), padded.data(), glc::STATIC_DRAW);
        api.glVertexAttribPointer(0, 2, glc::FLOAT, 0, sizeof(GlScene::Vertex), nullptr);
        api.glVertexAttribPointer(1, 4, glc::UNSIGNED_BYTE, 1, sizeof(GlScene::Vertex), reinterpret_cast<const void*>(8));
        api.glGenBuffers(1, &index_buffer);
        api.glBindBuffer(glc::ELEMENT_ARRAY_BUFFER, index_buffer);
        api.glBufferData(glc::ELEMENT_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(indices.size() * 4), indices.data(), glc::STATIC_DRAW);
        begin_pass();
        api.glDrawElementsBaseVertex(glc::TRIANGLES, static_cast<int>(indices.size()), glc::UNSIGNED_INT, nullptr, 8);
        check("DrawElementsBaseVertex");
        api.glBindBuffer(glc::ELEMENT_ARRAY_BUFFER, 0);
        api.glDeleteBuffers(1, &vertex_buffer);
        api.glDeleteBuffers(1, &index_buffer);
        plain_layout();
    } else {
        skipped += " DrawElementsBaseVertex";
    }
    if (api.supports(4, 0, "GL_ARB_draw_indirect") && api.glDrawArraysIndirect != nullptr) {
        struct Command {
            unsigned count;
            unsigned instances;
            unsigned first;
            unsigned base_instance;
        };
        std::vector<Command> commands;
        for (unsigned i = 0; i < 16; ++i) {
            commands.push_back({6, 1, i * 6, 0});
        }
        unsigned indirect = 0;
        api.glGenBuffers(1, &indirect);
        api.glBindBuffer(glc::DRAW_INDIRECT_BUFFER, indirect);
        api.glBufferData(glc::DRAW_INDIRECT_BUFFER, static_cast<std::ptrdiff_t>(commands.size() * sizeof(Command)), commands.data(), glc::STATIC_DRAW);
        begin_pass();
        for (std::size_t i = 0; i < commands.size(); ++i) {
            api.glDrawArraysIndirect(glc::TRIANGLES, reinterpret_cast<const void*>(i * sizeof(Command)));
        }
        check("DrawArraysIndirect");
        if (api.supports(4, 3, "GL_ARB_multi_draw_indirect") && api.glMultiDrawArraysIndirect != nullptr) {
            begin_pass();
            api.glMultiDrawArraysIndirect(glc::TRIANGLES, nullptr, 16, 0);
            check("MultiDrawArraysIndirect");
            struct ElementCommand {
                unsigned count;
                unsigned instances;
                unsigned first_index;
                int base_vertex;
                unsigned base_instance;
            };
            std::vector<std::uint32_t> indices(vertices.size());
            for (std::size_t i = 0; i < indices.size(); ++i) {
                indices[i] = static_cast<std::uint32_t>(i);
            }
            std::vector<ElementCommand> element_commands;
            for (unsigned i = 0; i < 16; ++i) {
                element_commands.push_back({6, 1, i * 6, 0, 0});
            }
            unsigned element_buffer = 0;
            unsigned element_indirect = 0;
            api.glGenBuffers(1, &element_buffer);
            api.glBindBuffer(glc::ELEMENT_ARRAY_BUFFER, element_buffer);
            api.glBufferData(glc::ELEMENT_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(indices.size() * 4), indices.data(), glc::STATIC_DRAW);
            api.glGenBuffers(1, &element_indirect);
            api.glBindBuffer(glc::DRAW_INDIRECT_BUFFER, element_indirect);
            api.glBufferData(glc::DRAW_INDIRECT_BUFFER, static_cast<std::ptrdiff_t>(element_commands.size() * sizeof(ElementCommand)), element_commands.data(), glc::STATIC_DRAW);
            begin_pass();
            api.glMultiDrawElementsIndirect(glc::TRIANGLES, glc::UNSIGNED_INT, nullptr, 16, 0);
            check("MultiDrawElementsIndirect");
            api.glBindBuffer(glc::ELEMENT_ARRAY_BUFFER, 0);
            api.glDeleteBuffers(1, &element_buffer);
            api.glDeleteBuffers(1, &element_indirect);
        } else {
            skipped += " MultiDraw*Indirect";
        }
        api.glBindBuffer(glc::DRAW_INDIRECT_BUFFER, 0);
        api.glDeleteBuffers(1, &indirect);
    } else {
        skipped += " DrawArraysIndirect MultiDraw*Indirect";
    }
    if (api.supports(4, 0, "GL_ARB_transform_feedback2") && api.glDrawTransformFeedback != nullptr && api.glGenTransformFeedbacks != nullptr) {
        GlProgram capture;
        const char* varyings[] = {"gl_Position", "v_color"};
        if (!capture.build_stages(api, GlScene::vertex_source(), "", varyings, 2, error)) {
            problems.add(error);
        } else {
            unsigned captured = 0;
            unsigned feedback = 0;
            api.glGenBuffers(1, &captured);
            api.glBindBuffer(glc::TRANSFORM_FEEDBACK_BUFFER, captured);
            api.glBufferData(glc::TRANSFORM_FEEDBACK_BUFFER, static_cast<std::ptrdiff_t>(vertices.size() * 32), nullptr, glc::STATIC_DRAW);
            api.glGenTransformFeedbacks(1, &feedback);
            api.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, feedback);
            api.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, captured);
            capture.use();
            plain_layout();
            api.glEnable(glc::RASTERIZER_DISCARD);
            api.glBeginTransformFeedback(glc::TRIANGLES);
            api.glDrawArrays(glc::TRIANGLES, 0, static_cast<int>(vertices.size()));
            api.glEndTransformFeedback();
            api.glDisable(glc::RASTERIZER_DISCARD);
            api.glBindBuffer(glc::ARRAY_BUFFER, captured);
            api.glVertexAttribPointer(0, 2, glc::FLOAT, 0, 32, nullptr);
            api.glVertexAttribPointer(1, 4, glc::FLOAT, 0, 32, reinterpret_cast<const void*>(16));
            begin_pass();
            flat.use();
            api.glDrawTransformFeedback(glc::TRIANGLES, feedback);
            check("DrawTransformFeedback");
            api.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, 0);
            api.glDeleteTransformFeedbacks(1, &feedback);
            api.glDeleteBuffers(1, &captured);
            plain_layout();
        }
    } else {
        skipped += " DrawTransformFeedback";
    }
    api.glUseProgram(0);
    api.glBindVertexArray(0);
    api.glDeleteVertexArrays(1, &vao);
    api.glDeleteBuffers(1, &vbo);
    context.measure("variants_run", ran);
    if (!skipped.empty()) {
        context.note("draw variants not advertised by this context:" + skipped);
    }
    return problems.outcome();
}

Outcome RenderChecks::large_batches(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 128, 128)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.at_least(3, 3)) {
        return Kit::not_advertised("OpenGL 3.3");
    }
    Problems problems;
    struct Phase {
        const char* name;
        int width;
        int height;
        int count;
        bool per_rect;
    };
    const Phase phases[] = {{"40000 draws", 256, 256, 40000, true}, {"3.1 million vertices", 1024, 512, 1024 * 512, false}};
    for (const Phase& phase : phases) {
        GlScene scene(phase.width, phase.height);
        scene.clear_color(GlTools::rgba(0, 0, 0, 255));
        for (int i = 0; i < phase.count; ++i) {
            const int x = i % phase.width;
            const int y = i / phase.width;
            scene.add({x, y, x + 1, y + 1, GlTools::rgba((x * 7 + y * 3) & 255, (x * 13 + y * 5 + 40) & 255, (x * 3 + y * 11 + 90) & 255, 255)});
        }
        GlTarget target;
        if (!target.create(api, phase.width, phase.height, glc::RGBA8, false, error)) {
            return Outcome::fail(error);
        }
        target.bind();
        const auto begin = std::chrono::steady_clock::now();
        if (!scene.draw(api, error, phase.per_rect)) {
            problems.add(std::string(phase.name) + ": " + error);
            continue;
        }
        unsigned waited = 0;
        const bool done = GlTools::wait_fence(api, 30'000'000'000ull, waited);
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
        context.measure(std::string(phase.name) + ".ms", ms);
        if (!done) {
            problems.add(std::string(phase.name) + ": the fence after the batch returned " + GlTools::wait_name(waited) + " after " + std::to_string(ms) + " ms");
            continue;
        }
        std::vector<std::uint8_t> image;
        if (!target.read_rgba(image, error)) {
            problems.add(error);
            continue;
        }
        std::string detail;
        if (!GlTools::same_image(context, std::string("batch_") + std::to_string(phase.count), scene.expected(), image, phase.width, phase.height, 0, detail)) {
            problems.add(std::string(phase.name) + ": " + detail);
        }
        scene.release();
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
