#include "sanity/reg/reg_checks.hpp"

#include "core/text.hpp"
#include "sanity/err/limit_checks.hpp"
#include "sanity/gl/context_checks.hpp"
#include "sanity/gl/kit.hpp"
#include "support/api_policy.hpp"
#include "support/backend_harness.hpp"
#include "support/cl_kernel.hpp"
#include "support/cl_program.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_tools.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_tools.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/refresh_rate.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace lsw::suites::sanity::reg {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

std::atomic<int> g_display_changes{0};

LRESULT CALLBACK watch_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_DISPLAYCHANGE) {
        g_display_changes.fetch_add(1);
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

template <typename T>
bool in_range(T value, T low, T high) {
    return value >= low && value <= high;
}

}  // namespace

Outcome RegChecks::cl_buffer_alive_while_in_use(Context&) {
    ClRig rig;
    std::string error;
    if (!rig.open(ClRigOptions{}, error)) {
        return Context::skip("OpenCL is unavailable: " + error);
    }
    ClProgram program;
    const char* source =
        "__kernel void slow_copy(__global const uint* a, __global uint* b, uint n, uint rounds) {\n"
        "  uint i = (uint)get_global_id(0);\n"
        "  uint x = i;\n"
        "  for (uint r = 0; r < rounds; ++r) { x = x * 1664525u + 1013904223u; }\n"
        "  b[i] = a[i] + (x == 0xFFFFFFFFu ? 1u : 0u) * 0u + (x & 0u);\n"
        "}\n";
    if (!program.build(rig, source, "", error)) {
        return Outcome::fail(error);
    }
    ClKernel kernel = program.kernel("slow_copy", error);
    if (!kernel) {
        return Outcome::fail(error);
    }
    constexpr std::size_t kCount = 4096;
    Problems problems;
    for (int round = 0; round < 40; ++round) {
        std::vector<std::uint32_t> input(kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            input[i] = static_cast<std::uint32_t>(i * 2654435761u + static_cast<std::uint32_t>(round));
        }
        cl_int code = CL_SUCCESS;
        cl_mem source_buffer = rig.api.CreateBuffer(rig.context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, kCount * 4, input.data(), &code);
        ClOwned<cl_mem> destination(rig, rig.api.CreateBuffer(rig.context, CL_MEM_READ_WRITE, kCount * 4, nullptr, &code));
        if (source_buffer == nullptr || !destination) {
            return Outcome::fail("creating the buffers failed: " + ClTools::error_name(code));
        }
        const cl_uint n = static_cast<cl_uint>(kCount);
        const cl_uint rounds = 1u << 20;
        cl_int set = CL_SUCCESS;
        set = rig.api.SetKernelArg(kernel.get(), 0, sizeof(cl_mem), &source_buffer);
        cl_mem dest_handle = destination.get();
        set |= rig.api.SetKernelArg(kernel.get(), 1, sizeof(cl_mem), &dest_handle);
        set |= rig.api.SetKernelArg(kernel.get(), 2, sizeof(n), &n);
        set |= rig.api.SetKernelArg(kernel.get(), 3, sizeof(rounds), &rounds);
        cl_event done = nullptr;
        if (set != CL_SUCCESS || kernel.run(kCount, 0, &done) != CL_SUCCESS) {
            return Outcome::fail("enqueueing the kernel failed");
        }
        rig.api.ReleaseMemObject(source_buffer);
        rig.api.Flush(rig.queue);
        std::vector<std::uint32_t> output(kCount);
        if (rig.api.EnqueueReadBuffer(rig.queue, destination.get(), CL_TRUE, 0, kCount * 4, output.data(), 0, nullptr, nullptr) != CL_SUCCESS) {
            return Outcome::fail("reading the result failed");
        }
        rig.api.ReleaseEvent(done);
        if (output != input) {
            problems.add("round " + std::to_string(round) + ": the result differs after the input buffer was released while the kernel was enqueued");
            break;
        }
    }
    return problems.outcome();
}

