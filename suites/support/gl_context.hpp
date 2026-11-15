#pragma once

#include "support/gl_library.hpp"
#include "support/wgl_api.hpp"

#include <string>

namespace lsw::suites {
struct GlContextRequest {
    int major = 0;
    int minor = 0;
    int profile = 0;
    int flags = 0;
    int reset_strategy = 0;
    int no_error = -1;
};

class GlContext {
public:
    GlContext() = default;
    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;
    ~GlContext() { destroy(); }

    [[nodiscard]] bool create(HDC dc, const GlContextRequest& request, HGLRC share, std::string& error);

    [[nodiscard]] bool make_current(HDC dc) const;

    void destroy();

    [[nodiscard]] HGLRC handle() const { return context_; }

    [[nodiscard]] DWORD create_error() const { return create_error_; }

    [[nodiscard]] bool legacy() const { return legacy_; }

    static void release();

private:
    HGLRC context_ = nullptr;
    DWORD create_error_ = 0;
    bool legacy_ = true;
};
}  // namespace lsw::suites
