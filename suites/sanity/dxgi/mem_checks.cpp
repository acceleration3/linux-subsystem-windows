#include "sanity/dxgi/mem_checks.hpp"

#include "core/text.hpp"
#include "support/d3d12_device.hpp"
#include "support/d3d11_tools.hpp"
#include "support/dxgi_budget.hpp"
#include "support/dyn_library.hpp"
#include "support/problems.hpp"
#include "support/swap_rig.hpp"
#include "support/target_adapter.hpp"

#include <dxgi1_6.h>
#include <dxgidebug.h>

#include <chrono>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::dxgi {

namespace {

const GUID kDebugAll = {0xe48ae283, 0xda80, 0x490b, {0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x08}};

std::uint32_t pack(int r, int g, int b) {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) | 0xFF000000u;
}

Com<IDXGIOutput> first_output(const AvAdapter& target) {
    Com<IDXGIOutput> output;
    IUnknown* opened = AvAdapters::open_adapter(target.luid);
    if (opened != nullptr) {
        Com<IDXGIAdapter1> adapter(static_cast<IDXGIAdapter1*>(opened));
        adapter->EnumOutputs(0, output.put());
    }
    return output;
}

}  // namespace

Outcome MemChecks::video_memory_info(Context& context) {
    AvAdapter target;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(target, error) || !device.create(target.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    VideoMemory before;
    if (!DxgiBudget::query(target.luid, true, before, error)) {
        return Outcome::fail(error);
    }
    if (before.budget == 0) {
        problems.add("the local budget is zero");
    }
    if (before.available_for_reservation > before.budget) {
        problems.add("AvailableForReservation " + std::to_string(before.available_for_reservation >> 20) + " MiB exceeds the budget " + std::to_string(before.budget >> 20) + " MiB");
    }
    constexpr std::uint64_t kSize = 256ull << 20;
    VideoMemory during;
    {
        Com<ID3D11Texture2D> big = D3d11Tools::texture(device, D3d11Tools::desc2d(8192, 8192, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_RENDER_TARGET));
        Com<ID3D11RenderTargetView> view = big ? D3d11Tools::rtv(device, big.get()) : Com<ID3D11RenderTargetView>();
        if (!view) {
            return Outcome::fail("a 256 MiB render target cannot be created");
        }
        D3d11Tools::clear(device, view.get(), 1, 0, 0, 0);
        static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
        if (!DxgiBudget::query(target.luid, true, during, error)) {
            return Outcome::fail(error);
        }
        if (during.usage < before.usage + kSize) {
            problems.add("CurrentUsage rose by " + std::to_string((static_cast<std::int64_t>(during.usage) - static_cast<std::int64_t>(before.usage)) >> 20) + " MiB after creating and rendering to a 256 MiB resource");
        }
        view.reset();
        big.reset();
    }
    device.context->ClearState();
    device.context->Flush();
    static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
    VideoMemory after;
    bool fell = false;
    for (int poll = 0; poll < 100 && !fell; ++poll) {
        fell = DxgiBudget::query(target.luid, true, after, error) && after.usage + kSize / 2 <= during.usage;
        if (!fell) {
            ::Sleep(50);
        }
    }
    if (!fell) {
        problems.add("CurrentUsage did not fall by at least half of 256 MiB within 5 s of releasing the resource");
    }
    const std::uint64_t request = std::min<std::uint64_t>(before.available_for_reservation, 64ull << 20);
    if (request > 0) {
        const long result = DxgiBudget::set_reservation(target.luid, true, request);
        VideoMemory reserved;
        if (result < 0 || !DxgiBudget::query(target.luid, true, reserved, error) || reserved.reservation != request) {
            problems.add("SetVideoMemoryReservation(" + std::to_string(request >> 20) + " MiB) is not accepted or not reported back");
        }
        (void)DxgiBudget::set_reservation(target.luid, true, 0);
        if (before.available_for_reservation < UINT64_MAX / 2) {
            const long too_much = DxgiBudget::set_reservation(target.luid, true, before.available_for_reservation + (64ull << 30));
            if (too_much >= 0) {
                problems.add("a reservation far above AvailableForReservation is accepted");
                (void)DxgiBudget::set_reservation(target.luid, true, 0);
            }
        }
    }
    context.measure("budget_mib", before.budget >> 20);
    context.measure("usage_rise_mib", (during.usage - std::min(during.usage, before.usage)) >> 20);
    return problems.outcome();
}

Outcome MemChecks::offer_reclaim(Context& context) {
    AvAdapter target;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(target, error) || !device.create(target.luid, error)) {
        return Outcome::fail(error);
    }
    Com<IDXGIDevice2> dxgi2;
    device.device->QueryInterface(__uuidof(IDXGIDevice2), dxgi2.put_void());
    if (!dxgi2) {
        return Context::skip("IDXGIDevice2 is not exposed, which the API reports as an older DXGI runtime");
    }
    Problems problems;
    const DXGI_OFFER_RESOURCE_PRIORITY priorities[] = {DXGI_OFFER_RESOURCE_PRIORITY_LOW, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL, DXGI_OFFER_RESOURCE_PRIORITY_HIGH};
    Com<ID3D11Texture2D> texture = D3d11Tools::target(device, 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    Com<ID3D11PixelShader> flat = D3d11Tools::ps(device, "float4 main() : SV_Target { return float4(0.2, 0.4, 0.6, 1); }", error);
    if (!texture || !flat) {
        return Outcome::fail("scene objects: " + error);
    }
    int discarded_count = 0;
    for (int round = 0; round < 6; ++round) {
        {
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, texture.get());
            D3d11Tools::clear(device, view.get(), 0.2f, 0.4f, 0.6f, 1.0f);
        }
        device.context->ClearState();
        device.context->Flush();
        static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
        IDXGIResource* resources[1] = {nullptr};
        texture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&resources[0]));
        const HRESULT offered = dxgi2->OfferResources(1, resources, priorities[round % 3]);
        BOOL discarded = FALSE;
        const HRESULT reclaimed = dxgi2->ReclaimResources(1, resources, &discarded);
        resources[0]->Release();
        if (FAILED(offered) || FAILED(reclaimed)) {
            problems.add("round " + std::to_string(round) + ": OfferResources " + D3d11Tools::hr(offered) + ", ReclaimResources " + D3d11Tools::hr(reclaimed));
            continue;
        }
        if (discarded) {
            ++discarded_count;
        } else {
            TexelImage image;
            if (!D3d11Tools::read_texture(device, texture.get(), 0, image, error) || image.pixel(100, 100) != pack(51, 102, 153)) {
                problems.add("round " + std::to_string(round) + ": the resource was reported as not discarded but its content changed");
            }
        }
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, texture.get());
        static_cast<void>(D3d11Tools::draw_fullscreen(device, flat.get(), view.get(), 512, 512, error));
        TexelImage image;
        if (!D3d11Tools::read_texture(device, texture.get(), 0, image, error) || image.pixel(7, 7) != pack(51, 102, 153)) {
            problems.add("round " + std::to_string(round) + ": a draw after ReclaimResources does not render");
        }
    }
    context.measure("discarded_rounds", discarded_count);
    return problems.outcome();
}