Outcome RegChecks::d3d12_caps_enum_defined(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    ID3D12Device* device = rig.device();
    Problems problems;
    auto bad_bool = [&](const char* what, BOOL value) {
        if (value != 0 && value != 1) {
            problems.add(std::string(what) + " holds " + std::to_string(value) + ", not a BOOL");
        }
    };
    int answered = 0;
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o)))) {
            ++answered;
            bad_bool("OPTIONS.DoublePrecisionFloatShaderOps", o.DoublePrecisionFloatShaderOps);
            bad_bool("OPTIONS.ROVsSupported", o.ROVsSupported);
            if (!in_range<int>(o.ResourceBindingTier, 1, 3) || !in_range<int>(o.TiledResourcesTier, 0, 4) || !in_range<int>(o.ConservativeRasterizationTier, 0, 3) ||
                !in_range<int>(o.CrossNodeSharingTier, 0, 3) || !in_range<int>(o.ResourceHeapTier, 1, 2)) {
                problems.add("OPTIONS holds a tier outside its enum");
            }
        }
    }
    {
        D3D12_FEATURE_DATA_ARCHITECTURE a{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &a, sizeof(a)))) {
            ++answered;
            bad_bool("ARCHITECTURE.TileBasedRenderer", a.TileBasedRenderer);
            bad_bool("ARCHITECTURE.UMA", a.UMA);
            bad_bool("ARCHITECTURE.CacheCoherentUMA", a.CacheCoherentUMA);
        }
    }
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE r{};
        r.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &r, sizeof(r)))) {
            ++answered;
            if (r.HighestVersion != D3D_ROOT_SIGNATURE_VERSION_1_0 && r.HighestVersion != D3D_ROOT_SIGNATURE_VERSION_1_1) {
                problems.add("ROOT_SIGNATURE.HighestVersion is " + std::to_string(r.HighestVersion));
            }
        }
    }
    {
        D3D12_FEATURE_DATA_SHADER_MODEL m{};
        m.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(0x7f);
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &m, sizeof(m)))) {
            ++answered;
            if (m.HighestShaderModel < 0x50 || m.HighestShaderModel > 0x7f) {
                problems.add("SHADER_MODEL.HighestShaderModel is 0x" + Text::hex(static_cast<unsigned>(m.HighestShaderModel), 2));
            }
        }
    }
    {
        D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT v{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &v, sizeof(v)))) {
            ++answered;
            if (v.MaxGPUVirtualAddressBitsPerResource < 32 || v.MaxGPUVirtualAddressBitsPerResource > 64 || v.MaxGPUVirtualAddressBitsPerProcess < v.MaxGPUVirtualAddressBitsPerResource) {
                problems.add("GPU_VIRTUAL_ADDRESS_SUPPORT holds " + std::to_string(v.MaxGPUVirtualAddressBitsPerResource) + " and " + std::to_string(v.MaxGPUVirtualAddressBitsPerProcess));
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 o{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &o, sizeof(o)))) {
            ++answered;
            bad_bool("OPTIONS3.CopyQueueTimestampQueriesSupported", o.CopyQueueTimestampQueriesSupported);
            bad_bool("OPTIONS3.CastingFullyTypedFormatSupported", o.CastingFullyTypedFormatSupported);
            if ((static_cast<unsigned>(o.WriteBufferImmediateSupportFlags) & ~0x7fu) != 0 || !in_range<int>(o.ViewInstancingTier, 0, 3)) {
                problems.add("OPTIONS3 holds an undefined flag or tier");
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o, sizeof(o)))) {
            ++answered;
            if (!in_range<int>(o.RenderPassesTier, 0, 2) || (o.RaytracingTier != 0 && o.RaytracingTier != 10 && o.RaytracingTier != 11 && o.RaytracingTier != 12)) {
                problems.add("OPTIONS5 holds a tier outside its enum");
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 o{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o, sizeof(o)))) {
            ++answered;
            if (!in_range<int>(o.VariableShadingRateTier, 0, 2)) {
                problems.add("OPTIONS6.VariableShadingRateTier is outside its enum");
            }
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 o{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o, sizeof(o)))) {
            ++answered;
            if ((o.MeshShaderTier != 0 && o.MeshShaderTier != 10) || (o.SamplerFeedbackTier != 0 && o.SamplerFeedbackTier != 90 && o.SamplerFeedbackTier != 100)) {
                problems.add("OPTIONS7 holds a tier outside its enum");
            }
        }
    }
    int refused = 0;
    for (int feature = -1; feature < 80; ++feature) {
        for (UINT size : {0u, 1u, 3u, 4u, 8u, 64u}) {
            std::uint8_t buffer[256] = {};
            const HRESULT result = device->CheckFeatureSupport(static_cast<D3D12_FEATURE>(feature), buffer, size);
            if (FAILED(result)) {
                ++refused;
                if (!D3d12Tools::in(result, {E_INVALIDARG, DXGI_ERROR_UNSUPPORTED, E_NOTIMPL, E_FAIL})) {
                    problems.add("CheckFeatureSupport(" + std::to_string(feature) + ", size " + std::to_string(size) + ") returned the undocumented " + D3d12Tools::hr(result));
                }
            }
        }
    }
    context.measure("structures_checked", answered);
    context.measure("calls_refused", refused);
    return problems.outcome();
}

