#include "sanity/gl/context_checks.hpp"

#include "sanity/gl/kit.hpp"
#include "support/gl_program.hpp"
#include "support/gl_satellite.hpp"
#include "support/gl_scene.hpp"
#include "support/gl_target.hpp"

#include <chrono>

namespace lsw::suites::sanity::gl {

namespace {

struct Granted {
    bool created = false;
    DWORD error = 0;
    int major = 0;
    int minor = 0;
    int profile = 0;
    int flags = 0;
    int strategy = 0;
    unsigned reset_status = 0;
    std::string failure;
};

Granted try_create(GlRig& rig, const GlContextRequest& request, bool query_reset = false) {
    Granted result;
    GlContext created;
    std::string error;
    if (!created.create(rig.dc(), request, nullptr, error)) {
        result.error = created.create_error();
        result.failure = error;
        return result;
    }
    result.created = true;
    if (!created.make_current(rig.dc())) {
        result.created = false;
        result.failure = "wglMakeCurrent failed on the new context (error " + std::to_string(::GetLastError()) + ")";
        return result;
    }
    GlApi api;
    if (!api.load(GlLibrary::instance(), error)) {
        result.created = false;
        result.failure = error;
        GlContext::release();
        return result;
    }
    result.major = api.integer(glc::MAJOR_VERSION);
    result.minor = api.integer(glc::MINOR_VERSION);
    if (api.drain_errors() != 0 || result.major == 0) {
        result.major = api.major();
        result.minor = api.minor();
    }
    api.glGetIntegerv(glc::CONTEXT_PROFILE_MASK, &result.profile);
    api.glGetIntegerv(glc::CONTEXT_FLAGS, &result.flags);
    if (api.drain_errors() != 0) {
        result.profile = 0;
        result.flags = 0;
    }
    if (query_reset) {
        auto status = reinterpret_cast<unsigned(__stdcall*)()>(GlLibrary::instance().proc("glGetGraphicsResetStatusARB"));
        if (status == nullptr) {
            status = api.glGetGraphicsResetStatus;
        }
        if (status != nullptr) {
            result.reset_status = status();
        } else {
            result.reset_status = 0xFFFFFFFFu;
        }
        api.glGetIntegerv(glc::RESET_NOTIFICATION_STRATEGY, &result.strategy);
        (void)api.drain_errors();
    }
    GlContext::release();
    return result;
}

std::string error_text(DWORD error) {
    return "error 0x" + Text::hex(error, 4);
}

bool documented_version_error(DWORD error) {
    return error == static_cast<DWORD>(wglc::VERSION_ERROR) || error == static_cast<DWORD>(wglc::PROFILE_ERROR);
}

std::vector<std::uint8_t> test_pattern(std::uint64_t seed, std::size_t size) {
    return Kit::bytes(seed, size);
}

bool restore_rig(GlRig& rig, std::string& error) {
    return rig.make_current(error);
}

}  // namespace

Outcome ContextChecks::legacy_create(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    if (!rig.gl.at_least(1, 1)) {
        problems.add("GL_VERSION '" + rig.version + "' is below 1.1");
    }
    GlLibrary& library = GlLibrary::instance();
    for (int cycle = 0; cycle < 6; ++cycle) {
        GlContext::release();
        if (library.current_context() != nullptr) {
            problems.add("cycle " + std::to_string(cycle) + ": wglGetCurrentContext is not NULL after wglMakeCurrent(NULL,NULL)");
        }
        if (!rig.context.make_current(rig.dc())) {
            problems.add("cycle " + std::to_string(cycle) + ": wglMakeCurrent failed (error " + std::to_string(::GetLastError()) + ")");
            break;
        }
        if (library.current_context() != rig.context.handle() || library.current_dc() != rig.dc()) {
            problems.add("cycle " + std::to_string(cycle) + ": wglGetCurrentContext/DC do not return what was made current");
        }
        const int shade = 20 + cycle * 40;
        rig.gl.glClearColor(static_cast<float>(shade) / 255.0f, 0.0f, 1.0f, 1.0f);
        rig.gl.glClear(glc::COLOR_BUFFER_BIT);
        std::uint8_t pixel[4] = {};
        rig.gl.glPixelStorei(glc::PACK_ALIGNMENT, 1);
        rig.gl.glReadPixels(3, 3, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, pixel);
        const unsigned failure = rig.gl.drain_errors();
        if (failure != 0 || std::abs(pixel[0] - shade) > 1 || pixel[2] != 255) {
            problems.add("cycle " + std::to_string(cycle) + ": clear to red " + std::to_string(shade) + " reads (" + std::to_string(pixel[0]) + "," +
                         std::to_string(pixel[1]) + "," + std::to_string(pixel[2]) + ") with " + GlApi::error_name(failure));
        }
    }
    GlContext second;
    if (!second.create(rig.dc(), {}, nullptr, error)) {
        problems.add("a second wglCreateContext on the same DC failed: " + error);
    } else if (!second.make_current(rig.dc())) {
        problems.add("the second context cannot be made current (error " + std::to_string(::GetLastError()) + ")");
    } else {
        GlApi api;
        if (!api.load(library, error) || !api.at_least(1, 1)) {
            problems.add("the second context reports no usable GL_VERSION: " + error);
        }
        GlContext::release();
    }
    if (!restore_rig(rig, error)) {
        problems.add(error);
    }
    return problems.outcome();
}

Outcome ContextChecks::arb_version_profile(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    if (rig.wgl.create_context_attribs == nullptr || !rig.wgl.has(rig.dc(), "WGL_ARB_create_context")) {
        return Kit::not_advertised("WGL_ARB_create_context");
    }
    const bool profiles = rig.wgl.has(rig.dc(), "WGL_ARB_create_context_profile");
    Problems problems;
    struct Version {
        int major;
        int minor;
    };
    const Version versions[] = {{3, 2}, {3, 3}, {4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4}, {4, 5}, {4, 6}};
    int highest[3] = {0, 0, 0};
    for (int profile = 0; profile < 2; ++profile) {
        if (!profiles && profile == 1) {
            break;
        }
        const int mask = profile == 0 ? wglc::CONTEXT_CORE_PROFILE_BIT : wglc::CONTEXT_COMPATIBILITY_PROFILE_BIT;
        const char* name = profile == 0 ? "core" : "compatibility";
        for (const Version& version : versions) {
            GlContextRequest request;
            request.major = version.major;
            request.minor = version.minor;
            request.profile = profiles ? mask : 0;
            const std::string text = std::string(name) + " " + std::to_string(version.major) + "." + std::to_string(version.minor);
            const Granted granted = try_create(rig, request);
            if (granted.created) {
                if (granted.major < version.major || (granted.major == version.major && granted.minor < version.minor)) {
                    problems.add(text + " granted a " + std::to_string(granted.major) + "." + std::to_string(granted.minor) + " context");
                }
                if (profiles && granted.major >= 3 && (granted.profile & mask) == 0) {
                    problems.add(text + " granted a context whose GL_CONTEXT_PROFILE_MASK is 0x" + Text::hex(static_cast<unsigned>(granted.profile), 2));
                }
                if (profiles && granted.major >= 3 && (granted.profile & ~mask) != 0) {
                    problems.add(text + " granted a context whose GL_CONTEXT_PROFILE_MASK 0x" + Text::hex(static_cast<unsigned>(granted.profile), 2) +
                                 " has bits besides the requested one");
                }
                highest[profile] = std::max(highest[profile], version.major * 10 + version.minor);
            } else if (!documented_version_error(granted.error)) {
                problems.add(text + " failed with " + error_text(granted.error) + " instead of ERROR_INVALID_VERSION_ARB or ERROR_INVALID_PROFILE_ARB: " + granted.failure);
            } else if (version.major * 10 + version.minor <= highest[profile]) {
                problems.add(text + " failed after a higher version was granted");
            }
        }
        for (const Version& version : versions) {
            if (version.major * 10 + version.minor < highest[profile]) {
                GlContextRequest request;
                request.major = version.major;
                request.minor = version.minor;
                request.profile = profiles ? mask : 0;
                if (!try_create(rig, request).created) {
                    problems.add(std::string(name) + " " + std::to_string(version.major) + "." + std::to_string(version.minor) +
                                 " is refused although " + std::to_string(highest[profile] / 10) + "." + std::to_string(highest[profile] % 10) + " is granted");
                }
            }
        }
    }
    context.measure("core.highest", highest[0]);
    context.measure("compatibility.highest", highest[1]);
    std::string restore_error;
    if (!restore_rig(rig, restore_error)) {
        problems.add(restore_error);
    }
    if (highest[0] == 0 && highest[1] == 0 && problems.empty()) {
        problems.add("no version from 3.2 to 4.6 is granted in either profile");
    }
    return problems.outcome();
}

Outcome ContextChecks::flags_granted_or_error(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    if (rig.wgl.create_context_attribs == nullptr) {
        return Kit::not_advertised("WGL_ARB_create_context");
    }
    const bool profiles = rig.wgl.has(rig.dc(), "WGL_ARB_create_context_profile");
    const bool robust = rig.wgl.has(rig.dc(), "WGL_ARB_create_context_robustness");
    Problems problems;
    struct Case {
        int flags;
        const char* name;
    };
    const Case cases[] = {{wglc::CONTEXT_DEBUG_BIT, "debug"},
                          {wglc::CONTEXT_FORWARD_COMPATIBLE_BIT, "forward-compatible"},
                          {wglc::CONTEXT_DEBUG_BIT | wglc::CONTEXT_FORWARD_COMPATIBLE_BIT, "debug+forward-compatible"},
                          {wglc::CONTEXT_ROBUST_ACCESS_BIT, "robust-access"},
                          {wglc::CONTEXT_DEBUG_BIT | wglc::CONTEXT_ROBUST_ACCESS_BIT, "debug+robust-access"}};
    for (const Case& item : cases) {
        GlContextRequest request;
        request.major = 3;
        request.minor = 3;
        request.profile = profiles ? wglc::CONTEXT_CORE_PROFILE_BIT : 0;
        request.flags = item.flags;
        const std::string text = std::string(item.name) + " 3.3 context";
        const bool wants_robust = (item.flags & wglc::CONTEXT_ROBUST_ACCESS_BIT) != 0;
        const Granted granted = try_create(rig, request);
        if (wants_robust && !robust) {
            if (granted.created) {
                problems.add(text + " is created although WGL_ARB_create_context_robustness is not advertised");
            }
            continue;
        }
        if (!granted.created) {
            problems.add(text + " is refused with " + error_text(granted.error) + ": " + granted.failure);
            continue;
        }
        const unsigned want_bits = ((item.flags & wglc::CONTEXT_DEBUG_BIT) != 0 ? glc::CONTEXT_FLAG_DEBUG_BIT : 0u) |
                                   ((item.flags & wglc::CONTEXT_FORWARD_COMPATIBLE_BIT) != 0 ? glc::CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT : 0u) |
                                   (wants_robust ? glc::CONTEXT_FLAG_ROBUST_ACCESS_BIT : 0u);
        if ((static_cast<unsigned>(granted.flags) & want_bits) != want_bits) {
            problems.add(text + " reports GL_CONTEXT_FLAGS 0x" + Text::hex(static_cast<unsigned>(granted.flags), 2) + ", missing 0x" +
                         Text::hex(want_bits & ~static_cast<unsigned>(granted.flags), 2));
        }
    }
    GlContextRequest forward;
    forward.major = 3;
    forward.minor = 3;
    forward.profile = profiles ? wglc::CONTEXT_CORE_PROFILE_BIT : 0;
    forward.flags = wglc::CONTEXT_FORWARD_COMPATIBLE_BIT;
    GlContext created;
    if (created.create(rig.dc(), forward, nullptr, error) && created.make_current(rig.dc())) {
        GlApi api;
        if (api.load(GlLibrary::instance(), error)) {
            (void)api.drain_errors();
            api.glLineWidth(2.0f);
            GlTools::expect_error(problems, api, "glLineWidth(2.0) in a forward-compatible context", glc::INVALID_VALUE);
            api.glBegin(glc::TRIANGLES);
            const unsigned begin_error = api.glGetError();
            api.glEnd();
            (void)api.drain_errors();
            context.measure("forward.glBegin_error", GlApi::error_name(begin_error));
        } else {
            problems.add(error);
        }
        GlContext::release();
    }
    std::string restore_error;
    if (!restore_rig(rig, restore_error)) {
        problems.add(restore_error);
    }
    return problems.outcome();
}

Outcome ContextChecks::robustness_reset(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    if (rig.wgl.create_context_attribs == nullptr) {
        return Kit::not_advertised("WGL_ARB_create_context");
    }
    const bool robust = rig.wgl.has(rig.dc(), "WGL_ARB_create_context_robustness");
    const bool profiles = rig.wgl.has(rig.dc(), "WGL_ARB_create_context_profile");
    Problems problems;
    if (!robust) {
        GlContextRequest request;
        request.flags = wglc::CONTEXT_ROBUST_ACCESS_BIT;
        request.reset_strategy = wglc::NO_RESET_NOTIFICATION;
        request.major = 3;
        request.minor = 3;
        if (try_create(rig, request).created) {
            problems.add("a robust-access context is created although WGL_ARB_create_context_robustness is not advertised");
        }
        std::string restore_error;
        if (!restore_rig(rig, restore_error)) {
            problems.add(restore_error);
        }
        return problems.outcome();
    }
    const int strategies[] = {wglc::NO_RESET_NOTIFICATION, wglc::LOSE_CONTEXT_ON_RESET};
    for (const int strategy : strategies) {
        GlContextRequest request;
        request.major = 3;
        request.minor = 3;
        request.profile = profiles ? wglc::CONTEXT_CORE_PROFILE_BIT : 0;
        request.flags = wglc::CONTEXT_ROBUST_ACCESS_BIT;
        request.reset_strategy = strategy;
        const std::string text = strategy == wglc::NO_RESET_NOTIFICATION ? "NO_RESET_NOTIFICATION" : "LOSE_CONTEXT_ON_RESET";
        const Granted granted = try_create(rig, request, true);
        if (!granted.created) {
            problems.add(text + " robust context is refused with " + error_text(granted.error) + ": " + granted.failure);
            continue;
        }
        if (granted.reset_status == 0xFFFFFFFFu) {
            problems.add(text + ": glGetGraphicsResetStatus(ARB) does not resolve in a robust context");
        } else if (granted.reset_status != glc::NO_ERROR_) {
            problems.add(text + ": glGetGraphicsResetStatus on a healthy context returned 0x" + Text::hex(granted.reset_status, 4));
        }
        if (static_cast<unsigned>(granted.strategy) != static_cast<unsigned>(strategy)) {
            problems.add(text + ": GL_RESET_NOTIFICATION_STRATEGY is 0x" + Text::hex(static_cast<unsigned>(granted.strategy), 4) + ", requested 0x" +
                         Text::hex(static_cast<unsigned>(strategy), 4));
        }
    }
    std::string restore_error;
    if (!restore_rig(rig, restore_error)) {
        problems.add(restore_error);
    }
    return problems.outcome();
}

Outcome ContextChecks::no_error(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    if (rig.wgl.create_context_attribs == nullptr) {
        return Kit::not_advertised("WGL_ARB_create_context");
    }
    const bool advertised = rig.wgl.has(rig.dc(), "WGL_ARB_create_context_no_error");
    const bool profiles = rig.wgl.has(rig.dc(), "WGL_ARB_create_context_profile");
    GlContextRequest request;
    request.major = 3;
    request.minor = 3;
    request.profile = profiles ? wglc::CONTEXT_CORE_PROFILE_BIT : 0;
    request.no_error = 1;
    Problems problems;
    GlContext created;
    if (!advertised) {
        if (created.create(rig.dc(), request, nullptr, error)) {
            problems.add("a no-error context is created although WGL_ARB_create_context_no_error is not advertised");
        } else if (created.create_error() == 0) {
            problems.add("a no-error context request fails without setting a Win32 error");
        }
        context.measure("no_error.create_error", static_cast<long long>(created.create_error()));
        created.destroy();
        std::string restore_error;
        if (!restore_rig(rig, restore_error)) {
            problems.add(restore_error);
        }
        return problems.outcome();
    }
    if (!created.create(rig.dc(), request, nullptr, error)) {
        return Outcome::fail("WGL_ARB_create_context_no_error is advertised but the request fails with error " + std::to_string(created.create_error()) + ": " + error);
    }
    if (!created.make_current(rig.dc())) {
        return Outcome::fail("wglMakeCurrent failed on the no-error context (error " + std::to_string(::GetLastError()) + ")");
    }
    GlApi api;
    if (!api.load(GlLibrary::instance(), error)) {
        return Outcome::fail(error);
    }
    int flags = 0;
    api.glGetIntegerv(glc::CONTEXT_FLAGS, &flags);
    if ((static_cast<unsigned>(flags) & glc::CONTEXT_FLAG_NO_ERROR_BIT) == 0) {
        problems.add("GL_CONTEXT_FLAGS 0x" + Text::hex(static_cast<unsigned>(flags), 2) + " lacks GL_CONTEXT_FLAG_NO_ERROR_BIT_KHR");
    }
    GlTarget target;
    GlScene scene(32, 32);
    scene.clear_color(GlTools::rgba(10, 20, 30, 255));
    scene.add({4, 4, 20, 12, GlTools::rgba(200, 100, 50, 255)});
    scene.add({10, 8, 30, 28, GlTools::rgba(1, 255, 128, 255)});
    if (!target.create(api, 32, 32, glc::RGBA8, false, error) || !scene.draw(api, error)) {
        problems.add(error);
    } else {
        std::vector<std::uint8_t> image;
        api.glFinish();
        if (!target.read_rgba(image, error)) {
            problems.add(error);
        } else {
            std::string detail;
            if (!GlTools::same_image(context, "no_error_scene", scene.expected(), image, 32, 32, 0, detail)) {
                problems.add(detail);
            }
        }
    }
    scene.release();
    target.destroy();
    GlContext::release();
    std::string restore_error;
    if (!restore_rig(rig, restore_error)) {
        problems.add(restore_error);
    }
    return problems.outcome();
}

Outcome ContextChecks::share_lists(Context& context) {
    Problems problems;
    for (int variant = 0; variant < 2; ++variant) {
        GlRig rig;
        GlRigOptions options;
        options.width = 64;
        options.height = 64;
        std::string error;
        const char* name = variant == 0 ? "wglShareLists" : "wglCreateContextAttribsARB share";
        if (variant == 1) {
            options.request.major = 3;
            options.request.minor = 3;
            GlRig probe;
            if (!Kit::open(probe, context, error)) {
                return Outcome::fail(error);
            }
            if (probe.wgl.create_context_attribs == nullptr) {
                context.note("wglCreateContextAttribsARB is not exposed; only the wglShareLists variant ran");
                continue;
            }
        }
        if (!rig.open(options, error)) {
            problems.add(std::string(name) + ": " + error);
            continue;
        }
        if (variant == 0) {
            rig.record(context);
        }
        GlSatellite other;
        if (!other.open(rig, true, 64, 64, error)) {
            problems.add(std::string(name) + ": the sharing context cannot be created: " + error);
            continue;
        }
        const GlApi& a = rig.gl;
        const std::vector<std::uint8_t> texels = test_pattern(11, 8 * 8 * 4);
        const std::vector<std::uint8_t> data = test_pattern(12, 256);
        unsigned texture = 0;
        unsigned buffer = 0;
        a.glGenTextures(1, &texture);
        a.glBindTexture(glc::TEXTURE_2D, texture);
        a.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
        a.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), 8, 8, 0, glc::RGBA, glc::UNSIGNED_BYTE, texels.data());
        a.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
        a.glGenBuffers(1, &buffer);
        a.glBindBuffer(glc::ARRAY_BUFFER, buffer);
        a.glBufferData(glc::ARRAY_BUFFER, 256, data.data(), glc::STATIC_DRAW);
        const unsigned shader = a.glCreateShader(glc::FRAGMENT_SHADER);
        GlProgram program;
        if (!program.build(a, GlScene::vertex_source(), GlScene::fragment_source(), error)) {
            problems.add(std::string(name) + ": " + error);
            continue;
        }
        a.glBindTexture(glc::TEXTURE_2D, 0);
        a.glBindBuffer(glc::ARRAY_BUFFER, 0);
        a.glFinish();
        const unsigned program_id = program.id();
        GlContext::release();
        if (!other.bind(error)) {
            problems.add(std::string(name) + ": " + error);
            continue;
        }
        const GlApi& b = other.gl;
        if (b.glIsTexture(texture) == 0 || b.glIsBuffer(buffer) == 0 || b.glIsShader(shader) == 0 || b.glIsProgram(program_id) == 0) {
            problems.add(std::string(name) + ": the texture, buffer, shader or program of the first context is not visible in the second (texture " +
                         std::to_string(b.glIsTexture(texture)) + ", buffer " + std::to_string(b.glIsBuffer(buffer)) + ", shader " +
                         std::to_string(b.glIsShader(shader)) + ", program " + std::to_string(b.glIsProgram(program_id)) + ")");
        } else {
            std::vector<std::uint8_t> read(8 * 8 * 4, 0xCD);
            b.glBindTexture(glc::TEXTURE_2D, texture);
            b.glPixelStorei(glc::PACK_ALIGNMENT, 1);
            b.glGetTexImage(glc::TEXTURE_2D, 0, glc::RGBA, glc::UNSIGNED_BYTE, read.data());
            if (read != texels) {
                problems.add(std::string(name) + ": the shared texture reads back different data in the second context");
            }
            std::vector<std::uint8_t> bytes(256, 0xCD);
            b.glBindBuffer(glc::COPY_READ_BUFFER, buffer);
            b.glGetBufferSubData(glc::COPY_READ_BUFFER, 0, 256, bytes.data());
            if (bytes != data) {
                problems.add(std::string(name) + ": the shared buffer reads back different data in the second context");
            }
            GlTarget target;
            unsigned vao = 0;
            unsigned vbo = 0;
            if (!target.create(b, 16, 16, glc::RGBA8, false, error)) {
                problems.add(error);
            } else {
                const float quad[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
                struct Vertex {
                    float x;
                    float y;
                    std::uint32_t rgba;
                };
                Vertex vertices[6];
                for (int i = 0; i < 6; ++i) {
                    vertices[i] = {quad[i * 2], quad[i * 2 + 1], GlTools::rgba(255, 0, 0, 255)};
                }
                b.glGenVertexArrays(1, &vao);
                b.glGenBuffers(1, &vbo);
                b.glBindVertexArray(vao);
                b.glBindBuffer(glc::ARRAY_BUFFER, vbo);
                b.glBufferData(glc::ARRAY_BUFFER, sizeof(vertices), vertices, glc::STATIC_DRAW);
                b.glEnableVertexAttribArray(0);
                b.glVertexAttribPointer(0, 2, glc::FLOAT, 0, sizeof(Vertex), nullptr);
                b.glEnableVertexAttribArray(1);
                b.glVertexAttribPointer(1, 4, glc::UNSIGNED_BYTE, 1, sizeof(Vertex), reinterpret_cast<const void*>(8));
                b.glViewport(0, 0, 16, 16);
                b.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                b.glClear(glc::COLOR_BUFFER_BIT);
                b.glUseProgram(program_id);
                b.glDrawArrays(glc::TRIANGLES, 0, 6);
                b.glUseProgram(0);
                std::vector<std::uint8_t> image;
                if (target.read_rgba(image, error)) {
                    if (image[(8 * 16 + 8) * 4] != 255 || image[(8 * 16 + 8) * 4 + 1] != 0) {
                        problems.add(std::string(name) + ": drawing with the program of the first context renders (" + std::to_string(image[(8 * 16 + 8) * 4]) +
                                     "," + std::to_string(image[(8 * 16 + 8) * 4 + 1]) + ") instead of red");
                    }
                } else {
                    problems.add(error);
                }
                b.glBindVertexArray(0);
                b.glDeleteVertexArrays(1, &vao);
                b.glDeleteBuffers(1, &vbo);
            }
            b.glBindTexture(glc::TEXTURE_2D, texture);
            b.glBindBuffer(glc::COPY_READ_BUFFER, buffer);
            b.glFinish();
        }
        GlContext::release();
        if (!rig.make_current(error)) {
            problems.add(error);
            continue;
        }
        a.glDeleteTextures(1, &texture);
        a.glDeleteBuffers(1, &buffer);
        a.glFinish();
        GlContext::release();
        if (!other.bind(error)) {
            problems.add(error);
            continue;
        }
        std::vector<std::uint8_t> after(8 * 8 * 4, 0xCD);
        b.drain_errors();
        b.glGetTexImage(glc::TEXTURE_2D, 0, glc::RGBA, glc::UNSIGNED_BYTE, after.data());
        const unsigned failure = b.drain_errors();
        if (failure != 0 || after != texels) {
            problems.add(std::string(name) + ": after the first context deletes the texture the second context's binding reads " + GlApi::error_name(failure) +
                         (after == texels ? "" : " with different data"));
        }
        std::vector<std::uint8_t> after_buffer(256, 0xCD);
        b.glGetBufferSubData(glc::COPY_READ_BUFFER, 0, 256, after_buffer.data());
        const unsigned buffer_failure = b.drain_errors();
        if (buffer_failure != 0 || after_buffer != data) {
            problems.add(std::string(name) + ": after the first context deletes the buffer the second context's binding reads " +
                         GlApi::error_name(buffer_failure) + (after_buffer == data ? "" : " with different data"));
        }
        b.glBindTexture(glc::TEXTURE_2D, 0);
        b.glBindBuffer(glc::COPY_READ_BUFFER, 0);
        GlContext::release();
        if (!rig.make_current(error)) {
            problems.add(error);
        }
        program.destroy();
        a.glDeleteShader(shader);
    }
    return problems.outcome();
}

