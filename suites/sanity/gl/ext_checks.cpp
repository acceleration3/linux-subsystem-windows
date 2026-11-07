#include "sanity/gl/ext_checks.hpp"

#include "sanity/gl/gl_name_tables.hpp"
#include "sanity/gl/kit.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"
#include "support/gl_fetch.hpp"
#include "support/gl_scene.hpp"
#include "support/gl_target.hpp"

#include <cmath>
#include <cstring>
#include <set>
#include <sstream>

namespace lsw::suites::sanity::gl {

namespace {

std::vector<std::string> split(const char* text) {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        out.push_back(word);
    }
    return out;
}

struct DebugRecord {
    int count = 0;
    int errors = 0;
    unsigned source = 0;
    unsigned type = 0;
    unsigned id = 0;
    unsigned severity = 0;
    std::string message;
    int length = 0;
};

void __stdcall debug_callback(unsigned source, unsigned type, unsigned id, unsigned severity, int length, const char* message, const void* user) {
    auto* record = const_cast<DebugRecord*>(static_cast<const DebugRecord*>(user));
    ++record->count;
    if (type == glc::DEBUG_TYPE_ERROR) {
        ++record->errors;
    }
    record->source = source;
    record->type = type;
    record->id = id;
    record->severity = severity;
    record->length = length;
    record->message = message == nullptr ? std::string() : std::string(message);
}

enum class Probe { Absent, Ok, Error };

struct Gate {
    const char* name;
    int major;
    int minor;
    const char* extension;
    Probe (*probe)(const GlApi&);
};

Probe finish_probe(const GlApi& api) {
    return api.drain_errors() == 0 ? Probe::Ok : Probe::Error;
}

Probe probe_compute(const GlApi& api) {
    (void)api.drain_errors();
    const unsigned shader = api.glCreateShader(glc::COMPUTE_SHADER);
    const Probe result = shader != 0 && api.glGetError() == 0 ? Probe::Ok : Probe::Error;
    (void)api.drain_errors();
    if (shader != 0) {
        api.glDeleteShader(shader);
    }
    return result;
}

Probe probe_tessellation(const GlApi& api) {
    (void)api.drain_errors();
    const unsigned shader = api.glCreateShader(glc::TESS_CONTROL_SHADER);
    const Probe result = shader != 0 && api.glGetError() == 0 ? Probe::Ok : Probe::Error;
    (void)api.drain_errors();
    if (shader != 0) {
        api.glDeleteShader(shader);
    }
    return result;
}

Probe probe_storage(const GlApi& api) {
    if (api.glBufferStorage == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    unsigned buffer = 0;
    api.glGenBuffers(1, &buffer);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, buffer);
    api.glBufferStorage(glc::COPY_WRITE_BUFFER, 256, nullptr, 0);
    const Probe result = finish_probe(api);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, 0);
    api.glDeleteBuffers(1, &buffer);
    return result;
}