Outcome RegChecks::stats_transfer_counts_plausible(Context& context) {
    AvAdapter adapter;
    std::string error;
    std::unique_ptr<GpuBackend> backend = BackendHarness::open("d3d11", adapter, error);
    if (backend == nullptr) {
        return Outcome::fail("D3D11 backend: " + error);
    }
    long long uploaded = 0;
    long long read_back = 0;
    for (int i = 0; i < 64; ++i) {
        AllocStatus status = AllocStatus::Ok;
        if (!BackendHarness::roundtrip(*backend, 1u << 20, 4000 + static_cast<std::uint64_t>(i), status, context, error)) {
            return Outcome::fail(error);
        }
        uploaded += 1 << 20;
        read_back += 1 << 20;
    }
    context.measure("uploaded_bytes", uploaded);
    context.measure("read_back_bytes", read_back);
    context.measure("operations", 128);
    return Context::skip("the stack's transfer counters exist on the host; the sequence of 64 uploads and 64 readbacks of 1 MiB was run and its totals recorded for the host phase");
}

Outcome RegChecks::upload_large_not_slower_than_small(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    constexpr UINT kWidth = 1920;
    constexpr UINT kHeight = 1088;
    constexpr UINT kPieces = 64;
    constexpr UINT kRows = kHeight / kPieces;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, D3d11Tools::desc2d(kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE));
    if (!texture) {
        return Outcome::fail("creating the texture failed");
    }
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kWidth) * kHeight);
    Pattern::fill_pixels(77, kWidth, kHeight, kWidth * 4, pixels.data());
    double best_large = 1e9;
    double best_small = 1e9;
    for (int attempt = 0; attempt < 5; ++attempt) {
        auto begin = Clock::now();
        device.context->UpdateSubresource(texture.get(), 0, nullptr, pixels.data(), kWidth * 4, 0);
        if (!device.wait_idle(error)) {
            return Outcome::fail(error);
        }
        best_large = std::min(best_large, ms_since(begin));
        begin = Clock::now();
        for (UINT piece = 0; piece < kPieces; ++piece) {
            const D3D11_BOX box{0, piece * kRows, 0, kWidth, (piece + 1) * kRows, 1};
            device.context->UpdateSubresource(texture.get(), 0, &box, pixels.data() + static_cast<std::size_t>(piece) * kRows * kWidth, kWidth * 4, 0);
        }
        if (!device.wait_idle(error)) {
            return Outcome::fail(error);
        }
        best_small = std::min(best_small, ms_since(begin));
    }
    context.measure("large_ms", best_large);
    context.measure("small_total_ms", best_small);
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
    TexelImage image;
    if (!staging) {
        return Outcome::fail("creating the staging texture failed");
    }
    device.context->CopyResource(staging.get(), texture.get());
    if (!D3d11Tools::read_texture(device, staging.get(), 0, image, error)) {
        return Outcome::fail(error);
    }
    if (std::memcmp(image.data.data(), pixels.data(), pixels.size() * 4) != 0) {
        return Outcome::fail("the uploaded texture does not read back equal");
    }
    if (best_large > best_small * 1.5) {
        return Outcome::fail("one full-screen upload took " + std::to_string(best_large) + " ms, 64 uploads of a 64th took " + std::to_string(best_small) + " ms in total");
    }
    return Outcome::ok();
}