Outcome MemChecks::trim_and_priority(Context& context) {
    AvAdapter target;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(target, error) || !device.create(target.luid, error)) {
        return Outcome::fail(error);
    }
    Com<IDXGIDevice3> dxgi3;
    device.device->QueryInterface(__uuidof(IDXGIDevice3), dxgi3.put_void());
    if (!dxgi3) {
        return Context::skip("IDXGIDevice3 is not exposed, which the API reports as an older DXGI runtime");
    }
    Problems problems;
    Com<ID3D11Texture2D> target_texture = D3d11Tools::target(device, 32, 32);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target_texture.get());
    auto renders = [&]() {
        D3d11Tools::clear(device, view.get(), 0.0f, 1.0f, 0.0f, 1.0f);
        TexelImage image;
        return D3d11Tools::read_texture(device, target_texture.get(), 0, image, error) && image.pixel(5, 5) == pack(0, 255, 0);
    };
    dxgi3->Trim();
    if (!renders()) {
        problems.add("rendering fails after IDXGIDevice3::Trim");
    }
    for (int priority = -7; priority <= 7; ++priority) {
        const HRESULT set = dxgi3->SetGPUThreadPriority(priority);
        INT back = 99;
        const HRESULT got = dxgi3->GetGPUThreadPriority(&back);
        if (FAILED(set)) {
            if (priority <= 0) {
                problems.add("SetGPUThreadPriority(" + std::to_string(priority) + ") failed with " + D3d11Tools::hr(set));
            } else {
                context.note("SetGPUThreadPriority(" + std::to_string(priority) + ") is refused with " + D3d11Tools::hr(set) + " (positive priorities need a privilege)");
            }
            continue;
        }
        if (FAILED(got) || back != priority) {
            problems.add("GetGPUThreadPriority returns " + std::to_string(back) + " after setting " + std::to_string(priority));
        }
    }
    dxgi3->SetGPUThreadPriority(0);
    for (int invalid : {-8, 8, 100, -100}) {
        if (SUCCEEDED(dxgi3->SetGPUThreadPriority(invalid))) {
            problems.add("SetGPUThreadPriority(" + std::to_string(invalid) + ") is accepted");
        }
    }
    INT unchanged = 99;
    dxgi3->GetGPUThreadPriority(&unchanged);
    if (unchanged != 0) {
        problems.add("a rejected priority changed the stored priority to " + std::to_string(unchanged));
    }
    dxgi3->Trim();
    dxgi3->Trim();
    if (!renders()) {
        problems.add("rendering fails after the priority and trim calls");
    }
    return problems.outcome();
}