Probe probe_debug(const GlApi& api) {
    if (api.glDebugMessageControl == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    api.glDebugMessageControl(glc::DONT_CARE, glc::DONT_CARE, glc::DONT_CARE, 0, nullptr, 1);
    return finish_probe(api);
}

Probe probe_storage_2d(const GlApi& api) {
    if (api.glTexStorage2D == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    unsigned texture = 0;
    api.glGenTextures(1, &texture);
    api.glBindTexture(glc::TEXTURE_2D, texture);
    api.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 4, 4);
    const Probe result = finish_probe(api);
    api.glBindTexture(glc::TEXTURE_2D, 0);
    api.glDeleteTextures(1, &texture);
    return result;
}

Probe probe_view(const GlApi& api) {
    if (api.glTextureView == nullptr || api.glTexStorage2D == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    unsigned textures[2] = {};
    api.glGenTextures(2, textures);
    api.glBindTexture(glc::TEXTURE_2D, textures[0]);
    api.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 4, 4);
    api.glTextureView(textures[1], glc::TEXTURE_2D, textures[0], glc::RGBA8, 0, 1, 0, 1);
    const Probe result = finish_probe(api);
    api.glBindTexture(glc::TEXTURE_2D, 0);
    api.glDeleteTextures(2, textures);
    return result;
}

Probe probe_clear_buffer(const GlApi& api) {
    if (api.glClearBufferData == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    unsigned buffer = 0;
    api.glGenBuffers(1, &buffer);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, buffer);
    api.glBufferData(glc::COPY_WRITE_BUFFER, 64, nullptr, glc::STATIC_DRAW);
    const std::uint8_t zero = 0;
    api.glClearBufferData(glc::COPY_WRITE_BUFFER, glc::R8, glc::RED, glc::UNSIGNED_BYTE, &zero);
    const Probe result = finish_probe(api);
    api.glBindBuffer(glc::COPY_WRITE_BUFFER, 0);
    api.glDeleteBuffers(1, &buffer);
    return result;
}

Probe probe_sync(const GlApi& api) {
    if (api.glFenceSync == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    void* fence = api.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
    const bool ok = fence != nullptr && api.glGetError() == 0;
    (void)api.drain_errors();
    if (fence != nullptr) {
        api.glDeleteSync(fence);
    }
    return ok ? Probe::Ok : Probe::Error;
}

Probe probe_image(const GlApi& api) {
    auto bind = reinterpret_cast<void(__stdcall*)(unsigned, unsigned, int, unsigned char, int, unsigned, unsigned)>(GlLibrary::instance().proc("glBindImageTexture"));
    if (bind == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    bind(0, 0, 0, 0, 0, glc::READ_ONLY, glc::RGBA8);
    return finish_probe(api);
}

Probe probe_dsa(const GlApi& api) {
    auto create = reinterpret_cast<void(__stdcall*)(int, unsigned*)>(GlLibrary::instance().proc("glCreateBuffers"));
    if (create == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    unsigned buffer = 0;
    create(1, &buffer);
    const Probe result = finish_probe(api);
    api.glDeleteBuffers(1, &buffer);
    return result;
}

Probe probe_clip(const GlApi& api) {
    auto control = reinterpret_cast<void(__stdcall*)(unsigned, unsigned)>(GlLibrary::instance().proc("glClipControl"));
    if (control == nullptr) {
        return Probe::Absent;
    }
    (void)api.drain_errors();
    control(0x8CA1, 0x935E);
    return finish_probe(api);
}

Probe probe_multisample(const GlApi& api) {
    (void)api.drain_errors();
    unsigned texture = 0;
    api.glGenTextures(1, &texture);
    api.glBindTexture(glc::TEXTURE_2D_MULTISAMPLE, texture);
    const Probe result = finish_probe(api);
    api.glBindTexture(glc::TEXTURE_2D_MULTISAMPLE, 0);
    api.glDeleteTextures(1, &texture);
    return result;
}

const Gate kGates[] = {
    {"compute shaders", 4, 3, "GL_ARB_compute_shader", probe_compute},
    {"tessellation shaders", 4, 0, "GL_ARB_tessellation_shader", probe_tessellation},
    {"immutable buffer storage", 4, 4, "GL_ARB_buffer_storage", probe_storage},
    {"debug output", 4, 3, "GL_KHR_debug", probe_debug},
    {"immutable texture storage", 4, 2, "GL_ARB_texture_storage", probe_storage_2d},
    {"texture views", 4, 3, "GL_ARB_texture_view", probe_view},
    {"clear buffer object", 4, 3, "GL_ARB_clear_buffer_object", probe_clear_buffer},
    {"sync objects", 3, 2, "GL_ARB_sync", probe_sync},
    {"image load store", 4, 2, "GL_ARB_shader_image_load_store", probe_image},
    {"direct state access", 4, 5, "GL_ARB_direct_state_access", probe_dsa},
    {"clip control", 4, 5, "GL_ARB_clip_control", probe_clip},
    {"multisample textures", 3, 2, "GL_ARB_texture_multisample", probe_multisample},
};

void run_gates(const GlApi& api, const std::string& label, Problems& problems, Context& context, int& unadvertised) {
    for (const Gate& gate : kGates) {
        const bool advertised = api.supports(gate.major, gate.minor, gate.extension);
        const Probe result = gate.probe(api);
        if (advertised && result != Probe::Ok) {
            problems.add(label + ": " + gate.name + " is advertised (version " + std::to_string(api.major()) + "." + std::to_string(api.minor()) + " / " + gate.extension +
                         ") but " + (result == Probe::Absent ? "its entry point does not resolve" : "a valid call gives a GL error"));
        }
        if (!advertised) {
            ++unadvertised;
            if (result == Probe::Ok) {
                problems.add(label + ": " + gate.name + " works although the " + std::to_string(api.major()) + "." + std::to_string(api.minor()) + " context advertises neither OpenGL " +
                             std::to_string(gate.major) + "." + std::to_string(gate.minor) + " nor " + gate.extension);
            }
        }
        context.measure(label + "." + gate.name, advertised ? (result == Probe::Ok ? "advertised, works" : "advertised, broken") : (result == Probe::Ok ? "unadvertised, works" : "unadvertised, refused"));
    }
}

}  // namespace

Outcome ExtChecks::string_matches_entry_points(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    GlLibrary& library = GlLibrary::instance();
    Problems problems;
    std::set<std::string> mapped;
    int functions = 0;
    int called = 0;
    for (const ExtensionFunctions& entry : kGlExtensionFunctions) {
        mapped.insert(entry.extension);
        if (!api.has_extension(entry.extension)) {
            continue;
        }
        for (const std::string& name : split(entry.functions)) {
            ++functions;
            void* address = library.proc(name.c_str());
            if (address == nullptr) {
                problems.add(std::string(entry.extension) + " is advertised but " + name + " does not resolve through wglGetProcAddress");
                continue;
            }
            if (name.rfind("glIs", 0) == 0 && name != "glIsEnabled" && name != "glIsSync") {
                (void)api.drain_errors();
                const unsigned char result = reinterpret_cast<unsigned char(__stdcall*)(unsigned)>(address)(0);
                const unsigned failure = api.drain_errors();
                ++called;
                if (result != 0 || failure != 0) {
                    problems.add(name + "(0) returned " + std::to_string(result) + " with " + GlApi::error_name(failure));
                }
            }
        }
    }
    std::string unmapped;
    for (const std::string& extension : api.extensions()) {
        if (mapped.count(extension) == 0) {
            unmapped += (unmapped.empty() ? "" : " ") + extension;
        }
    }
    const std::string wgl_list = rig.wgl.extensions(rig.dc());
    int wgl_functions = 0;
    for (const ExtensionFunctions& entry : kWglExtensionFunctions) {
        if (!WglApi::contains(wgl_list, entry.extension)) {
            continue;
        }
        for (const std::string& name : split(entry.functions)) {
            ++wgl_functions;
            if (library.proc(name.c_str()) == nullptr) {
                problems.add(std::string(entry.extension) + " is advertised but " + name + " does not resolve");
            }
        }
    }
    context.measure("gl_extensions", static_cast<long long>(api.extensions().size()));
    context.measure("entry_points_resolved", functions + wgl_functions);
    context.measure("trivial_calls", called);
    context.measure("wgl_extensions", wgl_list);
    if (!unmapped.empty()) {
        context.note("advertised extensions without a known entry point list: " + unmapped);
    }
    return problems.outcome();
}

Outcome ExtChecks::unadvertised_not_supported(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int unadvertised = 0;
    run_gates(rig.gl, "default", problems, context, unadvertised);
    if (Kit::core_profile_available(rig)) {
        GlRig core;
        GlRigOptions options;
        options.width = 64;
        options.height = 64;
        options.request.major = 3;
        options.request.minor = 2;
        options.request.profile = wglc::CONTEXT_CORE_PROFILE_BIT;
        if (!core.open(options, error)) {
            problems.add("a 3.2 core context cannot be created: " + error);
        } else {
            run_gates(core.gl, "core3.2", problems, context, unadvertised);
        }
    } else {
        context.note("WGL_ARB_create_context_profile is not advertised; only the default context was judged");
    }
    context.measure("unadvertised_gates", unadvertised);
    return problems.outcome();
}

Outcome ExtChecks::version_implies_core_functions(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    GlLibrary& library = GlLibrary::instance();
    Problems problems;
    int resolved = 0;
    for (const VersionFunctions& group : kCoreFunctions) {
        if (api.version() < group.version) {
            continue;
        }
        for (const std::string& name : split(group.functions)) {
            if (library.resolve(name.c_str()) == nullptr) {
                problems.add("GL_VERSION " + std::to_string(api.major()) + "." + std::to_string(api.minor()) + " but the OpenGL " + std::to_string(group.version / 10) + "." +
                             std::to_string(group.version % 10) + " function " + name + " does not resolve");
            } else {
                ++resolved;
            }
        }
    }
    int checked = 0;
    for (const VersionLimit& limit : kLimits) {
        if (api.version() < limit.since || (limit.major_only != 0 && api.major() != limit.major_only)) {
            continue;
        }
        (void)api.drain_errors();
        int value = -1;
        api.glGetIntegerv(limit.name, &value);
        const unsigned failure = api.drain_errors();
        ++checked;
        if (failure != 0) {
            problems.add(std::string(limit.label) + " query gave " + GlApi::error_name(failure));
        } else if (value < limit.minimum) {
            problems.add(std::string(limit.label) + " is " + std::to_string(value) + ", the specification minimum for OpenGL " + std::to_string(api.major()) + "." + std::to_string(api.minor()) +
                         " is " + std::to_string(limit.minimum));
        }
        context.measure(limit.label, value);
    }
    context.measure("functions_resolved", resolved);
    context.measure("limits_checked", checked);
    return problems.outcome();
}

Outcome ExtChecks::external_memory_caps(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    Problems problems;
    const bool memory = api.has_extension("GL_EXT_memory_object");
    const bool memory_win32 = api.has_extension("GL_EXT_memory_object_win32");
    const bool semaphore = api.has_extension("GL_EXT_semaphore");
    const bool semaphore_win32 = api.has_extension("GL_EXT_semaphore_win32");
    context.measure("memory_object", memory);
    context.measure("memory_object_win32", memory_win32);
    context.measure("semaphore", semaphore);
    context.measure("semaphore_win32", semaphore_win32);
    GlLibrary& library = GlLibrary::instance();
    struct Gated {
        bool advertised;
        const char* extension;
        const char* function;
    };
    const Gated gated[] = {{memory, "GL_EXT_memory_object", "glCreateMemoryObjectsEXT"}, {memory_win32, "GL_EXT_memory_object_win32", "glImportMemoryWin32HandleEXT"},
                           {semaphore, "GL_EXT_semaphore", "glGenSemaphoresEXT"}, {semaphore_win32, "GL_EXT_semaphore_win32", "glImportSemaphoreWin32HandleEXT"}};
    for (const Gated& item : gated) {
        void* address = library.proc(item.function);
        if (item.advertised && address == nullptr) {
            problems.add(std::string(item.extension) + " is advertised but " + item.function + " does not resolve");
        }
        if (!item.advertised && address != nullptr) {
            (void)api.drain_errors();
            if (std::strcmp(item.function, "glCreateMemoryObjectsEXT") == 0) {
                unsigned object = 0;
                api.glCreateMemoryObjectsEXT(1, &object);
            } else if (std::strcmp(item.function, "glGenSemaphoresEXT") == 0) {
                unsigned object = 0;
                api.glGenSemaphoresEXT(1, &object);
            } else if (std::strcmp(item.function, "glImportMemoryWin32HandleEXT") == 0) {
                api.glImportMemoryWin32HandleEXT(0, 0, glc::HANDLE_TYPE_D3D11_IMAGE_EXT, nullptr);
            } else {
                api.glImportSemaphoreWin32HandleEXT(0, glc::HANDLE_TYPE_D3D12_FENCE_EXT, nullptr);
            }
            if (api.drain_errors() == 0) {
                problems.add(std::string(item.function) + " resolves and succeeds although " + item.extension + " is not advertised");
            }
        }
    }
    if (!memory || !memory_win32) {
        if (!problems.empty()) {
            return problems.outcome();
        }
        if (!memory && !memory_win32) {
            context.note("GL_EXT_memory_object(_win32) is not advertised; the entry points are absent or refuse");
            return Outcome::ok();
        }
        return Context::skip(std::string("GL_EXT_memory_object is ") + (memory ? "" : "not ") + "advertised and GL_EXT_memory_object_win32 is " + (memory_win32 ? "" : "not ") +
                             "advertised, so no Win32 handle can be imported");
    }
    if (api.glGetUnsignedBytevEXT != nullptr) {
        (void)api.drain_errors();
        unsigned char luid[8] = {};
        api.glGetUnsignedBytevEXT(glc::DEVICE_LUID_EXT, luid);
        if (api.drain_errors() == 0) {
            if (std::memcmp(luid, &rig.adapter.luid, 8) != 0) {
                problems.add("GL_DEVICE_LUID_EXT 0x" + Text::hex(*reinterpret_cast<unsigned long long*>(luid), 16) + " is not the LUID " + rig.adapter.luid_text() + " of the adapter");
            }
        } else {
            context.note("GL_DEVICE_LUID_EXT is not queryable");
        }
    }
    D3d11Device device;
    if (!device.create(rig.adapter.luid, error)) {
        return Outcome::fail("the Direct3D 11 producer device cannot be created: " + error);
    }
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, desc);
    Com<IDXGIResource1> resource;
    if (texture) {
        texture->QueryInterface(__uuidof(IDXGIResource1), resource.put_void());
    }
    HANDLE handle = nullptr;
    if (!texture || !resource || FAILED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle)) || handle == nullptr) {
        return Context::skip("a Direct3D 11 texture with an NT shared handle cannot be created on this adapter, so there is nothing to import");
    }
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, texture.get());
    D3d11Tools::clear(device, view.get(), 0.25f, 0.5f, 0.75f, 1.0f);
    std::string wait_error;
    device.context->Flush();
    if (!device.wait_idle(wait_error)) {
        problems.add("the Direct3D 11 producer did not finish: " + wait_error);
    }
    (void)api.drain_errors();
    unsigned object = 0;
    api.glCreateMemoryObjectsEXT(1, &object);
    const int dedicated = 1;
    api.glMemoryObjectParameterivEXT(object, glc::DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
    api.glImportMemoryWin32HandleEXT(object, 64ull * 64ull * 4ull, glc::HANDLE_TYPE_D3D11_IMAGE_EXT, handle);
    const unsigned import_error = api.drain_errors();
    if (import_error != 0) {
        problems.add("glImportMemoryWin32HandleEXT with GL_HANDLE_TYPE_D3D11_IMAGE_EXT (0x958B) for a 64x64 RGBA8 shared texture gave " + GlApi::error_name(import_error) +
                     " although GL_EXT_memory_object_win32 is advertised");
    } else {
        unsigned gl_texture = 0;
        api.glGenTextures(1, &gl_texture);
        api.glBindTexture(glc::TEXTURE_2D, gl_texture);
        api.glTexStorageMem2DEXT(glc::TEXTURE_2D, 1, glc::RGBA8, 64, 64, object, 0);
        api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
        const unsigned storage_error = api.drain_errors();
        if (storage_error != 0) {
            problems.add("glTexStorageMem2DEXT over the imported memory gave " + GlApi::error_name(storage_error));
        } else {
            GlFetchRequest request;
            request.sampler = "sampler2D";
            request.expression = "texelFetch(tex, p, 0)";
            request.target = glc::TEXTURE_2D;
            request.texture = gl_texture;
            request.width = 64;
            request.height = 64;
            std::vector<float> values;
            if (!GlFetch::run(api, request, values, error)) {
                problems.add("sampling the imported texture: " + error);
            } else {
                const float want[3] = {0.25f, 0.5f, 0.75f};
                for (int y = 0; y < 64 && !values.empty(); y += 9) {
                    for (int x = 0; x < 64; x += 7) {
                        const float* px = &values[static_cast<std::size_t>(y * 64 + x) * 4];
                        if (std::fabs(px[0] - want[0]) > 0.01f || std::fabs(px[1] - want[1]) > 0.01f || std::fabs(px[2] - want[2]) > 0.01f) {
                            problems.add("the imported texture samples (" + std::to_string(px[0]) + "," + std::to_string(px[1]) + "," + std::to_string(px[2]) + ") at (" + std::to_string(x) +
                                         "," + std::to_string(y) + "), the Direct3D 11 producer cleared (0.25,0.5,0.75)");
                            y = 64;
                            break;
                        }
                    }
                }
            }
        }
        api.glBindTexture(glc::TEXTURE_2D, 0);
        api.glDeleteTextures(1, &gl_texture);
    }
    api.glDeleteMemoryObjectsEXT(1, &object);
    api.glFinish();
    ::CloseHandle(handle);
    if (semaphore && semaphore_win32) {
        context.note("the semaphore hand-off ordering is not exercised: no Direct3D 11 timeline fence import path is defined for GL_EXT_semaphore_win32 in this suite");
    }
    return problems.outcome();
}