Outcome ContextChecks::non_shared_isolation(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    GlSatellite unshared;
    GlSatellite shared;
    if (!unshared.open(rig, false, 64, 64, error) || !shared.open(rig, true, 64, 64, error)) {
        return Outcome::fail(error);
    }
    const GlApi& a = rig.gl;
    const std::vector<std::uint8_t> texels = test_pattern(21, 8 * 8 * 4);
    unsigned texture = 0;
    unsigned buffer = 0;
    unsigned vao = 0;
    unsigned fbo = 0;
    a.glGenTextures(1, &texture);
    a.glBindTexture(glc::TEXTURE_2D, texture);
    a.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    a.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), 8, 8, 0, glc::RGBA, glc::UNSIGNED_BYTE, texels.data());
    a.glGenBuffers(1, &buffer);
    a.glBindBuffer(glc::ARRAY_BUFFER, buffer);
    a.glBufferData(glc::ARRAY_BUFFER, 128, nullptr, glc::STATIC_DRAW);
    a.glGenVertexArrays(1, &vao);
    a.glBindVertexArray(vao);
    a.glGenFramebuffers(1, &fbo);
    a.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    a.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, texture, 0);
    const unsigned status = a.glCheckFramebufferStatus(glc::FRAMEBUFFER);
    a.glBindFramebuffer(glc::FRAMEBUFFER, 0);
    a.glBindVertexArray(0);
    a.glBindBuffer(glc::ARRAY_BUFFER, 0);
    a.glBindTexture(glc::TEXTURE_2D, 0);
    a.glFinish();
    Problems problems;
    if (status != glc::FRAMEBUFFER_COMPLETE) {
        return Outcome::fail("the first context cannot build a complete framebuffer for the probe: status 0x" + Text::hex(status, 4));
    }
    GlContext::release();
    if (!unshared.bind(error)) {
        return Outcome::fail(error);
    }
    {
        const GlApi& b = unshared.gl;
        if (b.glIsTexture(texture) != 0) {
            problems.add("glIsTexture(" + std::to_string(texture) + ") is true in a context outside the share group");
        }
        if (b.glIsBuffer(buffer) != 0) {
            problems.add("glIsBuffer(" + std::to_string(buffer) + ") is true in a context outside the share group");
        }
        (void)b.drain_errors();
        b.glBindTexture(glc::TEXTURE_2D, texture);
        const unsigned bind_error = b.glGetError();
        if (bind_error == 0) {
            int width = -1;
            b.glGetTexLevelParameteriv(glc::TEXTURE_2D, 0, glc::TEXTURE_WIDTH, &width);
            if (width != 0) {
                problems.add("binding the other group's texture name shows a level-0 width of " + std::to_string(width) + " instead of an empty texture");
            }
        } else if (bind_error != glc::INVALID_OPERATION) {
            problems.add("glBindTexture of a foreign name gave " + GlApi::error_name(bind_error));
        }
        b.glBindTexture(glc::TEXTURE_2D, 0);
        (void)b.drain_errors();
        b.glBindBuffer(glc::ARRAY_BUFFER, buffer);
        const unsigned buffer_error = b.glGetError();
        if (buffer_error == 0) {
            int size = -1;
            b.glGetBufferParameteriv(glc::ARRAY_BUFFER, glc::BUFFER_SIZE, &size);
            if (size != 0) {
                problems.add("binding the other group's buffer name shows a size of " + std::to_string(size) + " instead of an empty buffer");
            }
        } else if (buffer_error != glc::INVALID_OPERATION) {
            problems.add("glBindBuffer of a foreign name gave " + GlApi::error_name(buffer_error));
        }
        b.glBindBuffer(glc::ARRAY_BUFFER, 0);
        (void)b.drain_errors();
    }
    GlContext::release();
    if (!shared.bind(error)) {
        return Outcome::fail(error);
    }
    {
        const GlApi& c = shared.gl;
        if (c.glIsTexture(texture) == 0) {
            problems.add("the texture is not visible in a context of the same share group");
        }
        if (c.glIsVertexArray(vao) != 0) {
            problems.add("glIsVertexArray(" + std::to_string(vao) + ") is true in another context of the share group; vertex array objects are not shared");
        }
        if (c.glIsFramebuffer(fbo) != 0) {
            problems.add("glIsFramebuffer(" + std::to_string(fbo) + ") is true in another context of the share group; framebuffer objects are not shared");
        }
        (void)c.drain_errors();
        c.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
        const unsigned bind_error = c.glGetError();
        if (bind_error == 0) {
            if (c.glCheckFramebufferStatus(glc::FRAMEBUFFER) == glc::FRAMEBUFFER_COMPLETE) {
                problems.add("binding another context's framebuffer name yields its complete configuration");
            }
        } else if (bind_error != glc::INVALID_OPERATION) {
            problems.add("glBindFramebuffer of another context's name gave " + GlApi::error_name(bind_error));
        }
        c.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        (void)c.drain_errors();
        c.glBindVertexArray(vao);
        const unsigned vao_error = c.glGetError();
        if (vao_error != 0 && vao_error != glc::INVALID_OPERATION) {
            problems.add("glBindVertexArray of another context's name gave " + GlApi::error_name(vao_error));
        }
        c.glBindVertexArray(0);
        (void)c.drain_errors();
    }
    GlContext::release();
    if (!rig.make_current(error)) {
        problems.add(error);
    }
    return problems.outcome();
}

