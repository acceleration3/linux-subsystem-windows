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
        return rig_.open(options, error);
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
};

}  // namespace

std::unique_ptr<GlPlatform> make_gl_platform(const GlPlatformConfig&, std::string&) { return std::make_unique<WglPlatform>(); }

}  // namespace lsw::render::platform
