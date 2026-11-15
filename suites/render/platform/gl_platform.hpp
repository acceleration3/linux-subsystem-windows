#pragma once

#include <memory>
#include <string>

namespace lsw::render::platform {
struct GlRequest {
    bool core = true;
    int major = 4;
    int minor = 6;
    unsigned width = 128;
    unsigned height = 128;
    bool window = false;
};

struct GlPlatformConfig {
    std::string selector;
};

class GlPlatform {
public:
    virtual ~GlPlatform() = default;
    [[nodiscard]] virtual bool open(const GlRequest& request, std::string& error) = 0;
    [[nodiscard]] virtual void* get_proc(const char* name) = 0;
    [[nodiscard]] virtual std::string device_uuid() = 0;
    [[nodiscard]] virtual unsigned back_framebuffer() = 0;
    [[nodiscard]] virtual unsigned back_read_buffer() = 0;
    [[nodiscard]] virtual bool swap(std::string& error) = 0;
    [[nodiscard]] virtual bool create_shared_context(std::string& error) = 0;
    [[nodiscard]] virtual bool make_current(unsigned index, std::string& error) = 0;
    [[nodiscard]] virtual std::string describe() { return {}; }
};

[[nodiscard]] std::unique_ptr<GlPlatform> make_gl_platform(const GlPlatformConfig& config, std::string& error);
}  // namespace lsw::render::platform