Outcome ContextChecks::many_contexts(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    Problems problems;
    GlLibrary& library = GlLibrary::instance();
    constexpr int kRounds = 256;
    const auto begin = std::chrono::steady_clock::now();
    GlContext::release();
    for (int round = 0; round < kRounds; ++round) {
        TestWindow window;
        if (!window.create(rig.window_config(64, 64, false), error)) {
            return Outcome::fail("round " + std::to_string(round) + ": " + error);
        }
        PIXELFORMATDESCRIPTOR pfd = rig.pfd;
        if (::SetPixelFormat(window.dc(), rig.pixel_format, &pfd) == FALSE) {
            return Outcome::fail("round " + std::to_string(round) + ": SetPixelFormat failed (error " + std::to_string(::GetLastError()) + ")");
        }
        HGLRC created = library.create_context(window.dc());
        if (created == nullptr || library.make_current(window.dc(), created) == FALSE) {
            return Outcome::fail("round " + std::to_string(round) + ": a context cannot be created or made current (error " + std::to_string(::GetLastError()) + ")");
        }
        GlApi api;
        if (!api.load(library, error)) {
            return Outcome::fail("round " + std::to_string(round) + ": " + error);
        }
        api.glClearColor(static_cast<float>(round % 256) / 255.0f, 0.0f, 0.0f, 1.0f);
        api.glClear(glc::COLOR_BUFFER_BIT);
        if (round % 32 == 0) {
            std::uint8_t pixel[4] = {};
            api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
            api.glReadPixels(1, 1, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, pixel);
            if (std::abs(pixel[0] - round % 256) > 1) {
                problems.add("round " + std::to_string(round) + ": clear reads " + std::to_string(pixel[0]) + ", expected " + std::to_string(round % 256));
            }
        }
        api.glFinish();
        library.make_current(nullptr, nullptr);
        library.delete_context(created);
    }
    const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
    context.measure("rounds", kRounds);
    context.measure("loop_ms", elapsed);
    if (!rig.make_current(error)) {
        return Outcome::fail("after " + std::to_string(kRounds) + " contexts the original context cannot be made current: " + error);
    }
    GlTarget target;
    GlScene scene(48, 48);
    scene.clear_color(GlTools::rgba(5, 6, 7, 255));
    scene.add({8, 8, 40, 24, GlTools::rgba(250, 120, 60, 255)});
    if (!target.create(rig.gl, 48, 48, glc::RGBA8, false, error) || !scene.draw(rig.gl, error)) {
        problems.add("after the loop: " + error);
    } else {
        std::vector<std::uint8_t> image;
        rig.gl.glFinish();
        std::string detail;
        if (!target.read_rgba(image, error)) {
            problems.add(error);
        } else if (!GlTools::same_image(context, "many_contexts_scene", scene.expected(), image, 48, 48, 0, detail)) {
            problems.add("after the loop: " + detail);
        }
    }
    scene.release();
    target.destroy();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
