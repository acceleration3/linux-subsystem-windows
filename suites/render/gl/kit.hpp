#pragma once

#include "render/common/case.hpp"
#include "render/common/image.hpp"
#include "render/gl/api.hpp"
#include "render/platform/gl_platform.hpp"

#include <functional>
#include <string>
#include <vector>

namespace lsw::render::gl {
struct KitOptions {
    bool core = true;
    int major = 4;
    int minor = 6;
    unsigned width = 128;
    unsigned height = 128;
    bool window = false;

    [[nodiscard]] static KitOptions from(const CaseSpec& spec) {
        KitOptions options;
        options.core = !spec.compat();
        options.major = spec.major() != 0 ? static_cast<int>(spec.major()) : 4;
        options.minor = spec.major() != 0 ? static_cast<int>(spec.minor()) : 6;
        options.width = spec.width();
        options.height = spec.height();
        options.window = spec.uses_swapchain();
        return options;
    }
};

struct FormatInfo {
    GLenum internal = 0;
    GLenum format = 0;
    GLenum type = 0;
    PixelKind kind = PixelKind::Rgba8Unorm;
};

class Kit {
public:
    Kit() = default;
    Kit(const Kit&) = delete;
    Kit& operator=(const Kit&) = delete;
    ~Kit();

    [[nodiscard]] OpenResult open(platform::GlPlatform& host, const KitOptions& options, std::string& error);
    void close();

    [[nodiscard]] bool check(const char* where, std::string& error);
    [[nodiscard]] bool has_extension(const char* name) const;
    [[nodiscard]] int integer(GLenum name);
    [[nodiscard]] bool at_least(int wanted_major, int wanted_minor) const;

    [[nodiscard]] GLuint program(const char* vertex_source, const char* fragment_source, std::string& error);
    GLuint buffer(GLenum target, const void* data, std::size_t size, GLenum usage);
    GLuint vertex_array();
    [[nodiscard]] GLuint texture2d(unsigned width, unsigned height, GLenum internal, GLenum format, GLenum type,
                                   const std::vector<std::vector<std::uint8_t>>& levels, bool generate_mips);

    [[nodiscard]] static bool format_info(GLenum internal, FormatInfo& out);
    [[nodiscard]] GLint uniform(GLuint program, const char* name, std::string& error);

    [[nodiscard]] bool read_color(GLuint fbo, GLenum read_buffer, GLenum internal, unsigned width, unsigned height, Image& out,
                                  std::string& error);
    [[nodiscard]] bool read_depth(GLuint fbo, unsigned width, unsigned height, Image& out, std::string& error);
    [[nodiscard]] bool read_stencil(GLuint fbo, unsigned width, unsigned height, Image& out, std::string& error);

    void defer(std::function<void()> destroyer) { cleanups_.push_back(std::move(destroyer)); }

    Api gl;
    platform::GlPlatform* platform = nullptr;
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string uuid;
    int major = 0;
    int minor = 0;
    unsigned width = 0;
    unsigned height = 0;

private:
    std::vector<std::function<void()>> cleanups_;
};

struct TargetDesc {
    unsigned width = 128;
    unsigned height = 128;
    std::vector<GLenum> colors{glc::RGBA8};
    GLenum depth = 0;
    int samples = 1;
};

class Target {
public:
    [[nodiscard]] bool create(Kit& kit, const TargetDesc& desc, std::string& error);

    void bind(Kit& kit) const;
    [[nodiscard]] bool resolve(Kit& kit, std::string& error);

    [[nodiscard]] bool read_color(Kit& kit, std::size_t index, Image& out, std::string& error);
    [[nodiscard]] bool read_depth(Kit& kit, Image& out, std::string& error);
    [[nodiscard]] bool read_stencil(Kit& kit, Image& out, std::string& error);

    TargetDesc desc;
    GLuint fbo = 0;
    GLuint resolve_fbo = 0;
    std::vector<GLuint> color_buffers;
    std::vector<GLuint> resolve_textures;
    GLuint depth_buffer = 0;
};
}  // namespace lsw::render::gl
