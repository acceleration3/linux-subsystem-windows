#include "render/gl/api.hpp"
#include "render/platform/gl_platform.hpp"
#include "support/gl_rig.hpp"

#include <cstdio>

namespace lsw::render::platform {

namespace {

class WglPlatform : public GlPlatform {
public:
    bool open(const GlRequest& request, std::string& error) override {
        lsw::suites::GlRigOptions options;
        options.width = static_cast<int>(request.width);
        options.height = static_cast<int>(request.height);
        options.visible = true;
        options.request.major = request.major;
        options.request.minor = request.minor;
        options.request.profile = request.core ? 1 : 2;
        request_ = options.request;
        return rig_.open(options, error);
    }

    bool create_shared_context(std::string& error) override { return second_.create(rig_.dc(), request_, rig_.context.handle(), error); }

    bool make_current(unsigned index, std::string& error) override {
        const bool ok = index == 0 ? rig_.context.make_current(rig_.dc()) : second_.make_current(rig_.dc());
        if (!ok) {
            error = "wglMakeCurrent failed for context " + std::to_string(index);
        }
        return ok;
    }

    void* get_proc(const char* name) override { return lsw::suites::GlLibrary::instance().resolve(name); }

    std::string device_uuid() override {
        using GetBytev = void(__stdcall*)(unsigned, unsigned char*);
        auto* const bytev = reinterpret_cast<GetBytev>(get_proc("glGetUnsignedBytevEXT"));
        if (bytev == nullptr) {
            return {};
        }
        unsigned char uuid[16] = {};
        bytev(gl::glc::DEVICE_UUID_EXT, uuid);
        if (rig_.gl.drain_errors() != 0) {
            return {};
        }
        std::string out;
        char text[4];
        for (unsigned char byte : uuid) {
            std::snprintf(text, sizeof text, "%02x", byte);
            out += text;
        }
        return out;
    }

    std::string describe() override {
        PIXELFORMATDESCRIPTOR pfd{};
        if (::DescribePixelFormat(rig_.dc(), rig_.pixel_format, sizeof(pfd), &pfd) == 0) {
            return "pixel format " + std::to_string(rig_.pixel_format);
        }
        char text[160];
        std::snprintf(text, sizeof text, "pixel format %d flags 0x%lx color %d alpha %d depth %d stencil %d accum %d", rig_.pixel_format,
                      static_cast<unsigned long>(pfd.dwFlags), pfd.cColorBits, pfd.cAlphaBits, pfd.cDepthBits, pfd.cStencilBits, pfd.cAccumBits);
        return text;
    }

    unsigned back_framebuffer() override { return 0; }
    unsigned back_read_buffer() override { return gl::glc::BACK; }

    bool swap(std::string& error) override {
        if (!::SwapBuffers(rig_.dc())) {
            error = "SwapBuffers failed (error " + std::to_string(::GetLastError()) + ")";
            return false;
        }
        return true;
    }

private:
    lsw::suites::GlRig rig_;
    lsw::suites::GlContext second_;
    lsw::suites::GlContextRequest request_;
};

}  // namespace

std::unique_ptr<GlPlatform> make_gl_platform(const GlPlatformConfig&, std::string&) { return std::make_unique<WglPlatform>(); }

}  // namespace lsw::render::platform