Outcome ExtChecks::getError_contracts(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 128, 128)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    Problems problems;
    (void)api.drain_errors();
    auto expect = [&](const char* what, unsigned wanted, auto&& call) {
        call();
        GlTools::expect_error(problems, api, what, wanted);
    };
    int viewport[4] = {};
    api.glViewport(3, 4, 50, 60);
    api.glGetIntegerv(glc::VIEWPORT, viewport);
    expect("glEnable(0xFFFF)", glc::INVALID_ENUM, [&] { api.glEnable(0xFFFF); });
    expect("glDisable(0xFFFF)", glc::INVALID_ENUM, [&] { api.glDisable(0xFFFF); });
    expect("glViewport with a negative width", glc::INVALID_VALUE, [&] { api.glViewport(0, 0, -1, 1); });
    int after[4] = {};
    api.glGetIntegerv(glc::VIEWPORT, after);
    if (std::memcmp(viewport, after, sizeof(viewport)) != 0) {
        problems.add("a rejected glViewport changed the viewport");
    }
    expect("glClear with an undefined bit", glc::INVALID_VALUE, [&] { api.glClear(1u << 20); });
    expect("glBegin(0xFFFF)", glc::INVALID_ENUM, [&] { api.glBegin(0xFFFF); });
    expect("glEnd without glBegin", glc::INVALID_OPERATION, [&] { api.glEnd(); });
    expect("glBegin inside glBegin", glc::INVALID_OPERATION, [&] {
        api.glBegin(glc::TRIANGLES);
        api.glBegin(glc::TRIANGLES);
        api.glEnd();
    });
    expect("glPopMatrix on a stack of depth 1", glc::STACK_UNDERFLOW, [&] { api.glPopMatrix(); });
    {
        const int depth = api.integer(glc::MAX_MODELVIEW_STACK_DEPTH);
        int pushed = 0;
        unsigned overflow = 0;
        for (int i = 0; i < depth + 2 && overflow == 0; ++i) {
            api.glPushMatrix();
            overflow = api.glGetError();
            pushed += overflow == 0 ? 1 : 0;
        }
        if (overflow != glc::STACK_OVERFLOW || pushed != depth - 1) {
            problems.add("pushing the modelview stack (depth " + std::to_string(depth) + ") overflowed after " + std::to_string(pushed) + " pushes with " + GlApi::error_name(overflow) +
                         ", expected GL_STACK_OVERFLOW after " + std::to_string(depth - 1));
        }
        for (int i = 0; i < pushed; ++i) {
            api.glPopMatrix();
        }
        (void)api.drain_errors();
    }
    expect("glReadBuffer(GL_COLOR_ATTACHMENT0) on the default framebuffer", glc::INVALID_OPERATION, [&] { api.glReadBuffer(glc::COLOR_ATTACHMENT0); });
    expect("glBindFramebuffer(0xFFFF, 0)", glc::INVALID_ENUM, [&] { api.glBindFramebuffer(0xFFFF, 0); });
    unsigned fbo = 0;
    api.glGenFramebuffers(1, &fbo);
    api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    expect("glFramebufferTexture2D with an invalid attachment", glc::INVALID_ENUM, [&] { api.glFramebufferTexture2D(glc::FRAMEBUFFER, 0xFFFF, glc::TEXTURE_2D, 0, 0); });
    expect("glClear on an incomplete framebuffer", glc::INVALID_FRAMEBUFFER_OPERATION, [&] { api.glClear(glc::COLOR_BUFFER_BIT); });
    api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
    api.glDeleteFramebuffers(1, &fbo);
    {
        const int limit = api.integer(glc::MAX_DRAW_BUFFERS);
        std::vector<unsigned> buffers(static_cast<std::size_t>(limit) + 1, glc::NONE);
        (void)api.drain_errors();
        expect("glDrawBuffers with more entries than GL_MAX_DRAW_BUFFERS", glc::INVALID_VALUE, [&] { api.glDrawBuffers(limit + 1, buffers.data()); });
    }
    expect("glTexImage2D with border 2", glc::INVALID_VALUE, [&] { api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), 4, 4, 2, glc::RGBA, glc::UNSIGNED_BYTE, nullptr); });
    {
        unsigned texture = 0;
        expect("glGenTextures(-1)", glc::INVALID_VALUE, [&] { api.glGenTextures(-1, &texture); });
        api.glGenTextures(1, &texture);
        api.glBindTexture(glc::TEXTURE_CUBE_MAP, texture);
        expect("glBindTexture(GL_TEXTURE_2D) of a cube map texture", glc::INVALID_OPERATION, [&] { api.glBindTexture(glc::TEXTURE_2D, texture); });
        api.glBindTexture(glc::TEXTURE_CUBE_MAP, 0);
        api.glDeleteTextures(1, &texture);
    }
    expect("glUseProgram of a name that was never created", glc::INVALID_VALUE, [&] { api.glUseProgram(0x7FFFFF); });
    expect("glBufferData with a negative size", glc::INVALID_VALUE, [&] {
        unsigned buffer = 0;
        api.glGenBuffers(1, &buffer);
        api.glBindBuffer(glc::ARRAY_BUFFER, buffer);
        api.glBufferData(glc::ARRAY_BUFFER, -1, nullptr, glc::STATIC_DRAW);
        api.glBindBuffer(glc::ARRAY_BUFFER, 0);
        api.glDeleteBuffers(1, &buffer);
    });
    expect("glDrawArrays with a negative count", glc::INVALID_VALUE, [&] { api.glDrawArrays(glc::TRIANGLES, 0, -1); });
    expect("glDrawArrays with mode 0xFFFF", glc::INVALID_ENUM, [&] { api.glDrawArrays(0xFFFF, 0, 3); });
    if (api.glIsEnabled(glc::BLEND) != 0) {
        problems.add("GL_BLEND became enabled after rejected glEnable calls");
    }
    GlTarget target;
    GlScene scene(32, 32);
    scene.clear_color(GlTools::rgba(20, 40, 60, 255));
    scene.add({4, 4, 20, 28, GlTools::rgba(250, 10, 100, 255)});
    std::vector<std::uint8_t> image;
    if (!target.create(api, 32, 32, glc::RGBA8, false, error) || !scene.draw(api, error) || !target.read_rgba(image, error)) {
        problems.add("after the error cases: " + error);
    } else {
        std::string detail;
        if (!GlTools::same_image(context, "after_errors", scene.expected(), image, 32, 32, 0, detail)) {
            problems.add("after the error cases the context renders wrongly: " + detail);
        }
    }
    scene.release();
    return problems.outcome();
}