Outcome MemChecks::overlay_caps_consistent(Context& context) {
    AvAdapter target;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(target, error) || !device.create(target.luid, error)) {
        return Outcome::fail(error);
    }
    Com<IDXGIOutput> output = first_output(target);
    if (!output) {
        return Outcome::fail("the adapter has no output");
    }
    Problems problems;
    Com<IDXGIOutput2> output2 = output.as<IDXGIOutput2>();
    Com<IDXGIOutput3> output3 = output.as<IDXGIOutput3>();
    Com<IDXGIOutput4> output4 = output.as<IDXGIOutput4>();
    Com<IDXGIOutput6> output6 = output.as<IDXGIOutput6>();
    if (!output3) {
        return Context::skip("IDXGIOutput3 is not exposed, which the API reports as an older DXGI runtime");
    }
    const BOOL supports = output2 ? output2->SupportsOverlays() : FALSE;
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_NV12, DXGI_FORMAT_YUY2, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_P010};
    bool any = false;
    for (DXGI_FORMAT format : formats) {
        UINT flags = 0;
        const HRESULT result = output3->CheckOverlaySupport(format, device.device, &flags);
        if (FAILED(result)) {
            problems.add("CheckOverlaySupport(" + std::to_string(format) + ") failed: " + D3d11Tools::hr(result));
            continue;
        }
        context.measure("overlay_flags_" + std::to_string(format), flags);
        any = any || flags != 0;
        if (!supports && flags != 0) {
            problems.add("SupportsOverlays is FALSE but format " + std::to_string(format) + " reports overlay flags 0x" + Text::hex(flags, 2));
        }
        if (output4) {
            const DXGI_COLOR_SPACE_TYPE spaces[] = {DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709};
            for (DXGI_COLOR_SPACE_TYPE space : spaces) {
                UINT color_flags = 0;
                if (SUCCEEDED(output4->CheckOverlayColorSpaceSupport(format, space, device.device, &color_flags)) && color_flags != 0 && flags == 0) {
                    problems.add("format " + std::to_string(format) + " has colour space support 0x" + Text::hex(color_flags, 2) + " in space " + std::to_string(space) + " but no overlay support");
                }
            }
        }
    }
    if (supports && !any) {
        problems.add("SupportsOverlays is TRUE but no tested format reports overlay support");
    }
    if (output6) {
        UINT hardware = 0;
        if (SUCCEEDED(output6->CheckHardwareCompositionSupport(&hardware))) {
            context.measure("hardware_composition_flags", hardware);
            if (!supports && (hardware & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED) != 0 && any) {
                problems.add("Output6 reports windowed hardware composition while SupportsOverlays is FALSE");
            }
        }
    }
    context.measure("supports_overlays", supports != FALSE);
    UINT direct = 0;
    output3->CheckOverlaySupport(DXGI_FORMAT_NV12, device.device, &direct);
    if ((direct & DXGI_OVERLAY_SUPPORT_FLAG_DIRECT) != 0) {
        SwapRig rig;
        SwapOptions options;
        options.format = DXGI_FORMAT_NV12;
        options.width = 128;
        options.height = 128;
        options.buffers = 2;
        options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (!rig.open(options, error)) {
            problems.add("an overlay-capable NV12 swap chain cannot be created: " + error);
        }
    } else {
        context.note("NV12 reports no direct overlay support; the swap chain part is not tested");
    }
    context.note("the MPO plane count of the display configuration is not exposed by the available headers and is not compared");
    return problems.outcome();
}