Outcome RegChecks::dwm_output_not_toggled(Context& context) {
    g_display_changes = 0;
    WNDCLASSW cls{};
    cls.lpfnWndProc = watch_proc;
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.lpszClassName = L"lsw-suites-display-watch";
    ::RegisterClassW(&cls);
    std::atomic<bool> stop{false};
    std::thread pump([&] {
        HWND window = ::CreateWindowExW(0, cls.lpszClassName, L"watch", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, cls.hInstance, nullptr);
        MSG message;
        while (!stop.load() && window != nullptr) {
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                ::DispatchMessageW(&message);
            }
            ::Sleep(20);
        }
        if (window != nullptr) {
            ::DestroyWindow(window);
        }
    });
    DEVMODEW first{};
    first.dmSize = sizeof(first);
    ::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &first);
    Problems problems;
    DWM_TIMING_INFO timing;
    std::uint64_t last_refresh = 0;
    int stalls = 0;
    const auto begin = Clock::now();
    while (ms_since(begin) < 30000) {
        DEVMODEW now{};
        now.dmSize = sizeof(now);
        ::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &now);
        if (now.dmPelsWidth != first.dmPelsWidth || now.dmPelsHeight != first.dmPelsHeight || now.dmDisplayFrequency != first.dmDisplayFrequency) {
            problems.add("the display mode changed during the window");
            break;
        }
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        bool attached = false;
        for (DWORD i = 0; ::EnumDisplayDevicesW(nullptr, i, &device, 0) != FALSE; ++i) {
            attached = attached || (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
        }
        if (!attached) {
            problems.add("no output is attached to the desktop");
            break;
        }
        if (RefreshRate::dwm_timing(timing)) {
            if (timing.cRefresh < last_refresh) {
                problems.add("the DWM refresh counter went backwards, the output restarted");
                break;
            }
            if (timing.cRefresh == last_refresh && ++stalls > 30) {
                problems.add("the DWM refresh counter stopped for 3 s");
                break;
            }
            if (timing.cRefresh != last_refresh) {
                stalls = 0;
            }
            last_refresh = timing.cRefresh;
        }
        ::Sleep(100);
    }
    stop = true;
    pump.join();
    context.measure("display_change_messages", g_display_changes.load());
    if (g_display_changes.load() != 0) {
        problems.add(std::to_string(g_display_changes.load()) + " WM_DISPLAYCHANGE messages arrived in 30 s");
    }
    return problems.outcome();
}

Outcome RegChecks::gl_core_context_works(Context& context) {
    return gl::ContextChecks::arb_version_profile(context);
}

Outcome RegChecks::objects_many_objects_work(Context& context) {
    return err::LimitChecks::many_objects(context, 200000);
}

Outcome RegChecks::gl_legacy_context_version_string(Context& context) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    struct Rule {
        int version;
        std::vector<const char*> extensions;
    };
    const Rule rules[] = {{30, {"GL_ARB_framebuffer_object", "GL_ARB_vertex_array_object"}}, {31, {"GL_ARB_uniform_buffer_object", "GL_ARB_copy_buffer"}}, {33, {"GL_ARB_sampler_objects", "GL_ARB_explicit_attrib_location"}},
                          {40, {"GL_ARB_tessellation_shader"}}, {41, {"GL_ARB_separate_shader_objects"}}, {43, {"GL_ARB_compute_shader"}}, {45, {"GL_ARB_direct_state_access"}}};
    int implied = 0;
    for (const Rule& rule : rules) {
        bool all = true;
        for (const char* extension : rule.extensions) {
            all = all && rig.gl.has_extension(extension);
        }
        if (all) {
            implied = rule.version;
        }
    }
    int major = 0;
    int minor = 0;
    if (std::sscanf(rig.version.c_str(), "%d.%d", &major, &minor) != 2) {
        return Outcome::fail("GL_VERSION '" + rig.version + "' does not start with a version number");
    }
    context.measure("version_number", major * 10 + minor);
    context.measure("version_implied_by_extensions", implied);
    if (major * 10 + minor < implied) {
        return Outcome::fail("GL_VERSION '" + rig.version + "' is below version " + std::to_string(implied / 10) + "." + std::to_string(implied % 10) + " that the extension string enables");
    }
    return Outcome::ok();
}

}  // namespace lsw::suites::sanity::reg
