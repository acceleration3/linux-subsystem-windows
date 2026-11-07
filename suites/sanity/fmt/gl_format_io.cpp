#include "sanity/fmt/gl_format_io.hpp"

namespace lsw::suites::sanity::fmt {

namespace {

constexpr unsigned kInternalformatSupported = 0x826F;
constexpr unsigned kTexture2dQuery = glc::TEXTURE_2D;

void clear_errors(const GlApi& api) {
    api.drain_errors();
}

}  // namespace

bool GlFormatIo::reported_supported(const GlApi& api, unsigned internal) {
    if (api.glGetInternalformativ != nullptr && api.supports(4, 2, "GL_ARB_internalformat_query")) {
        clear_errors(api);
        int supported = 0;
        api.glGetInternalformativ(kTexture2dQuery, internal, kInternalformatSupported, 1, &supported);
        if (api.glGetError() == 0) {
            return supported != 0;
        }
    }
    return api.at_least(3, 1);
}

GlFormatIo::Result GlFormatIo::upload_read(const GlApi& api, const FormatEntry& entry, int width, int height, const Bytes& in, Bytes& out,
                                           std::string& error) {
    clear_errors(api);
    unsigned texture = 0;
    api.glGenTextures(1, &texture);
    api.glBindTexture(glc::TEXTURE_2D, texture);
    api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
    api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<int>(glc::NEAREST));
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
    api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(entry.gl_internal), width, height, 0, entry.gl_format, entry.gl_type, in.data());
    const unsigned upload_error = api.glGetError();
    Result result = Result::Done;
    if (upload_error != 0) {
        error = "glTexImage2D failed with " + GlApi::error_name(upload_error);
        result = upload_error == glc::INVALID_ENUM || upload_error == glc::INVALID_VALUE ? Result::Unsupported : Result::Failed;
    } else {
        out.assign(in.size(), 0xCD);
        api.glGetTexImage(glc::TEXTURE_2D, 0, entry.gl_format, entry.gl_type, out.data());
        const unsigned read_error = api.glGetError();
        if (read_error != 0) {
            error = "glGetTexImage failed with " + GlApi::error_name(read_error);
            result = Result::Failed;
        }
    }
    api.glFinish();
    api.glBindTexture(glc::TEXTURE_2D, 0);
    api.glDeleteTextures(1, &texture);
    return result;
}

GlFormatIo::Result GlFormatIo::clear_read(const GlApi& api, const FormatEntry& entry, const FormatSpec& spec, const double values[4],
                                          int width, int height, Bytes& out, std::string& error) {
    clear_errors(api);
    unsigned texture = 0;
    unsigned fbo = 0;
    api.glGenTextures(1, &texture);
    api.glBindTexture(glc::TEXTURE_2D, texture);
    api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
    api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<int>(glc::NEAREST));
    api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(entry.gl_internal), width, height, 0, entry.gl_format, entry.gl_type, nullptr);
    api.glGenFramebuffers(1, &fbo);
    api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    api.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, texture, 0);
    Result result = Result::Done;
    if (api.glGetError() != 0 || api.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
        error = "the format is not framebuffer renderable";
        result = Result::Unsupported;
    } else {
        if (api.glClampColor != nullptr) {
            api.glClampColor(glc::CLAMP_READ_COLOR, 0);
        }
        api.glDrawBuffer(glc::COLOR_ATTACHMENT0);
        api.glReadBuffer(glc::COLOR_ATTACHMENT0);
        if (spec.kind == FormatKind::Uint) {
            const unsigned v[4] = {static_cast<unsigned>(values[0]), static_cast<unsigned>(values[1]), static_cast<unsigned>(values[2]),
                                   static_cast<unsigned>(values[3])};
            api.glClearBufferuiv(glc::COLOR, 0, v);
        } else if (spec.kind == FormatKind::Sint) {
            const int v[4] = {static_cast<int>(values[0]), static_cast<int>(values[1]), static_cast<int>(values[2]), static_cast<int>(values[3])};
            api.glClearBufferiv(glc::COLOR, 0, v);
        } else {
            const float v[4] = {static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]),
                                static_cast<float>(values[3])};
            api.glClearBufferfv(glc::COLOR, 0, v);
        }
        out.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * texel_bytes(entry), 0xCD);
        api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
        api.glReadPixels(0, 0, width, height, entry.gl_format, entry.gl_type, out.data());
        const unsigned failure = api.glGetError();
        if (failure != 0) {
            error = "rendering or reading back failed with " + GlApi::error_name(failure);
            result = Result::Failed;
        }
    }
    api.glFinish();
    api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
    api.glDeleteFramebuffers(1, &fbo);
    api.glBindTexture(glc::TEXTURE_2D, 0);
    api.glDeleteTextures(1, &texture);
    return result;
}

}  // namespace lsw::suites::sanity::fmt
