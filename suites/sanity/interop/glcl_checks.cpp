#include "sanity/interop/glcl_checks.hpp"

#include "sanity/cl/kit.hpp"
#include "sanity/interop/kit.hpp"
#include "support/cl_interop.hpp"
#include "support/gl_frame_draw.hpp"
#include "support/gl_rig.hpp"
#include "support/gl_tools.hpp"

#include <cstring>

namespace lsw::suites::sanity::interop {

namespace {

namespace clkit = lsw::suites::sanity::cl;

enum class Open { Ready, Unsupported, Failed };

struct Bench {
    GlRig gl;
    ClRig cl;
    ClInterop interop;
    std::string detail;
    cl_context_properties props[7] = {};

    Open open(Context& context, int size, bool require_extension = true) {
        std::string error;
        GlRigOptions options;
        options.width = size;
        options.height = size;
        options.visible = false;
        if (!gl.open(options, error)) {
            detail = error;
            return Open::Failed;
        }
        gl.record(context);
        ClApi api;
        ClDeviceList list;
        if (!ClRig::list_devices(api, list, error)) {
            detail = "OpenCL: " + error;
            return Open::Failed;
        }
        const ClDeviceRef* pick = &list.refs.front();
        for (const ClDeviceRef& ref : list.refs) {
            if (ref.adapter.luid_key() == gl.adapter.luid_key()) {
                pick = &ref;
            }
        }
        ClRigOptions rig_options;
        rig_options.context = false;
        if (!cl.open_ref(*pick, rig_options, error)) {
            detail = pick->label() + ": " + error;
            return Open::Failed;
        }
        interop.load(cl);
        if (require_extension && !cl.has_extension("cl_khr_gl_sharing")) {
            detail = "cl_khr_gl_sharing is not advertised by " + pick->label();
            return Open::Unsupported;
        }
        props[0] = CL_GL_CONTEXT_KHR;
        props[1] = reinterpret_cast<cl_context_properties>(GlLibrary::instance().current_context());
        props[2] = CL_WGL_HDC_KHR;
        props[3] = reinterpret_cast<cl_context_properties>(GlLibrary::instance().current_dc());
        props[4] = CL_CONTEXT_PLATFORM;
        props[5] = reinterpret_cast<cl_context_properties>(cl.platform);
        props[6] = 0;
        if (!require_extension) {
            return Open::Ready;
        }
        if (interop.GetGLContextInfo == nullptr || interop.CreateFromGLBuffer == nullptr || interop.CreateFromGLTexture == nullptr || interop.CreateFromGLRenderbuffer == nullptr ||
            interop.AcquireGLObjects == nullptr || interop.ReleaseGLObjects == nullptr) {
            detail = "cl_khr_gl_sharing is advertised but an entry point does not resolve";
            return Open::Failed;
        }
        cl_device_id current = nullptr;
        std::size_t returned = 0;
        const cl_int info = interop.GetGLContextInfo(props, CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR, sizeof(current), &current, &returned);
        if (info == CL_SUCCESS && current != nullptr) {
            cl.device = current;
        }
        cl_int code = CL_SUCCESS;
        cl.context = cl.api.CreateContext(props, 1, &cl.device, nullptr, nullptr, &code);
        if (cl.context == nullptr || code != CL_SUCCESS) {
            detail = "clCreateContext with the current GL context failed: " + ClTools::error_name(code);
            cl.context = nullptr;
            return Open::Failed;
        }
        cl.queue = cl.create_queue(0, code);
        if (cl.queue == nullptr) {
            detail = "the command queue of the sharing context cannot be created: " + ClTools::error_name(code);
            return Open::Failed;
        }
        return Open::Ready;
    }

