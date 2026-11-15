#include "render/gl/api.hpp"
#include "render/platform/gl_platform.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace lsw::render::platform {

namespace {

using namespace lsw::render::gl;

constexpr EGLint kContextMajor = 0x3098;
constexpr EGLint kContextMinor = 0x30FB;
constexpr EGLint kContextProfile = 0x30FD;
constexpr EGLint kProfileCore = 0x00000001;
constexpr EGLint kProfileCompat = 0x00000002;

std::string hex(const std::uint8_t* bytes, std::size_t count) {
    std::string out;
    char text[4];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(text, sizeof text, "%02x", bytes[i]);
        out += text;
    }
    return out;
}

class EglPlatform : public GlPlatform {
public:
    explicit EglPlatform(std::string selector) : selector_(std::move(selector)) {}

    ~EglPlatform() override { destroy(); }

    bool open(const GlRequest& request, std::string& error) override {
        request_ = request;
        setenv("mesa_glthread", "false", 0);
        library_ = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
        if (library_ == nullptr) {
            error = "libEGL.so.1 cannot be loaded";
            return false;
        }
        auto* const query = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
        auto* const platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (query == nullptr || platform_display == nullptr) {
            error = "EGL lacks EGL_EXT_device_enumeration or EGL_EXT_platform_device";
            return false;
        }
        EGLDeviceEXT devices[16];
        EGLint found = 0;
        if (query(16, devices, &found) != EGL_TRUE || found == 0) {
            error = "eglQueryDevicesEXT found no device";
            return false;
        }
        std::string seen;
        for (EGLint i = 0; i < found && context_ == EGL_NO_CONTEXT; ++i) {
            EGLDisplay display = platform_display(EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr);
            if (display == EGL_NO_DISPLAY || eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
                continue;
            }
            if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
                eglTerminate(display);
                continue;
            }
            const EGLint attributes[] = {kContextMajor, request.major, kContextMinor, request.minor, kContextProfile,
                                         request.core ? kProfileCore : kProfileCompat, EGL_NONE};
            EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
            if (context == EGL_NO_CONTEXT || eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) != EGL_TRUE) {
                if (context != EGL_NO_CONTEXT) {
                    eglDestroyContext(display, context);
                }
                eglTerminate(display);
                continue;
            }
            using GetBytev = void (*)(unsigned, std::uint8_t*);
            auto* const bytev = reinterpret_cast<GetBytev>(eglGetProcAddress("glGetUnsignedBytevEXT"));
            std::uint8_t uuid[16] = {};
            if (bytev != nullptr) {
                bytev(glc::DEVICE_UUID_EXT, uuid);
            }
            const std::string text = hex(uuid, 16);
            seen += (seen.empty() ? "" : "; ") + text;
            const bool take = selector_.empty() || selector_ == "auto" || selector_.rfind("uuid:", 0) != 0 || selector_.substr(5) == text;
            if (take) {
                display_ = display;
                context_ = context;
                uuid_ = text;
            } else {
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                eglDestroyContext(display, context);
                eglTerminate(display);
            }
        }
        if (context_ == EGL_NO_CONTEXT) {
            error = "no EGL device matches the requested GPU (saw UUIDs: " + seen + ")";
            return false;
        }
        if (request.window && !make_stand_in(request, error)) {
            return false;
        }
        return true;
    }

    bool create_shared_context(std::string& error) override {
        const EGLint attributes[] = {kContextMajor, request_.major, kContextMinor, request_.minor, kContextProfile,
                                     request_.core ? kProfileCore : kProfileCompat, EGL_NONE};
        second_ = eglCreateContext(display_, EGL_NO_CONFIG_KHR, context_, attributes);
        if (second_ == EGL_NO_CONTEXT) {
            error = "eglCreateContext for the shared context failed";
            return false;
        }
        return true;
    }

    bool make_current(unsigned index, std::string& error) override {
        EGLContext target = index == 0 ? context_ : second_;
        if (target == EGL_NO_CONTEXT || eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, target) != EGL_TRUE) {
            error = "eglMakeCurrent failed for context " + std::to_string(index);
            return false;
        }
        return true;
    }

    void* get_proc(const char* name) override { return reinterpret_cast<void*>(eglGetProcAddress(name)); }

    std::string device_uuid() override { return uuid_; }

    unsigned back_framebuffer() override { return fbo_; }

    unsigned back_read_buffer() override { return fbo_ != 0 ? glc::COLOR_ATTACHMENT0 : glc::BACK; }

    bool swap(std::string&) override {
        if (flush_ != nullptr) {
            flush_();
        }
        return true;
    }