Outcome MemChecks::overlay_mpo_present(Context& context) {
    AvAdapter target;
    std::string error;
    if (!TargetAdapter::pick(target, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    SwapRig rig;
    SwapOptions options;
    options.format = DXGI_FORMAT_NV12;
    options.width = 128;
    options.height = 128;
    options.buffers = 2;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    options.flags = DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapRig probe;
    SwapOptions plain;
    plain.width = 128;
    plain.height = 128;
    if (!probe.open(plain, error)) {
        return Outcome::fail(error);
    }
    {
        const int want[3] = {10, 200, 90};
        if (!probe.fill(10 / 255.0f, 200 / 255.0f, 90 / 255.0f, 1.0f, error) || FAILED(probe.chain->Present(1, 0))) {
            return Outcome::fail("the composition fallback cannot present");
        }
        int got[3] = {};
        if (!probe.wait_for_color(64, 64, want, 4, 3000, got)) {
            problems.add("a plain chain shows " + std::to_string(got[0]) + "," + std::to_string(got[1]) + "," + std::to_string(got[2]) + " instead of the composed colour");
        }
    }
    UINT support = 0;
    Com<IDXGIOutput> output = first_output(target);
    Com<IDXGIOutput3> output3 = output ? output.as<IDXGIOutput3>() : Com<IDXGIOutput3>();
    if (output3) {
        output3->CheckOverlaySupport(DXGI_FORMAT_NV12, probe.device.device, &support);
    }
    TestWindow window;
    TestWindowConfig config;
    TestWindow::outer_size(config.x, config.y, 128, 128, config.width, config.height);
    Com<IDXGISwapChain1> chain;
    if (!window.create(config, error)) {
        return Outcome::fail(error);
    }
    const HRESULT created = probe.try_create(options, window.hwnd(), chain);
    context.measure("nv12_chain", D3d11Tools::hr(created));
    if (FAILED(created)) {
        if (support != 0) {
            problems.add("overlay support is reported for NV12 but creating the NV12 chain fails with " + D3d11Tools::hr(created));
        } else if (created != DXGI_ERROR_INVALID_CALL && created != DXGI_ERROR_UNSUPPORTED && created != E_INVALIDARG) {
            problems.add("an unsupported NV12 chain fails with the undocumented " + D3d11Tools::hr(created));
        }
        return problems.outcome();
    }
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(probe.device, D3d11Tools::desc2d(128, 128, DXGI_FORMAT_NV12, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE));
    Com<ID3D11Texture2D> buffer;
    chain->GetBuffer(0, __uuidof(ID3D11Texture2D), buffer.put_void());
    if (!staging || !buffer) {
        problems.add("an NV12 staging texture cannot be created");
        return problems.outcome();
    }
    int previous_luma = -1;
    for (int frame = 0; frame < 8; ++frame) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(probe.device.context->Map(staging.get(), 0, D3D11_MAP_WRITE, 0, &mapped))) {
            problems.add("the NV12 staging texture cannot be mapped");
            break;
        }
        const int y_level = 40 + frame * 24;
        for (UINT row = 0; row < 128; ++row) {
            std::memset(static_cast<std::uint8_t*>(mapped.pData) + row * mapped.RowPitch, y_level, 128);
        }
        for (UINT row = 0; row < 64; ++row) {
            std::memset(static_cast<std::uint8_t*>(mapped.pData) + (128 + row) * mapped.RowPitch, 128, 128);
        }
        probe.device.context->Unmap(staging.get(), 0);
        probe.device.context->CopyResource(buffer.get(), staging.get());
        if (FAILED(chain->Present(1, 0))) {
            problems.add("frame " + std::to_string(frame) + " cannot be presented");
            break;
        }
        POINT point{72, 72};
        ::ClientToScreen(window.hwnd(), &point);
        const int expected = static_cast<int>(std::lround((y_level - 16) * 255.0 / 219.0));
        int got[3] = {};
        bool matched = false;
        for (int poll = 0; poll < 100 && !matched; ++poll) {
            matched = SwapRig::screen_rgb(point.x, point.y, got) && std::abs(got[1] - expected) <= 10 && std::abs(got[0] - got[2]) <= 10;
            if (!matched) {
                ::Sleep(20);
            }
        }
        if (!matched) {
            problems.add("frame " + std::to_string(frame) + " shows " + std::to_string(got[0]) + "," + std::to_string(got[1]) + "," + std::to_string(got[2]) + " where grey " + std::to_string(expected) + " is expected");
            break;
        }
        if (got[1] <= previous_luma) {
            problems.add("the frames are not displayed in order: luma " + std::to_string(got[1]) + " follows " + std::to_string(previous_luma));
        }
        previous_luma = got[1];
    }
    return problems.outcome();
}