    void settle() {
        cl.drain();
        gl.gl.glFinish();
    }
};

Outcome opened(const Bench& bench, Open state) {
    if (state == Open::Unsupported) {
        return Context::skip(bench.detail);
    }
    return Outcome::fail(bench.detail);
}

constexpr unsigned kCubeFirst = 0x8515;

struct Shape {
    const char* name;
    unsigned target;
    unsigned cl_target;
    int width;
    int height;
    int depth;
    int level;
    bool optional_level;
};

int level_size(int value, int level) {
    return std::max(1, value >> level);
}

}  // namespace

Outcome GlClChecks::context_device_match(Context& context) {
    if (const std::string why = Kit::missing({"gl", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    Bench bench;
    const Open state = bench.open(context, 64);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    Problems problems;
    ClRig& cl = bench.cl;
    cl_uint platform_devices = 0;
    cl.api.GetDeviceIDs(cl.platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &platform_devices);
    std::vector<cl_device_id> all(platform_devices);
    cl.api.GetDeviceIDs(cl.platform, CL_DEVICE_TYPE_ALL, platform_devices, all.data(), nullptr);
    auto in_platform = [&](cl_device_id device) { return std::find(all.begin(), all.end(), device) != all.end(); };
    cl_device_id current = nullptr;
    std::size_t returned = 0;
    cl_int code = bench.interop.GetGLContextInfo(bench.props, CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR, sizeof(current), &current, &returned);
    if (code != CL_SUCCESS || current == nullptr) {
        problems.add("CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR returned " + ClTools::error_name(code));
    } else if (!in_platform(current)) {
        problems.add("the current device for the GL context is not one of the platform's devices");
    }
    std::size_t bytes = 0;
    code = bench.interop.GetGLContextInfo(bench.props, CL_DEVICES_FOR_GL_CONTEXT_KHR, 0, nullptr, &bytes);
    std::vector<cl_device_id> listed(bytes / sizeof(cl_device_id));
    if (code != CL_SUCCESS || listed.empty()) {
        problems.add("CL_DEVICES_FOR_GL_CONTEXT_KHR returned " + ClTools::error_name(code) + " with " + std::to_string(listed.size()) + " device(s)");
    } else if (bench.interop.GetGLContextInfo(bench.props, CL_DEVICES_FOR_GL_CONTEXT_KHR, bytes, listed.data(), nullptr) == CL_SUCCESS) {
        for (cl_device_id device : listed) {
            if (!in_platform(device)) {
                problems.add("CL_DEVICES_FOR_GL_CONTEXT_KHR lists a device outside the platform");
            }
        }
        if (current != nullptr && std::find(listed.begin(), listed.end(), current) == listed.end()) {
            problems.add("CL_DEVICES_FOR_GL_CONTEXT_KHR does not contain the current device");
        }
    }
    std::size_t context_bytes = 0;
    cl.api.GetContextInfo(cl.context, CL_CONTEXT_DEVICES, 0, nullptr, &context_bytes);
    std::vector<cl_device_id> context_devices(context_bytes / sizeof(cl_device_id));
    cl.api.GetContextInfo(cl.context, CL_CONTEXT_DEVICES, context_bytes, context_devices.data(), nullptr);
    if (std::find(context_devices.begin(), context_devices.end(), cl.device) == context_devices.end()) {
        problems.add("the context created from the GL context does not contain its device");
    }
    const std::vector<std::uint8_t> data = Kit::frame_bytes(Kit::seed(5), 0, 32, 8);
    ClOwned<cl_mem> buffer;
    buffer.adopt(cl, cl.api.CreateBuffer(cl.context, CL_MEM_READ_WRITE, data.size(), nullptr, &code));
    std::vector<std::uint8_t> back(data.size());
    if (!buffer || code != CL_SUCCESS) {
        problems.add("a buffer in the context created from the GL context failed: " + ClTools::error_name(code));
    } else if (cl.api.EnqueueWriteBuffer(cl.queue, buffer.get(), CL_TRUE, 0, data.size(), data.data(), 0, nullptr, nullptr) != CL_SUCCESS ||
               cl.api.EnqueueReadBuffer(cl.queue, buffer.get(), CL_TRUE, 0, back.size(), back.data(), 0, nullptr, nullptr) != CL_SUCCESS || back != data) {
        problems.add("a write and read of a buffer in the context created from the GL context does not round trip");
    }
    buffer.reset();
    bench.settle();
    return problems.outcome();
}

Outcome GlClChecks::buffer_sharing(Context& context) {
    if (const std::string why = Kit::missing({"gl", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    Bench bench;
    const Open state = bench.open(context, 64);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    Problems problems;
    ClRig& cl = bench.cl;
    const GlApi& gl = bench.gl.gl;
    ClProgram program;
    if (!clkit::Kit::compile(cl, program, ClSources::kStep, "", problems, "the step kernel")) {
        return problems.outcome();
    }
    ClKernel kernel = clkit::Kit::kernel(program, "lsw_step", problems);
    if (!kernel) {
        return problems.outcome();
    }
    constexpr std::size_t kWords = 1024;
    constexpr std::size_t kBytes = kWords * 4;
    unsigned buffer = 0;
    gl.glGenBuffers(1, &buffer);
    gl.glBindBuffer(glc::ARRAY_BUFFER, buffer);
    gl.glBufferData(glc::ARRAY_BUFFER, static_cast<std::ptrdiff_t>(kBytes), nullptr, glc::DYNAMIC_DRAW);
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> shared;
    shared.adopt(cl, bench.interop.CreateFromGLBuffer(cl.context, CL_MEM_READ_WRITE, buffer, &code));
    if (!shared) {
        problems.add("clCreateFromGLBuffer returned " + ClTools::error_name(code));
    }
    for (int round = 0; shared && round < 4; ++round) {
        const std::vector<std::uint8_t> written = Kit::frame_bytes(Kit::seed(6), round, 32, 32);
        gl.glBufferSubData(glc::ARRAY_BUFFER, 0, static_cast<std::ptrdiff_t>(kBytes), written.data());
        gl.glFinish();
        cl_mem handle = shared.get();
        code = bench.interop.AcquireGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr);
        if (code != CL_SUCCESS) {
            problems.add("round " + std::to_string(round) + ": clEnqueueAcquireGLObjects returned " + ClTools::error_name(code));
            break;
        }
        std::vector<std::uint8_t> seen(kBytes);
        code = cl.api.EnqueueReadBuffer(cl.queue, shared.get(), CL_TRUE, 0, kBytes, seen.data(), 0, nullptr, nullptr);
        if (code != CL_SUCCESS || seen != written) {
            problems.add("round " + std::to_string(round) + ": the CL read of the GL buffer after glFinish and acquire " +
                         (code != CL_SUCCESS ? "returned " + ClTools::error_name(code) : "differs from what GL wrote"));
            clkit::Kit::same_bytes(context, problems, "gl_to_cl_round_" + std::to_string(round), written.data(), seen.data(), kBytes);
        }
        const cl_uint multiplier = 3;
        const cl_uint addend = static_cast<cl_uint>(round) + 1u;
        if (kernel.set(shared, multiplier, addend) != CL_SUCCESS || kernel.run(kWords) != CL_SUCCESS) {
            problems.add("round " + std::to_string(round) + ": the kernel over the shared buffer cannot run");
        }
        code = bench.interop.ReleaseGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr);
        cl.api.Finish(cl.queue);
        if (code != CL_SUCCESS) {
            problems.add("round " + std::to_string(round) + ": clEnqueueReleaseGLObjects returned " + ClTools::error_name(code));
            break;
        }
        std::vector<std::uint32_t> expected(kWords);
        std::memcpy(expected.data(), written.data(), kBytes);
        for (std::uint32_t& word : expected) {
            word = word * multiplier + addend;
        }
        std::vector<std::uint32_t> after(kWords);
        gl.glGetBufferSubData(glc::ARRAY_BUFFER, 0, static_cast<std::ptrdiff_t>(kBytes), after.data());
        clkit::Kit::same_words(context, problems, "cl_to_gl_round_" + std::to_string(round), expected, after);
    }
    shared.reset();
    gl.glBindBuffer(glc::ARRAY_BUFFER, 0);
    gl.glDeleteBuffers(1, &buffer);
    bench.settle();
    return problems.outcome();
}

Outcome GlClChecks::texture_targets_mips_layers(Context& context) {
    if (const std::string why = Kit::missing({"gl", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    Bench bench;
    const Open state = bench.open(context, 64);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    ClRig& cl = bench.cl;
    const GlApi& gl = bench.gl.gl;
    if (!cl.images()) {
        return Context::skip("the OpenCL device reports no image support");
    }
    Problems problems;
    std::vector<Shape> shapes = {{"2d", glc::TEXTURE_2D, glc::TEXTURE_2D, 32, 16, 1, 0, false}, {"2d mip 1", glc::TEXTURE_2D, glc::TEXTURE_2D, 32, 16, 1, 1, true}};
    if (gl.at_least(1, 2)) {
        shapes.push_back({"3d", glc::TEXTURE_3D, glc::TEXTURE_3D, 8, 8, 4, 0, false});
    }
    if (gl.supports(3, 0, "GL_EXT_texture_array")) {
        shapes.push_back({"2d array", glc::TEXTURE_2D_ARRAY, glc::TEXTURE_2D_ARRAY, 16, 8, 3, 0, false});
    }
    if (gl.supports(1, 3, "GL_ARB_texture_cube_map")) {
        for (unsigned face = 0; face < 6; ++face) {
            shapes.push_back({"cube face", glc::TEXTURE_CUBE_MAP, kCubeFirst + face, 16, 16, 1, 0, false});
        }
    }
    if (gl.supports(3, 1, "GL_ARB_texture_rectangle")) {
        shapes.push_back({"rectangle", glc::TEXTURE_RECTANGLE, glc::TEXTURE_RECTANGLE, 24, 12, 1, 0, false});
    }
    gl.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    gl.glPixelStorei(glc::PACK_ALIGNMENT, 1);
    unsigned shared_cube = 0;
    int created_targets = 0;
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        const Shape& shape = shapes[index];
        const bool is_cube = shape.target == glc::TEXTURE_CUBE_MAP;
        const std::string label = std::string(shape.name) + (is_cube ? " " + std::to_string(shape.cl_target - kCubeFirst) : std::string());
        unsigned texture = 0;
        if (is_cube && shared_cube != 0) {
            texture = shared_cube;
            gl.glBindTexture(shape.target, texture);
        } else {
            gl.glGenTextures(1, &texture);
            gl.glBindTexture(shape.target, texture);
            gl.glTexParameteri(shape.target, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
            gl.glTexParameteri(shape.target, glc::TEXTURE_MAG_FILTER, static_cast<int>(glc::NEAREST));
            if (shape.target != glc::TEXTURE_RECTANGLE) {
                gl.glTexParameteri(shape.target, glc::TEXTURE_MAX_LEVEL, shape.level);
            }
            if (is_cube) {
                shared_cube = texture;
                for (unsigned face = 0; face < 6; ++face) {
                    gl.glTexImage2D(kCubeFirst + face, 0, static_cast<int>(glc::RGBA8), shape.width, shape.height, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
                }
            } else if (shape.target == glc::TEXTURE_3D || shape.target == glc::TEXTURE_2D_ARRAY) {
                gl.glTexImage3D(shape.target, 0, static_cast<int>(glc::RGBA8), shape.width, shape.height, shape.depth, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
            } else {
                for (int level = 0; level <= shape.level; ++level) {
                    gl.glTexImage2D(shape.target, level, static_cast<int>(glc::RGBA8), level_size(shape.width, level), level_size(shape.height, level), 0, glc::RGBA, glc::UNSIGNED_BYTE,
                                    nullptr);
                }
            }
        }
        const int w = level_size(shape.width, shape.level);
        const int h = level_size(shape.height, shape.level);
        const int d = shape.depth;
        const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(d) * 4u;
        const unsigned gl_target = is_cube ? shape.cl_target : shape.target;
        const std::vector<std::uint8_t> from_gl = clkit::Kit::bytes(Kit::seed(7, static_cast<int>(index)), bytes);
        if (shape.target == glc::TEXTURE_3D || shape.target == glc::TEXTURE_2D_ARRAY) {
            gl.glTexSubImage3D(shape.target, shape.level, 0, 0, 0, w, h, d, glc::RGBA, glc::UNSIGNED_BYTE, from_gl.data());
        } else {
            gl.glTexSubImage2D(gl_target, shape.level, 0, 0, w, h, glc::RGBA, glc::UNSIGNED_BYTE, from_gl.data());
        }
        gl.glFinish();
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> image;
        image.adopt(cl, bench.interop.CreateFromGLTexture(cl.context, CL_MEM_READ_WRITE, shape.cl_target, shape.level, texture, &code));
        if (!image) {
            if (shape.optional_level && code == CL_INVALID_MIP_LEVEL) {
                context.note(label + ": mip level creation is declined with CL_INVALID_MIP_LEVEL, which the extension allows");
            } else {
                problems.add(label + ": clCreateFromGLTexture returned " + ClTools::error_name(code));
            }
        } else {
            ++created_targets;
            cl_mem handle = image.get();
            code = bench.interop.AcquireGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr);
            const std::size_t origin[3] = {0, 0, 0};
            const std::size_t region[3] = {static_cast<std::size_t>(w), static_cast<std::size_t>(h), static_cast<std::size_t>(d)};
            std::vector<std::uint8_t> seen(bytes);
            if (code != CL_SUCCESS) {
                problems.add(label + ": clEnqueueAcquireGLObjects returned " + ClTools::error_name(code));
            } else {
                code = cl.api.EnqueueReadImage(cl.queue, image.get(), CL_TRUE, origin, region, 0, 0, seen.data(), 0, nullptr, nullptr);
                if (code != CL_SUCCESS || seen != from_gl) {
                    problems.add(label + ": the CL image of the GL texture " + (code != CL_SUCCESS ? "cannot be read: " + ClTools::error_name(code) : std::string("differs from what GL uploaded")));
                    clkit::Kit::same_bytes(context, problems, "gl_to_cl_" + Kit::sanitize(label), from_gl.data(), seen.data(), bytes);
                }
                const std::vector<std::uint8_t> from_cl = clkit::Kit::bytes(Kit::seed(8, static_cast<int>(index)), bytes);
                code = cl.api.EnqueueWriteImage(cl.queue, image.get(), CL_TRUE, origin, region, 0, 0, from_cl.data(), 0, nullptr, nullptr);
                if (code != CL_SUCCESS) {
                    problems.add(label + ": the CL write to the shared image returned " + ClTools::error_name(code));
                }
                code = bench.interop.ReleaseGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr);
                cl.api.Finish(cl.queue);
                if (code != CL_SUCCESS) {
                    problems.add(label + ": clEnqueueReleaseGLObjects returned " + ClTools::error_name(code));
                } else {
                    std::vector<std::uint8_t> gl_seen(bytes);
                    gl.glGetTexImage(gl_target, shape.level, glc::RGBA, glc::UNSIGNED_BYTE, gl_seen.data());
                    if (gl_seen != from_cl) {
                        problems.add(label + ": GL does not see what CL wrote into the shared image");
                        clkit::Kit::same_bytes(context, problems, "cl_to_gl_" + Kit::sanitize(label), from_cl.data(), gl_seen.data(), bytes);
                    }
                }
            }
            image.reset();
        }
        gl.glBindTexture(shape.target, 0);
        const bool last_face = is_cube && shape.cl_target == kCubeFirst + 5;
        if (!is_cube || last_face) {
            gl.glDeleteTextures(1, &texture);
            shared_cube = 0;
        }
    }
    if (created_targets == 0) {
        problems.add("no texture target could be shared with OpenCL although cl_khr_gl_sharing is advertised");
    }
    bench.settle();
    return problems.outcome();
}

Outcome GlClChecks::renderbuffer_sharing(Context& context) {
    if (const std::string why = Kit::missing({"gl", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    Bench bench;
    const Open state = bench.open(context, 64);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    ClRig& cl = bench.cl;
    const GlApi& gl = bench.gl.gl;
    if (!cl.images()) {
        return Context::skip("the OpenCL device reports no image support");
    }
    Problems problems;
    constexpr int kEdge = 32;
    unsigned rbo = 0;
    unsigned fbo = 0;
    gl.glGenRenderbuffers(1, &rbo);
    gl.glBindRenderbuffer(glc::RENDERBUFFER, rbo);
    gl.glRenderbufferStorage(glc::RENDERBUFFER, glc::RGBA8, kEdge, kEdge);
    gl.glGenFramebuffers(1, &fbo);
    gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::RENDERBUFFER, rbo);
    if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
        problems.add("the RGBA8 renderbuffer framebuffer is incomplete");
    } else {
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> image;
        image.adopt(cl, bench.interop.CreateFromGLRenderbuffer(cl.context, CL_MEM_READ_WRITE, rbo, &code));
        if (!image) {
            problems.add("clCreateFromGLRenderbuffer returned " + ClTools::error_name(code));
        }
        for (int frame = 0; image && frame < 3; ++frame) {
            GlFrameDraw::frame(gl, kEdge, kEdge, frame + 2);
            gl.glFinish();
            std::vector<std::uint8_t> expected(static_cast<std::size_t>(kEdge) * kEdge * 4u);
            for (int row = 0; row < kEdge; ++row) {
                for (int x = 0; x < kEdge; ++x) {
                    const std::uint32_t c = FrameImage::at(frame + 2, kEdge, kEdge, x, kEdge - 1 - row);
                    std::uint8_t* px = &expected[Kit::at(x, row, kEdge) * 4u];
                    px[0] = static_cast<std::uint8_t>((c >> 16) & 255u);
                    px[1] = static_cast<std::uint8_t>((c >> 8) & 255u);
                    px[2] = static_cast<std::uint8_t>(c & 255u);
                    px[3] = 255;
                }
            }
            cl_mem handle = image.get();
            code = bench.interop.AcquireGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr);
            const std::size_t origin[3] = {0, 0, 0};
            const std::size_t region[3] = {kEdge, kEdge, 1};
            std::vector<std::uint8_t> seen(expected.size());
            if (code != CL_SUCCESS) {
                problems.add("frame " + std::to_string(frame) + ": clEnqueueAcquireGLObjects returned " + ClTools::error_name(code));
                break;
            }
            code = cl.api.EnqueueReadImage(cl.queue, image.get(), CL_TRUE, origin, region, 0, 0, seen.data(), 0, nullptr, nullptr);
            if (code != CL_SUCCESS || seen != expected) {
                problems.add("frame " + std::to_string(frame) + ": CL reads " + (code != CL_SUCCESS ? ClTools::error_name(code) : std::string("a different image")) + " from the rendered renderbuffer");
                clkit::Kit::same_bytes(context, problems, "gl_renderbuffer_frame_" + std::to_string(frame), expected.data(), seen.data(), expected.size());
            }
            const std::vector<std::uint8_t> from_cl = clkit::Kit::bytes(Kit::seed(9, frame), expected.size());
            code = cl.api.EnqueueWriteImage(cl.queue, image.get(), CL_TRUE, origin, region, 0, 0, from_cl.data(), 0, nullptr, nullptr);
            const cl_int released = bench.interop.ReleaseGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr);
            cl.api.Finish(cl.queue);
            if (code != CL_SUCCESS || released != CL_SUCCESS) {
                problems.add("frame " + std::to_string(frame) + ": the CL write or the release failed: " + ClTools::error_name(code) + " / " + ClTools::error_name(released));
                break;
            }
            std::vector<std::uint8_t> gl_seen(expected.size());
            gl.glReadPixels(0, 0, kEdge, kEdge, glc::RGBA, glc::UNSIGNED_BYTE, gl_seen.data());
            if (gl_seen != from_cl) {
                problems.add("frame " + std::to_string(frame) + ": glReadPixels does not return what CL wrote into the renderbuffer");
                clkit::Kit::same_bytes(context, problems, "cl_renderbuffer_frame_" + std::to_string(frame), from_cl.data(), gl_seen.data(), from_cl.size());
            }
        }
        image.reset();
    }
    gl.glBindFramebuffer(glc::FRAMEBUFFER, 0);
    gl.glDeleteFramebuffers(1, &fbo);
    gl.glDeleteRenderbuffers(1, &rbo);
    bench.settle();
    return problems.outcome();
}

Outcome GlClChecks::gl_event_sync(Context& context) {
    if (const std::string why = Kit::missing({"gl", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    Bench bench;
    const Open state = bench.open(context, 64);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    ClRig& cl = bench.cl;
    const GlApi& gl = bench.gl.gl;
    Problems problems;
    const bool advertised = cl.has_extension("cl_khr_gl_event");
    context.measure("cl_khr_gl_event", advertised);
    if (!advertised) {
        if (bench.interop.CreateEventFromGLsync != nullptr) {
            const void* fence = gl.glFenceSync != nullptr ? gl.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0) : nullptr;
            cl_int code = CL_SUCCESS;
            cl_event event = fence != nullptr ? bench.interop.CreateEventFromGLsync(cl.context, reinterpret_cast<cl_GLsync>(const_cast<void*>(fence)), &code) : nullptr;
            if (event != nullptr) {
                cl.api.ReleaseEvent(event);
                problems.add("clCreateEventFromGLsyncKHR succeeds although cl_khr_gl_event is not advertised");
            }
            gl.glFinish();
            if (fence != nullptr) {
                gl.glDeleteSync(const_cast<void*>(fence));
            }
        }
        context.note("cl_khr_gl_event is not advertised; clCreateEventFromGLsyncKHR fails or is absent, and the glFinish path is covered by the buffer sharing row");
        return problems.outcome();
    }
    if (bench.interop.CreateEventFromGLsync == nullptr || gl.glFenceSync == nullptr || !cl.images()) {
        return Context::skip("cl_khr_gl_event is advertised but a GL fence or CL image support is not available here");
    }
    constexpr int kEdge = 64;
    unsigned rbo = 0;
    unsigned fbo = 0;
    gl.glGenRenderbuffers(1, &rbo);
    gl.glBindRenderbuffer(glc::RENDERBUFFER, rbo);
    gl.glRenderbufferStorage(glc::RENDERBUFFER, glc::RGBA8, kEdge, kEdge);
    gl.glGenFramebuffers(1, &fbo);
    gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::RENDERBUFFER, rbo);
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> image;
    image.adopt(cl, bench.interop.CreateFromGLRenderbuffer(cl.context, CL_MEM_READ_WRITE, rbo, &code));
    if (!image) {
        problems.add("clCreateFromGLRenderbuffer returned " + ClTools::error_name(code));
    }
    const bool reverse = gl.has_extension("GL_ARB_cl_event") && gl.glCreateSyncFromCLeventARB != nullptr;
    context.measure("GL_ARB_cl_event", reverse);
    for (int frame = 0; image && frame < 4; ++frame) {
        GlFrameDraw::frame(gl, kEdge, kEdge, frame + 10);
        void* fence = gl.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
        gl.glFlush();
        cl_event gl_done = fence != nullptr ? bench.interop.CreateEventFromGLsync(cl.context, reinterpret_cast<cl_GLsync>(fence), &code) : nullptr;
        if (gl_done == nullptr) {
            problems.add("frame " + std::to_string(frame) + ": clCreateEventFromGLsyncKHR returned " + ClTools::error_name(code));
            if (fence != nullptr) {
                gl.glFinish();
                gl.glDeleteSync(fence);
            }
            break;
        }
        cl_mem handle = image.get();
        cl_event acquired = nullptr;
        code = bench.interop.AcquireGLObjects(cl.queue, 1, &handle, 1, &gl_done, &acquired);
        std::vector<std::uint8_t> seen(static_cast<std::size_t>(kEdge) * kEdge * 4u);
        const std::size_t origin[3] = {0, 0, 0};
        const std::size_t region[3] = {kEdge, kEdge, 1};
        if (code == CL_SUCCESS) {
            code = cl.api.EnqueueReadImage(cl.queue, image.get(), CL_TRUE, origin, region, 0, 0, seen.data(), 1, &acquired, nullptr);
        }
        bool matches = code == CL_SUCCESS;
        for (int row = 0; matches && row < kEdge; row += 7) {
            for (int x = 0; x < kEdge; x += 5) {
                const std::uint32_t c = FrameImage::at(frame + 10, kEdge, kEdge, x, kEdge - 1 - row);
                const std::uint8_t* px = &seen[Kit::at(x, row, kEdge) * 4u];
                if (px[0] != ((c >> 16) & 255u) || px[1] != ((c >> 8) & 255u) || px[2] != (c & 255u)) {
                    matches = false;
                    problems.add("frame " + std::to_string(frame) + ": CL ordered after the GL fence by cl_khr_gl_event reads pixel (" + std::to_string(x) + "," + std::to_string(row) + ") as " +
                                 Kit::rgb(Kit::pack(px[0], px[1], px[2])) + ", GL rendered " + Kit::rgb(c));
                    break;
                }
            }
        }
        if (code != CL_SUCCESS) {
            problems.add("frame " + std::to_string(frame) + ": the CL side of the GL event ordering returned " + ClTools::error_name(code));
        }
        const std::vector<std::uint8_t> from_cl = clkit::Kit::bytes(Kit::seed(10, frame), seen.size());
        cl_event released = nullptr;
        const bool held = acquired != nullptr;
        const bool wrote = code == CL_SUCCESS && cl.api.EnqueueWriteImage(cl.queue, image.get(), CL_FALSE, origin, region, 0, 0, from_cl.data(), 0, nullptr, nullptr) == CL_SUCCESS;
        if (held && !wrote) {
            static_cast<void>(bench.interop.ReleaseGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr));
        }
        if (wrote) {
            code = bench.interop.ReleaseGLObjects(cl.queue, 1, &handle, 0, nullptr, &released);
            if (code == CL_SUCCESS && reverse) {
                void* sync = gl.glCreateSyncFromCLeventARB(cl.context, released, 0);
                if (sync == nullptr) {
                    problems.add("frame " + std::to_string(frame) + ": glCreateSyncFromCLeventARB returned no sync object");
                } else {
                    cl.api.Flush(cl.queue);
                    const unsigned waited = gl.glClientWaitSync(sync, glc::SYNC_FLUSH_COMMANDS_BIT, 5000000000ull);
                    if (waited != glc::ALREADY_SIGNALED && waited != glc::CONDITION_SATISFIED) {
                        problems.add("frame " + std::to_string(frame) + ": the GL sync created from the CL release event never signalled (" + GlTools::wait_name(waited) + ")");
                    }
                    gl.glDeleteSync(sync);
                }
            }
        }
        cl.api.Finish(cl.queue);
        if (code == CL_SUCCESS) {
            std::vector<std::uint8_t> gl_seen(seen.size());
            gl.glReadPixels(0, 0, kEdge, kEdge, glc::RGBA, glc::UNSIGNED_BYTE, gl_seen.data());
            if (gl_seen != from_cl) {
                problems.add("frame " + std::to_string(frame) + ": GL does not see what CL wrote after the release");
            }
        }
        for (cl_event event : {gl_done, acquired, released}) {
            if (event != nullptr) {
                cl.api.ReleaseEvent(event);
            }
        }
        gl.glFinish();
        gl.glDeleteSync(fence);
    }
    image.reset();
    gl.glBindFramebuffer(glc::FRAMEBUFFER, 0);
    gl.glDeleteFramebuffers(1, &fbo);
    gl.glDeleteRenderbuffers(1, &rbo);
    bench.settle();
    return problems.outcome();
}

Outcome GlClChecks::acquire_release_errors(Context& context) {
    if (const std::string why = Kit::missing({"gl", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    Bench bench;
    const Open state = bench.open(context, 64);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    ClRig& cl = bench.cl;
    const GlApi& gl = bench.gl.gl;
    Problems problems;
    auto expect = [&](const std::string& what, cl_int got, std::initializer_list<cl_int> accepted) {
        clkit::Kit::expect(problems, what, got, accepted);
    };
    unsigned buffer = 0;
    gl.glGenBuffers(1, &buffer);
    gl.glBindBuffer(glc::ARRAY_BUFFER, buffer);
    gl.glBufferData(glc::ARRAY_BUFFER, 256, nullptr, glc::DYNAMIC_DRAW);
    unsigned texture = 0;
    gl.glGenTextures(1, &texture);
    gl.glBindTexture(glc::TEXTURE_2D, texture);
    gl.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), 4, 4, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
    gl.glFinish();
    cl_int code = CL_SUCCESS;
    cl_mem made = bench.interop.CreateFromGLBuffer(cl.context, CL_MEM_READ_WRITE, 0xFFFFFFF0u, &code);
    if (made != nullptr) {
        cl.api.ReleaseMemObject(made);
    }
    expect("clCreateFromGLBuffer of a name GL never created", code, {CL_INVALID_GL_OBJECT});
    made = bench.interop.CreateFromGLBuffer(cl.context, 0x4000, buffer, &code);
    if (made != nullptr) {
        cl.api.ReleaseMemObject(made);
    }
    expect("clCreateFromGLBuffer with an invalid flag", code, {CL_INVALID_VALUE});
    made = bench.interop.CreateFromGLBuffer(nullptr, CL_MEM_READ_WRITE, buffer, &code);
    if (made != nullptr) {
        cl.api.ReleaseMemObject(made);
    }
    expect("clCreateFromGLBuffer with a null context", code, {CL_INVALID_CONTEXT});
    made = bench.interop.CreateFromGLTexture(cl.context, CL_MEM_READ_WRITE, glc::ARRAY_BUFFER, 0, texture, &code);
    if (made != nullptr) {
        cl.api.ReleaseMemObject(made);
    }
    expect("clCreateFromGLTexture with a target that is not a texture target", code, {CL_INVALID_VALUE});
    made = bench.interop.CreateFromGLTexture(cl.context, CL_MEM_READ_WRITE, glc::TEXTURE_2D, 0, 0xFFFFFFF0u, &code);
    if (made != nullptr) {
        cl.api.ReleaseMemObject(made);
    }
    expect("clCreateFromGLTexture of a name GL never created", code, {CL_INVALID_GL_OBJECT});
    made = bench.interop.CreateFromGLRenderbuffer(cl.context, CL_MEM_READ_WRITE, 0xFFFFFFF0u, &code);
    if (made != nullptr) {
        cl.api.ReleaseMemObject(made);
    }
    expect("clCreateFromGLRenderbuffer of a name GL never created", code, {CL_INVALID_GL_OBJECT});
    ClOwned<cl_mem> shared;
    shared.adopt(cl, bench.interop.CreateFromGLBuffer(cl.context, CL_MEM_READ_WRITE, buffer, &code));
    cl_int plain_code = CL_SUCCESS;
    ClOwned<cl_mem> plain;
    plain.adopt(cl, cl.api.CreateBuffer(cl.context, CL_MEM_READ_WRITE, 64, nullptr, &plain_code));
    if (!shared || !plain) {
        problems.add("the shared or the plain buffer cannot be created: " + ClTools::error_name(code) + " / " + ClTools::error_name(plain_code));
    } else {
        cl_mem plain_handle = plain.get();
        expect("clEnqueueAcquireGLObjects with zero objects and a null list", bench.interop.AcquireGLObjects(cl.queue, 0, nullptr, 0, nullptr, nullptr), {CL_SUCCESS});
        expect("clEnqueueAcquireGLObjects with zero objects and a list", bench.interop.AcquireGLObjects(cl.queue, 0, &plain_handle, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        expect("clEnqueueAcquireGLObjects with a null list", bench.interop.AcquireGLObjects(cl.queue, 1, nullptr, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        expect("clEnqueueReleaseGLObjects with zero objects and a null list", bench.interop.ReleaseGLObjects(cl.queue, 0, nullptr, 0, nullptr, nullptr), {CL_SUCCESS});
        expect("clEnqueueReleaseGLObjects with zero objects and a list", bench.interop.ReleaseGLObjects(cl.queue, 0, &plain_handle, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        expect("clEnqueueAcquireGLObjects of a buffer not created from GL", bench.interop.AcquireGLObjects(cl.queue, 1, &plain_handle, 0, nullptr, nullptr), {CL_INVALID_GL_OBJECT});
        expect("clEnqueueReleaseGLObjects of a buffer not created from GL", bench.interop.ReleaseGLObjects(cl.queue, 1, &plain_handle, 0, nullptr, nullptr), {CL_INVALID_GL_OBJECT});
        cl_gl_object_type type = 0;
        cl_GLuint name = 0;
        expect("clGetGLObjectInfo of a buffer not created from GL", bench.interop.GetGLObjectInfo(plain.get(), &type, &name), {CL_INVALID_GL_OBJECT});
        const cl_int info = bench.interop.GetGLObjectInfo(shared.get(), &type, &name);
        if (info != CL_SUCCESS || type != CL_GL_OBJECT_BUFFER || name != buffer) {
            problems.add("clGetGLObjectInfo of the shared buffer returns " + ClTools::error_name(info) + ", type " + std::to_string(type) + ", name " + std::to_string(name) + " instead of the GL buffer " + std::to_string(buffer));
        }
        cl_mem handle = shared.get();
        const cl_int acquired = bench.interop.AcquireGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr);
        const cl_int released = bench.interop.ReleaseGLObjects(cl.queue, 1, &handle, 0, nullptr, nullptr);
        cl.api.Finish(cl.queue);
        expect("a plain acquire of the shared buffer after the errors", acquired, {CL_SUCCESS});
        expect("the matching release", released, {CL_SUCCESS});
    }
    shared.reset();
    plain.reset();
    gl.glBindTexture(glc::TEXTURE_2D, 0);
    gl.glDeleteTextures(1, &texture);
    gl.glBindBuffer(glc::ARRAY_BUFFER, 0);
    gl.glDeleteBuffers(1, &buffer);
    bench.settle();
    return problems.outcome();
}

Outcome GlClChecks::caps_unsupported(Context& context) {
    if (const std::string why = Kit::missing({"gl", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    Bench bench;
    const Open state = bench.open(context, 64, false);
    if (state != Open::Ready) {
        return Outcome::fail(bench.detail);
    }
    ClRig& cl = bench.cl;
    const GlApi& gl = bench.gl.gl;
    Problems problems;
    const bool advertised = cl.has_extension("cl_khr_gl_sharing");
    context.measure("cl_khr_gl_sharing", advertised);
    const char* names[] = {"clGetGLContextInfoKHR", "clCreateFromGLBuffer", "clCreateFromGLTexture", "clCreateFromGLRenderbuffer", "clEnqueueAcquireGLObjects", "clEnqueueReleaseGLObjects"};
    int resolved = 0;
    for (const char* name : names) {
        bool from_platform = false;
        static_cast<void>(ClInterop::find(cl, name, &from_platform));
        resolved += from_platform ? 1 : 0;
        if (advertised && !from_platform) {
            problems.add(std::string(name) + " does not resolve from the platform although cl_khr_gl_sharing is advertised");
        }
    }
    context.measure("platform_entry_points", resolved);
    cl_int code = CL_SUCCESS;
    cl.context = cl.api.CreateContext(bench.props, 1, &cl.device, nullptr, nullptr, &code);
    if (advertised) {
        if (cl.context == nullptr) {
            problems.add("cl_khr_gl_sharing is advertised but a context from the current GL context fails: " + ClTools::error_name(code));
        } else {
            cl.queue = cl.create_queue(0, code);
        }
    } else {
        if (cl.context != nullptr) {
            problems.add("a context with CL_GL_CONTEXT_KHR is created although cl_khr_gl_sharing is not advertised");
        } else if (code == CL_SUCCESS) {
            problems.add("clCreateContext with CL_GL_CONTEXT_KHR returned no context and CL_SUCCESS");
        }
        cl_context_properties plain[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(cl.platform), 0};
        cl.context = cl.api.CreateContext(plain, 1, &cl.device, nullptr, nullptr, &code);
        if (cl.context == nullptr) {
            problems.add("a plain context cannot be created: " + ClTools::error_name(code));
        } else {
            unsigned buffer = 0;
            gl.glGenBuffers(1, &buffer);
            gl.glBindBuffer(glc::ARRAY_BUFFER, buffer);
            gl.glBufferData(glc::ARRAY_BUFFER, 64, nullptr, glc::STATIC_DRAW);
            if (bench.interop.CreateFromGLBuffer != nullptr) {
                cl_mem made = bench.interop.CreateFromGLBuffer(cl.context, CL_MEM_READ_WRITE, buffer, &code);
                if (made != nullptr) {
                    cl.api.ReleaseMemObject(made);
                    problems.add("clCreateFromGLBuffer succeeds in a context without GL sharing");
                } else if (!ClTools::code_in(code, {CL_INVALID_CONTEXT, CL_INVALID_GL_OBJECT, CL_INVALID_OPERATION, CL_INVALID_VALUE})) {
                    problems.add("clCreateFromGLBuffer in a context without GL sharing returned " + ClTools::error_name(code) + ", not a documented error code");
                }
            }
            gl.glBindBuffer(glc::ARRAY_BUFFER, 0);
            gl.glDeleteBuffers(1, &buffer);
        }
    }
    bench.settle();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