private:
    bool make_stand_in(const GlRequest& request, std::string& error) {
        using GenFn = void(LSW_GLAPI*)(GLsizei, GLuint*);
        using BindFn = void(LSW_GLAPI*)(GLenum, GLuint);
        using StorageFn = void(LSW_GLAPI*)(GLenum, GLenum, GLsizei, GLsizei);
        using AttachFn = void(LSW_GLAPI*)(GLenum, GLenum, GLenum, GLuint);
        using StatusFn = GLenum(LSW_GLAPI*)(GLenum);
        auto gen_fbo = reinterpret_cast<GenFn>(get_proc("glGenFramebuffers"));
        auto gen_rb = reinterpret_cast<GenFn>(get_proc("glGenRenderbuffers"));
        auto bind_fbo = reinterpret_cast<BindFn>(get_proc("glBindFramebuffer"));
        auto bind_rb = reinterpret_cast<BindFn>(get_proc("glBindRenderbuffer"));
        auto storage = reinterpret_cast<StorageFn>(get_proc("glRenderbufferStorage"));
        auto attach = reinterpret_cast<AttachFn>(get_proc("glFramebufferRenderbuffer"));
        auto status = reinterpret_cast<StatusFn>(get_proc("glCheckFramebufferStatus"));
        flush_ = reinterpret_cast<void(LSW_GLAPI*)()>(get_proc("glFlush"));
        if (!gen_fbo || !gen_rb || !bind_fbo || !bind_rb || !storage || !attach || !status) {
            error = "framebuffer entry points missing for the presentation stand-in";
            return false;
        }
        GLuint renderbuffers[2] = {};
        gen_fbo(1, &fbo_);
        gen_rb(2, renderbuffers);
        bind_fbo(glc::FRAMEBUFFER, fbo_);
        bind_rb(glc::RENDERBUFFER, renderbuffers[0]);
        storage(glc::RENDERBUFFER, glc::RGBA8, static_cast<GLsizei>(request.width), static_cast<GLsizei>(request.height));
        attach(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::RENDERBUFFER, renderbuffers[0]);
        bind_rb(glc::RENDERBUFFER, renderbuffers[1]);
        storage(glc::RENDERBUFFER, glc::DEPTH24_STENCIL8, static_cast<GLsizei>(request.width), static_cast<GLsizei>(request.height));
        attach(glc::FRAMEBUFFER, glc::DEPTH_STENCIL_ATTACHMENT, glc::RENDERBUFFER, renderbuffers[1]);
        if (status(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
            error = "presentation stand-in framebuffer incomplete";
            return false;
        }
        return true;
    }

    void destroy() {
        if (display_ != nullptr) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (second_ != EGL_NO_CONTEXT) {
                eglDestroyContext(display_, second_);
            }
            if (context_ != EGL_NO_CONTEXT) {
                eglDestroyContext(display_, context_);
            }
            eglTerminate(display_);
        }
        display_ = nullptr;
        context_ = EGL_NO_CONTEXT;
        second_ = EGL_NO_CONTEXT;
    }

    std::string selector_;
    std::string uuid_;
    void* library_ = nullptr;
    EGLDisplay display_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLContext second_ = EGL_NO_CONTEXT;
    GlRequest request_;
    GLuint fbo_ = 0;
    void(LSW_GLAPI* flush_)() = nullptr;
};

}  // namespace

std::unique_ptr<GlPlatform> make_gl_platform(const GlPlatformConfig& config, std::string&) {
    return std::make_unique<EglPlatform>(config.selector);
}

}  // namespace lsw::render::platform