Outcome MemChecks::debug_no_messages(Context& context) {
    DynLibrary library;
    using GetDebug = HRESULT(WINAPI*)(REFIID, void**);
    GetDebug get = library.load("dxgidebug.dll") ? library.symbol<GetDebug>("DXGIGetDebugInterface1") : nullptr;
    if (get == nullptr) {
        return Context::skip("dxgidebug.dll is not installed (Graphics Tools), so the DXGI debug layer is unavailable");
    }
    Com<IDXGIInfoQueue> queue;
    using GetDebug1 = HRESULT(WINAPI*)(UINT, REFIID, void**);
    if (FAILED(reinterpret_cast<GetDebug1>(reinterpret_cast<void*>(get))(0, __uuidof(IDXGIInfoQueue), queue.put_void()))) {
        return Context::skip("the DXGI info queue cannot be obtained, so the debug layer is unavailable");
    }
    SwapRig rig;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    options.device_flags = D3D11_CREATE_DEVICE_DEBUG;
    std::string error;
    if (!rig.open(options, error)) {
        return Context::skip("a debug device cannot be created (" + error + "), so the SDK layers are unavailable");
    }
    Problems problems;
    queue->ClearStoredMessages(kDebugAll);
    for (int i = 0; i < 20; ++i) {
        if (!rig.fill(static_cast<float>(i) / 20.0f, 0.5f, 0.5f, 1.0f, error) || FAILED(rig.chain->Present(1, 0))) {
            problems.add("a valid frame cannot be presented");
            break;
        }
    }
    rig.device.context->ClearState();
    const UINT64 total = queue->GetNumStoredMessages(kDebugAll);
    int bad = 0;
    for (UINT64 i = 0; i < total; ++i) {
        SIZE_T length = 0;
        queue->GetMessage(kDebugAll, i, nullptr, &length);
        std::vector<std::uint8_t> bytes(length);
        auto* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(bytes.data());
        if (SUCCEEDED(queue->GetMessage(kDebugAll, i, message, &length)) &&
            (message->Severity == DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR || message->Severity == DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION)) {
            if (++bad <= 3) {
                problems.add("a valid call produced a DXGI error message: " + std::string(message->pDescription, message->DescriptionByteLength > 0 ? message->DescriptionByteLength - 1 : 0));
            }
        }
    }
    context.measure("valid_run_messages", static_cast<long long>(total));
    queue->ClearStoredMessages(kDebugAll);
    const HRESULT invalid = rig.chain->Present(1, 0x80000000u);
    if (SUCCEEDED(invalid)) {
        problems.add("Present with undefined flags is accepted");
    }
    if (queue->GetNumStoredMessages(kDebugAll) == 0) {
        problems.add("an invalid Present produced no debug message");
    } else {
        bool error_category = false;
        for (UINT64 i = 0; i < queue->GetNumStoredMessages(kDebugAll); ++i) {
            SIZE_T length = 0;
            queue->GetMessage(kDebugAll, i, nullptr, &length);
            std::vector<std::uint8_t> bytes(length);
            auto* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(bytes.data());
            if (SUCCEEDED(queue->GetMessage(kDebugAll, i, message, &length)) && message->Severity <= DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING) {
                error_category = true;
            }
        }
        if (!error_category) {
            problems.add("the message for an invalid call is not an error or warning");
        }
    }
    return problems.outcome();
}

