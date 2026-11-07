#pragma once

#include "support/gl_api.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
class GlTarget {
public:
    GlTarget() = default;
    GlTarget(const GlTarget&) = delete;
    GlTarget& operator=(const GlTarget&) = delete;
    ~GlTarget() { destroy(); }

    [[nodiscard]] bool create(const GlApi& api, int width, int height, unsigned internal_format, bool depth,
                              std::string& error);

    void bind() const { api_->glBindFramebuffer(glc::FRAMEBUFFER, fbo_); }

    void destroy();

    [[nodiscard]] bool read_rgba(std::vector<std::uint8_t>& out, std::string& error) const;

    [[nodiscard]] unsigned fbo() const { return fbo_; }

    [[nodiscard]] unsigned texture() const { return texture_; }

    [[nodiscard]] int width() const { return width_; }

    [[nodiscard]] int height() const { return height_; }

private:
    const GlApi* api_ = nullptr;
    unsigned fbo_ = 0;
    unsigned texture_ = 0;
    unsigned depth_ = 0;
    int width_ = 0;
    int height_ = 0;
};
}  // namespace lsw::suites
