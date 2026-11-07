#include "support/gl_target.hpp"

#include "core/text.hpp"

namespace lsw::suites {

bool GlTarget::create(const GlApi& api, int width, int height, unsigned internal_format, bool depth, std::string& error) {
    destroy();
    api_ = &api;
    width_ = width;
    height_ = height;
    api.drain_errors();
    api.glGenTextures(1, &texture_);
    api.glBindTexture(glc::TEXTURE_2D, texture_);
    const bool integer = internal_format == glc::R32UI || internal_format == glc::RGBA8UI || internal_format == glc::RGBA32UI ||
                         internal_format == glc::R8UI || internal_format == glc::R32I || internal_format == glc::RGBA8I;
    api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(internal_format), width, height, 0, integer ? glc::RGBA_INTEGER : glc::RGBA,
                     glc::UNSIGNED_BYTE, nullptr);
    api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
    api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<int>(glc::NEAREST));
    api.glGenFramebuffers(1, &fbo_);
    api.glBindFramebuffer(glc::FRAMEBUFFER, fbo_);
    api.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, texture_, 0);
    if (depth) {
        api.glGenRenderbuffers(1, &depth_);
        api.glBindRenderbuffer(glc::RENDERBUFFER, depth_);
        api.glRenderbufferStorage(glc::RENDERBUFFER, glc::DEPTH_COMPONENT24, width, height);
        api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, glc::RENDERBUFFER, depth_);
    }
    const unsigned status = api.glCheckFramebufferStatus(glc::FRAMEBUFFER);
    const unsigned failure = api.drain_errors();
    if (status != glc::FRAMEBUFFER_COMPLETE || failure != 0) {
        error = "render target " + std::to_string(width) + "x" + std::to_string(height) + " format 0x" + Text::hex(internal_format, 4) +
                ": framebuffer status 0x" + Text::hex(status, 4) + ", " + GlApi::error_name(failure);
        return false;
    }
    return true;
}

bool GlTarget::read_rgba(std::vector<std::uint8_t>& out, std::string& error) const {
    out.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4, 0xCD);
    api_->glBindFramebuffer(glc::READ_FRAMEBUFFER, fbo_);
    api_->glPixelStorei(glc::PACK_ALIGNMENT, 1);
    api_->glReadPixels(0, 0, width_, height_, glc::RGBA, glc::UNSIGNED_BYTE, out.data());
    const unsigned failure = api_->drain_errors();
    if (failure != 0) {
        error = "glReadPixels from the render target gave " + GlApi::error_name(failure);
        return false;
    }
    return true;
}

void GlTarget::destroy() {
    if (api_ == nullptr) {
        return;
    }
    api_->glBindFramebuffer(glc::FRAMEBUFFER, 0);
    if (fbo_ != 0) {
        api_->glDeleteFramebuffers(1, &fbo_);
    }
    if (texture_ != 0) {
        api_->glDeleteTextures(1, &texture_);
    }
    if (depth_ != 0) {
        api_->glDeleteRenderbuffers(1, &depth_);
    }
    fbo_ = texture_ = depth_ = 0;
    api_ = nullptr;
}

}  // namespace lsw::suites