Outcome MemChecks::removal_events(Context& context) {
    AvAdapter target;
    D3d11Device device;
    D3d12Device device12;
    std::string error;
    if (!TargetAdapter::pick(target, error) || !device.create(target.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    HANDLE event12 = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE event11 = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ID3D12Fence* fence = nullptr;
    if (device12.create(target.luid, error) && SUCCEEDED(device12.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence)))) {
        fence->SetEventOnCompletion(UINT64_MAX, event12);
    } else {
        context.note("no D3D12 device for the removal fence: " + error);
    }
    Com<ID3D11Device4> device4;
    device.device->QueryInterface(__uuidof(ID3D11Device4), device4.put_void());
    DWORD cookie = 0;
    const bool registered = device4 && SUCCEEDED(device4->RegisterDeviceRemovedEvent(event11, &cookie));
    Com<ID3D11Texture2D> texture = D3d11Tools::target(device, 64, 64);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, texture.get());
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "float4 main() : SV_Target { return float4(0.2, 0.4, 0.6, 1); }", error);
    if (!view || !ps) {
        return Outcome::fail("scene objects: " + error);
    }
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int frames = 0;
    while (std::chrono::steady_clock::now() < end) {
        D3d11Tools::clear(device, view.get(), 0, 0, 0, 1);
        static_cast<void>(D3d11Tools::draw_fullscreen(device, ps.get(), view.get(), 64, 64, error));
        TexelImage image;
        if (!D3d11Tools::read_texture(device, texture.get(), 0, image, error) || image.pixel(3, 3) != pack(51, 102, 153)) {
            problems.add("rendering failed during the 5 s loop: " + error);
            break;
        }
        if (::WaitForSingleObject(event12, 0) == WAIT_OBJECT_0 || ::WaitForSingleObject(event11, 0) == WAIT_OBJECT_0) {
            problems.add("a device removed event fired on a healthy device");
            break;
        }
        ++frames;
        ::Sleep(20);
    }
    if (device.device->GetDeviceRemovedReason() != S_OK) {
        problems.add("GetDeviceRemovedReason is not S_OK on a healthy D3D11 device");
    }
    if (device12.device != nullptr && device12.device->GetDeviceRemovedReason() != S_OK) {
        problems.add("GetDeviceRemovedReason is not S_OK on a healthy D3D12 device");
    }
    if (registered) {
        device4->UnregisterDeviceRemoved(cookie);
    }
    if (fence != nullptr) {
        fence->Release();
    }
    ::CloseHandle(event12);
    ::CloseHandle(event11);
    context.measure("frames", frames);
    context.note("the firing half of the removal notification is exercised by sanity.d3d11.err.device_removed");
    return problems.outcome();
}