Outcome ExtChecks::debug_output(Context& context) {
    GlRig probe;
    std::string error;
    if (!Kit::open(probe, context, error)) {
        return Outcome::fail(error);
    }
    if (probe.wgl.create_context_attribs == nullptr) {
        return Kit::not_advertised("WGL_ARB_create_context");
    }
    GlRig rig;
    GlRigOptions options;
    options.width = 64;
    options.height = 64;
    options.request.major = 3;
    options.request.minor = 3;
    options.request.profile = Kit::core_profile_available(probe) ? wglc::CONTEXT_CORE_PROFILE_BIT : 0;
    options.request.flags = wglc::CONTEXT_DEBUG_BIT;
    if (!rig.open(options, error)) {
        return Outcome::fail("a debug context cannot be created: " + error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(4, 3, "GL_KHR_debug") || api.glDebugMessageCallback == nullptr || api.glDebugMessageInsert == nullptr) {
        return Kit::not_advertised("GL_KHR_debug");
    }
    Problems problems;
    DebugRecord record;
    api.glDebugMessageCallback(reinterpret_cast<const void*>(&debug_callback), &record);
    api.glEnable(glc::DEBUG_OUTPUT);
    api.glEnable(glc::DEBUG_OUTPUT_SYNCHRONOUS);
    api.glDebugMessageControl(glc::DONT_CARE, glc::DONT_CARE, glc::DONT_CARE, 0, nullptr, 1);
    GlTools::expect_clean(problems, api, "setting up the callback");
    const char* marker = "lsw-suites marker";
    api.glDebugMessageInsert(glc::DEBUG_SOURCE_APPLICATION, glc::DEBUG_TYPE_MARKER, 4711, glc::DEBUG_SEVERITY_LOW, -1, marker);
    if (record.count != 1) {
        problems.add("glDebugMessageInsert with DEBUG_OUTPUT_SYNCHRONOUS did not deliver exactly one message before returning (" + std::to_string(record.count) + ")");
    } else if (record.source != glc::DEBUG_SOURCE_APPLICATION || record.type != glc::DEBUG_TYPE_MARKER || record.id != 4711 || record.severity != glc::DEBUG_SEVERITY_LOW ||
               record.message != marker || record.length != static_cast<int>(std::strlen(marker))) {
        problems.add("the inserted message arrives as source 0x" + Text::hex(record.source, 4) + " type 0x" + Text::hex(record.type, 4) + " id " + std::to_string(record.id) + " severity 0x" +
                     Text::hex(record.severity, 4) + " length " + std::to_string(record.length) + " '" + record.message + "'");
    }
    record = DebugRecord();
    api.glEnable(0xFFFF);
    if (record.count < 1 || record.errors < 1) {
        problems.add("glEnable(0xFFFF) delivered " + std::to_string(record.count) + " messages (" + std::to_string(record.errors) + " of type ERROR) before returning");
    } else {
        context.measure("error_message_severity", GlTools::hex(record.severity));
        context.measure("error_message", record.message);
        if (record.type != glc::DEBUG_TYPE_ERROR || record.severity == glc::DEBUG_SEVERITY_NOTIFICATION || record.message.empty()) {
            problems.add("the error message has type 0x" + Text::hex(record.type, 4) + ", severity 0x" + Text::hex(record.severity, 4) + " and text '" + record.message + "'");
        }
    }
    GlTools::expect_error(problems, api, "glEnable(0xFFFF)", glc::INVALID_ENUM);
    record = DebugRecord();
    api.glDebugMessageControl(glc::DONT_CARE, glc::DEBUG_TYPE_ERROR, glc::DONT_CARE, 0, nullptr, 0);
    api.glEnable(0xFFFF);
    (void)api.drain_errors();
    if (record.errors != 0) {
        problems.add("an error message is delivered after errors were disabled with glDebugMessageControl");
    }
    api.glDebugMessageControl(glc::DONT_CARE, glc::DEBUG_TYPE_ERROR, glc::DONT_CARE, 0, nullptr, 1);
    record = DebugRecord();
    GlTarget target;
    GlScene scene(32, 32);
    scene.clear_color(GlTools::rgba(1, 2, 3, 255));
    scene.add({2, 2, 30, 30, GlTools::rgba(9, 8, 7, 255)});
    std::vector<std::uint8_t> image;
    unsigned buffer = 0;
    unsigned texture = 0;
    if (!target.create(api, 32, 32, glc::RGBA8, false, error) || !scene.draw(api, error) || !target.read_rgba(image, error)) {
        problems.add(error);
    }
    api.glGenBuffers(1, &buffer);
    api.glBindBuffer(glc::ARRAY_BUFFER, buffer);
    api.glBufferData(glc::ARRAY_BUFFER, 64, nullptr, glc::STATIC_DRAW);
    api.glGenTextures(1, &texture);
    api.glBindTexture(glc::TEXTURE_2D, texture);
    api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), 4, 4, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
    api.glBindTexture(glc::TEXTURE_2D, 0);
    api.glDeleteTextures(1, &texture);
    api.glBindBuffer(glc::ARRAY_BUFFER, 0);
    api.glDeleteBuffers(1, &buffer);
    api.glFinish();
    scene.release();
    target.destroy();
    GlTools::expect_clean(problems, api, "valid work");
    if (record.errors != 0) {
        problems.add("valid work delivered " + std::to_string(record.errors) + " error-type debug messages, the last: '" + record.message + "'");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