Outcome MemChecks::duplication_visible(Context& context) {
    AvAdapter target;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(target, error) || !device.create(target.luid, error)) {
        return Outcome::fail(error);
    }
    Com<IDXGIOutput> output = first_output(target);
    Com<IDXGIOutput1> output1 = output ? output.as<IDXGIOutput1>() : Com<IDXGIOutput1>();
    if (!output1) {
        return Outcome::fail("the adapter has no IDXGIOutput1");
    }
    Com<IDXGIOutputDuplication> duplication;
    Com<IDXGIOutput5> output5 = output.as<IDXGIOutput5>();
    const DXGI_FORMAT duplication_formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
    const HRESULT created = output5 ? output5->DuplicateOutput1(device.device, 0, 1, duplication_formats, duplication.put())
                                    : output1->DuplicateOutput(device.device, duplication.put());
    context.measure("duplicate_output1", static_cast<bool>(output5));
    if (FAILED(created)) {
        if (created == DXGI_ERROR_UNSUPPORTED || created == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || created == E_NOTIMPL) {
            return Context::skip("output duplication is reported as not available (" + D3d11Tools::hr(created) + ")");
        }
        return Outcome::fail("DuplicateOutput failed: " + D3d11Tools::hr(created));
    }
    Problems problems;
    DXGI_OUTPUT_DESC od{};
    output->GetDesc(&od);
    TestWindow window;
    TestWindowConfig config;
    config.x = od.DesktopCoordinates.left + 100;
    config.y = od.DesktopCoordinates.top + 100;
    TestWindow::outer_size(config.x, config.y, 184, 161, config.width, config.height);
    if (!window.create(config, error)) {
        return Outcome::fail(error);
    }
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RECT client{};
    ::GetClientRect(window.hwnd(), &client);
    POINT origin{50, 50};
    ::ClientToScreen(window.hwnd(), &origin);
    Com<ID3D11Texture2D> staging;
    bool matched = false;
    std::uint32_t last = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int acquired_frames = 0;
    long long last_present_time = 0;
    while (!matched && std::chrono::steady_clock::now() < deadline) {
        HDC dc = ::GetDC(window.hwnd());
        HBRUSH brush = ::CreateSolidBrush(RGB(200, 30, 90));
        ::FillRect(dc, &client, brush);
        ::DeleteObject(brush);
        ::ReleaseDC(window.hwnd(), dc);
        DXGI_OUTDUPL_FRAME_INFO info{};
        Com<IDXGIResource> resource;
        const HRESULT acquired = duplication->AcquireNextFrame(200, &info, resource.put());
        if (acquired == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }
        if (SUCCEEDED(acquired)) {
            ++acquired_frames;
            last_present_time = info.LastPresentTime.QuadPart;
        }
        if (FAILED(acquired)) {
            problems.add("AcquireNextFrame returned " + D3d11Tools::hr(acquired));
            break;
        }
        Com<ID3D11Texture2D> frame;
        resource->QueryInterface(__uuidof(ID3D11Texture2D), frame.put_void());
        if (frame) {
            D3D11_TEXTURE2D_DESC fd{};
            frame->GetDesc(&fd);
            if (!staging) {
                staging = D3d11Tools::texture(device, D3d11Tools::desc2d(fd.Width, fd.Height, fd.Format, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
            }
            device.context->CopyResource(staging.get(), frame.get());
            TexelImage image;
            if (D3d11Tools::read_texture(device, staging.get(), 0, image, error)) {
                last = image.pixel(static_cast<UINT>(origin.x - od.DesktopCoordinates.left), static_cast<UINT>(origin.y - od.DesktopCoordinates.top));
                matched = (last & 0xFFFFFFu) == 0xC81E5Au;
            }
        }
        frame.reset();
        resource.reset();
        const HRESULT released = duplication->ReleaseFrame();
        if (FAILED(released)) {
            problems.add("ReleaseFrame returned " + D3d11Tools::hr(released));
            break;
        }
    }
    context.measure("acquired_frames", acquired_frames);
    context.measure("last_present_time", last_present_time);
    if (!matched && problems.empty()) {
        problems.add("no duplicated frame shows the topmost window colour; the pixel reads 0x" + Text::hex(last, 8));
    }
    if (duplication->ReleaseFrame() != DXGI_ERROR_INVALID_CALL) {
        problems.add("ReleaseFrame without an acquired frame does not return DXGI_ERROR_INVALID_CALL");
    }
    {
        DXGI_OUTDUPL_FRAME_INFO info{};
        Com<IDXGIResource> first;
        Com<IDXGIResource> second;
        ::FillRect(::GetDC(window.hwnd()), &client, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
        if (duplication->AcquireNextFrame(1000, &info, first.put()) == S_OK) {
            const HRESULT again = duplication->AcquireNextFrame(50, &info, second.put());
            if (again != DXGI_ERROR_INVALID_CALL) {
                problems.add("a second AcquireNextFrame without ReleaseFrame returned " + D3d11Tools::hr(again));
            }
            duplication->ReleaseFrame();
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::dxgi
