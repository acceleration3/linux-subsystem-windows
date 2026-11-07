#include "sanity/interop/d3d_process_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/interop/kit.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_tools.hpp"

#include <cstring>
#include <thread>

namespace lsw::suites::sanity::interop {

namespace {

constexpr UINT kEdge = 64;
constexpr int kRounds = 40;

template <typename Fence>
bool reached(Fence* fence, std::uint64_t value, DWORD milliseconds) {
    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        return false;
    }
    bool done = fence->GetCompletedValue() >= value;
    if (!done && SUCCEEDED(fence->SetEventOnCompletion(value, event))) {
        done = ::WaitForSingleObject(event, milliseconds) == WAIT_OBJECT_0;
    }
    ::CloseHandle(event);
    return done;
}

D3D11_TEXTURE2D_DESC shared_desc(UINT edge, UINT misc) {
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(edge, edge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = misc;
    return desc;
}

bool read_bytes(D3d11Device& device, ID3D11Texture2D* texture, std::vector<std::uint8_t>& out, std::string& error) {
    TexelImage image;
    if (!D3d11Tools::read_texture(device, texture, 0, image, error)) {
        return false;
    }
    out = image.data;
    return true;
}

std::vector<std::uint8_t> inverted(std::vector<std::uint8_t> data) {
    for (std::uint8_t& byte : data) {
        byte = static_cast<std::uint8_t>(byte ^ 0xA5u);
    }
    return data;
}

struct Names {
    std::string process;
    std::string texture;
    std::string fence;
    std::string name;
    std::string ready;
    std::string opened;
};

std::string env(const wchar_t* name) {
    return ChildMode::variable(name);
}

Outcome consume(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create_ex(adapter.luid, nullptr, 0, 0, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11Device1> device1 = Kit::query<ID3D11Device1>(device.device);
    Com<ID3D11Device5> device5 = Kit::query<ID3D11Device5>(device.device);
    Com<ID3D11DeviceContext4> context4 = Kit::query<ID3D11DeviceContext4>(device.context);
    HANDLE texture_handle = Kit::adopt_handle(env(L"LSW_SUITES_XP_PID"), env(L"LSW_SUITES_XP_TEXTURE"));
    HANDLE fence_handle = Kit::adopt_handle(env(L"LSW_SUITES_XP_PID"), env(L"LSW_SUITES_XP_FENCE"));
    if (!device1 || !device5 || !context4 || texture_handle == nullptr || fence_handle == nullptr) {
        return Outcome::fail("the consumer cannot duplicate the producer's handles or lacks D3D11.4");
    }
    Com<ID3D11Texture2D> by_handle;
    Com<ID3D11Texture2D> by_name;
    Com<ID3D11Fence> fence;
    const HRESULT a = device1->OpenSharedResource1(texture_handle, __uuidof(ID3D11Texture2D), by_handle.put_void());
    const HRESULT b = device1->OpenSharedResourceByName(Text::wide(env(L"LSW_SUITES_XP_NAME")).c_str(), DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, __uuidof(ID3D11Texture2D), by_name.put_void());
    const HRESULT c = device5->OpenSharedFence(fence_handle, __uuidof(ID3D11Fence), fence.put_void());
    ::CloseHandle(texture_handle);
    ::CloseHandle(fence_handle);
    if (FAILED(a) || FAILED(b) || FAILED(c)) {
        return Outcome::fail("opening in the consumer failed: duplicated texture handle " + Kit::hr(a) + ", texture by name " + Kit::hr(b) + ", duplicated fence " + Kit::hr(c));
    }
    Outcome outcome = Outcome::ok();
    for (int round = 0; round < kRounds; ++round) {
        const std::uint64_t odd = static_cast<std::uint64_t>(round) * 2 + 1;
        if (!reached(fence.get(), odd, 20000)) {
            outcome = Outcome::fail("round " + std::to_string(round) + ": the producer never signalled fence value " + std::to_string(odd));
            break;
        }
        context4->Wait(fence.get(), odd);
        std::vector<std::uint8_t> seen;
        std::vector<std::uint8_t> seen_by_name;
        const std::vector<std::uint8_t> want = Kit::frame_bytes(Kit::seed(70), round, kEdge, kEdge);
        if (!read_bytes(device, by_handle.get(), seen, error) || !read_bytes(device, by_name.get(), seen_by_name, error) || seen != want || seen_by_name != want) {
            outcome = Outcome::fail("round " + std::to_string(round) + ": the consumer reads different pixels than the producer wrote after the fence" + (error.empty() ? "" : ": " + error));
            break;
        }
        const std::vector<std::uint8_t> reply = inverted(want);
        device.context->UpdateSubresource(by_handle.get(), 0, nullptr, reply.data(), kEdge * 4u, 0);
        context4->Signal(fence.get(), odd + 1);
        device.context->Flush();
    }
    static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
    context.measure("consumed_rounds", kRounds);
    return outcome;
}

Outcome produce(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create_ex(adapter.luid, nullptr, 0, 0, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, shared_desc(kEdge, D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE));
    Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
    HANDLE handle = nullptr;
    if (!texture || !resource ||
        FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, Text::wide(env(L"LSW_SUITES_XP_NAME")).c_str(), &handle))) {
        return Outcome::fail("the producer cannot create the named shared texture");
    }
    const std::vector<std::uint8_t> data = Kit::frame_bytes(Kit::seed(71), 0, kEdge, kEdge);
    device.context->UpdateSubresource(texture.get(), 0, nullptr, data.data(), kEdge * 4u, 0);
    device.context->Flush();
    if (!D3d11Tools::wait_event(device, device.context, error)) {
        return Outcome::fail(error);
    }
    HANDLE ready = ::OpenEventW(EVENT_ALL_ACCESS, FALSE, Text::wide(env(L"LSW_SUITES_XP_READY")).c_str());
    HANDLE opened = ::OpenEventW(EVENT_ALL_ACCESS, FALSE, Text::wide(env(L"LSW_SUITES_XP_OPENED")).c_str());
    if (ready == nullptr || opened == nullptr) {
        return Outcome::fail("the producer cannot open the hand-off events");
    }
    ::SetEvent(ready);
    const bool taken = ::WaitForSingleObject(opened, 30000) == WAIT_OBJECT_0;
    static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
    ::CloseHandle(ready);
    ::CloseHandle(opened);
    ::CloseHandle(handle);
    context.measure("producer_exited_after_open", taken);
    return taken ? Outcome::ok() : Outcome::fail("the consumer never confirmed that it opened the texture");
}

Outcome sharing(Context& context, const char* id, bool sibling) {
    const std::string role = env(L"LSW_SUITES_XP_ROLE");
    if (ChildMode::active() && role == "consumer") {
        const Outcome outcome = consume(context);
        context.measure("role_done", true);
        return outcome;
    }
    if (ChildMode::active() && role == "producer") {
        const Outcome outcome = produce(context);
        context.measure("role_done", true);
        return outcome;
    }
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("api excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create_ex(adapter.luid, nullptr, 0, 0, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11Device1> device1 = Kit::query<ID3D11Device1>(device.device);
    Com<ID3D11Device5> device5 = Kit::query<ID3D11Device5>(device.device);
    Com<ID3D11DeviceContext4> context4 = Kit::query<ID3D11DeviceContext4>(device.context);
    if (!device1 || !device5 || !context4) {
        return Context::skip("ID3D11Device5 is not exposed, so shared fences are not offered");
    }
    Problems problems;
    const std::string stem = "Local\\lsw-suites-xp-" + std::to_string(::GetCurrentProcessId()) + (sibling ? "-b" : "-p");
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, shared_desc(kEdge, D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE));
    Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
    Com<ID3D11Fence> fence;
    HANDLE texture_handle = nullptr;
    HANDLE fence_handle = nullptr;
    if (!texture || !resource || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, Text::wide(stem).c_str(), &texture_handle)) ||
        FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), fence.put_void())) || FAILED(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle))) {
        return Outcome::fail("the shared texture or the shared fence cannot be created");
    }
    ChildRunOptions options;
    options.timeout_seconds = 150;
    options.env = {{"LSW_SUITES_XP_ROLE", "consumer"},
                   {"LSW_SUITES_XP_PID", std::to_string(::GetCurrentProcessId())},
                   {"LSW_SUITES_XP_TEXTURE", Kit::handle_text(texture_handle)},
                   {"LSW_SUITES_XP_FENCE", Kit::handle_text(fence_handle)},
                   {"LSW_SUITES_XP_NAME", stem}};
    CheckResult child;
    std::string sibling_error;
    bool launched = true;
    std::thread runner([&] {
        if (sibling) {
            launched = ChildRuns::run_sibling(id, options, child, sibling_error);
        } else {
            child = ChildRuns::run_self(id, options);
        }
    });
    int completed = 0;
    for (int round = 0; round < kRounds; ++round) {
        const std::uint64_t odd = static_cast<std::uint64_t>(round) * 2 + 1;
        const std::vector<std::uint8_t> data = Kit::frame_bytes(Kit::seed(70), round, kEdge, kEdge);
        device.context->UpdateSubresource(texture.get(), 0, nullptr, data.data(), kEdge * 4u, 0);
        context4->Signal(fence.get(), odd);
        device.context->Flush();
        if (!reached(fence.get(), odd + 1, 20000)) {
            problems.add("round " + std::to_string(round) + ": the consumer process never signalled fence value " + std::to_string(odd + 1));
            break;
        }
        context4->Wait(fence.get(), odd + 1);
        std::vector<std::uint8_t> seen;
        if (!read_bytes(device, texture.get(), seen, error) || seen != inverted(data)) {
            problems.add("round " + std::to_string(round) + ": the producer reads something other than the consumer's reply after the fence" + (error.empty() ? "" : ": " + error));
            break;
        }
        ++completed;
    }
    runner.join();
    static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
    if (sibling && !launched) {
        problems.add("the " + std::string(sizeof(void*) == 8 ? "x86" : "x64") + " process did not run: " + sibling_error);
    } else if (child.status != Status::Ok) {
        problems.add("the consumer process failed: " + child.reason);
    }
    context.measure("rounds_completed", completed);
    ::CloseHandle(texture_handle);
    ::CloseHandle(fence_handle);
    fence.reset();
    texture.reset();
    resource.reset();
    HANDLE ready = ::CreateEventW(nullptr, TRUE, FALSE, Text::wide(stem + "-ready").c_str());
    HANDLE opened = ::CreateEventW(nullptr, TRUE, FALSE, Text::wide(stem + "-opened").c_str());
    ChildRunOptions producer_options;
    producer_options.timeout_seconds = 120;
    producer_options.env = {{"LSW_SUITES_XP_ROLE", "producer"}, {"LSW_SUITES_XP_NAME", stem + "-tex"}, {"LSW_SUITES_XP_READY", stem + "-ready"}, {"LSW_SUITES_XP_OPENED", stem + "-opened"}};
    CheckResult producer;
    bool producer_launched = true;
    std::thread second([&] {
        if (sibling) {
            producer_launched = ChildRuns::run_sibling(id, producer_options, producer, sibling_error);
        } else {
            producer = ChildRuns::run_self(id, producer_options);
        }
    });
    Com<ID3D11Texture2D> adopted;
    if (ready == nullptr || opened == nullptr || ::WaitForSingleObject(ready, 60000) != WAIT_OBJECT_0) {
        problems.add("the producer process never announced its texture");
    } else if (FAILED(device1->OpenSharedResourceByName(Text::wide(stem + "-tex").c_str(), DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, __uuidof(ID3D11Texture2D), adopted.put_void()))) {
        problems.add("the texture created by the other process cannot be opened by name");
    }
    if (opened != nullptr) {
        ::SetEvent(opened);
    }
    second.join();
    if (sibling && !producer_launched) {
        problems.add("the " + std::string(sizeof(void*) == 8 ? "x86" : "x64") + " producer did not run: " + sibling_error);
    } else if (producer.status != Status::Ok) {
        problems.add("the producer process failed: " + producer.reason);
    }
    if (adopted) {
        const std::vector<std::uint8_t> want = Kit::frame_bytes(Kit::seed(71), 0, kEdge, kEdge);
        std::vector<std::uint8_t> seen;
        if (!read_bytes(device, adopted.get(), seen, error) || seen != want) {
            problems.add("after the producer process exited the texture it created does not hold its pixels" + (error.empty() ? "" : ": " + error));
        }
        const std::vector<std::uint8_t> fresh = Kit::frame_bytes(Kit::seed(72), 0, kEdge, kEdge);
        device.context->UpdateSubresource(adopted.get(), 0, nullptr, fresh.data(), kEdge * 4u, 0);
        if (!read_bytes(device, adopted.get(), seen, error) || seen != fresh) {
            problems.add("the texture is not writable after its producer exited");
        }
        static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
        adopted.reset();
    }
    if (ready != nullptr) {
        ::CloseHandle(ready);
    }
    if (opened != nullptr) {
        ::CloseHandle(opened);
    }
    return problems.outcome();
}

}  // namespace

Outcome D3dProcessChecks::cross_process_sharing(Context& context) {
    return sharing(context, "sanity.interop.d3d.cross_process_sharing", false);
}

Outcome D3dProcessChecks::cross_bitness_sharing(Context& context) {
    return sharing(context, "sanity.interop.d3d.cross_bitness_sharing", true);
}

Outcome D3dProcessChecks::cross_adapter_render_only(Context& context) {
    std::string error;
    std::vector<AvAdapter> adapters = D3d12Rig::target_adapters(error);
    if (adapters.size() < 2) {
        return Context::skip("fewer than two accel-virt adapters exist, so there is no render-only plus display pair");
    }
    const AvAdapter* display = nullptr;
    const AvAdapter* render = nullptr;
    for (const AvAdapter& adapter : adapters) {
        if (display == nullptr && adapter.has_outputs()) {
            display = &adapter;
        }
    }
    for (const AvAdapter& adapter : adapters) {
        if (display != nullptr && adapter.luid_key() != display->luid_key() && render == nullptr) {
            render = &adapter;
        }
    }
    if (display == nullptr || render == nullptr) {
        return Context::skip("no pair of a display adapter and another adapter exists");
    }
    D3d12Rig display_rig;
    D3d12Rig render_rig;
    if (!display_rig.open(*display, D3D_FEATURE_LEVEL_11_0, error) || !render_rig.open(*render, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    constexpr UINT kSide = 128;
    constexpr UINT64 kBytes = static_cast<UINT64>(kSide) * kSide * 4;
    HRESULT made = S_OK;
    D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(kBytes, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER);
    ID3D12Resource* source = D3d12Tools::committed(render_rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), static_cast<D3D12_HEAP_FLAGS>(D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER), desc,
                                                   D3D12_RESOURCE_STATE_COMMON, nullptr, &made);
    if (source == nullptr) {
        return Context::skip("a cross-adapter shared buffer cannot be created on the render adapter (" + Kit::hr(made) + "), so cross-adapter sharing is not offered");
    }
    HANDLE handle = nullptr;
    Com<ID3D12Resource> opened_owner;
    Com<ID3D12Fence> fence_render;
    Com<ID3D12Fence> fence_display;
    HANDLE fence_handle = nullptr;
    if (FAILED(render_rig.device()->CreateSharedHandle(source, nullptr, GENERIC_ALL, nullptr, &handle)) ||
        FAILED(display_rig.device()->OpenSharedHandle(handle, __uuidof(ID3D12Resource), opened_owner.put_void())) ||
        FAILED(render_rig.device()->CreateFence(0, static_cast<D3D12_FENCE_FLAGS>(D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER), __uuidof(ID3D12Fence), fence_render.put_void())) ||
        FAILED(render_rig.device()->CreateSharedHandle(fence_render.get(), nullptr, GENERIC_ALL, nullptr, &fence_handle)) ||
        FAILED(display_rig.device()->OpenSharedHandle(fence_handle, __uuidof(ID3D12Fence), fence_display.put_void()))) {
        if (handle != nullptr) {
            ::CloseHandle(handle);
        }
        if (fence_handle != nullptr) {
            ::CloseHandle(fence_handle);
        }
        return Outcome::fail("the cross-adapter buffer or fence cannot be shared with the display adapter");
    }
    ::CloseHandle(handle);
    ::CloseHandle(fence_handle);
    ID3D12Resource* imported = display_rig.hold(std::move(opened_owner));
    ID3D12Fence* render_fence = render_rig.hold(std::move(fence_render));
    ID3D12Fence* display_fence = display_rig.hold(std::move(fence_display));
    TestWindow window;
    TestWindowConfig config;
    config.x = 60;
    config.y = 60;
    TestWindow::outer_size(config.x, config.y, kSide, kSide, config.width, config.height);
    if (!window.create(config, error)) {
        static_cast<void>(display_rig.idle(error));
        display_rig.shutdown();
        return Outcome::fail(error);
    }
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    DXGI_SWAP_CHAIN_DESC1 chain_desc{};
    chain_desc.Width = kSide;
    chain_desc.Height = kSide;
    chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    chain_desc.SampleDesc.Count = 1;
    chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    chain_desc.BufferCount = 2;
    chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    chain_desc.Scaling = DXGI_SCALING_STRETCH;
    Com<IDXGISwapChain1> chain1;
    made = display_rig.factory->CreateSwapChainForHwnd(display_rig.direct().queue(), window.hwnd(), &chain_desc, nullptr, nullptr, chain1.put());
    Com<IDXGISwapChain3> chain = chain1.as<IDXGISwapChain3>();
    Problems problems;
    ID3D12Resource* buffers[2] = {nullptr, nullptr};
    if (FAILED(made) || !chain) {
        problems.add("CreateSwapChainForHwnd on the display adapter returned " + Kit::hr(made));
    } else {
        for (UINT i = 0; i < 2; ++i) {
            Com<ID3D12Resource> buffer;
            if (FAILED(chain->GetBuffer(i, __uuidof(ID3D12Resource), buffer.put_void()))) {
                problems.add("GetBuffer failed");
                break;
            }
            buffers[i] = display_rig.hold(std::move(buffer));
        }
    }
    ID3D12Resource* upload = D3d12Tools::upload(render_rig, kBytes);
    if (upload == nullptr) {
        problems.add("the upload buffer cannot be created on the render adapter");
    }
    for (int frame = 0; problems.empty() && frame < 5 && upload != nullptr; ++frame) {
        const std::vector<std::uint32_t> pixels = FrameImage::render(frame + 2, kSide, kSide);
        void* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        upload->Map(0, &none, &mapped);
        std::memcpy(mapped, pixels.data(), kBytes);
        upload->Unmap(0, nullptr);
        std::uint64_t value = 0;
        if (!render_rig.direct().run(
                [&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                    list->CopyBufferRegion(source, 0, upload, 0, kBytes);
                    D3d12Tools::move(list, source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                },
                value, error)) {
            problems.add("frame " + std::to_string(frame) + ": the render adapter cannot fill the shared buffer: " + error);
            break;
        }
        const std::uint64_t signalled = static_cast<std::uint64_t>(frame) + 1;
        render_rig.direct().queue()->Signal(render_fence, signalled);
        render_rig.direct().touch();
        if (!reached(display_fence, signalled, 20000)) {
            problems.add("frame " + std::to_string(frame) + ": the cross-adapter fence never reached " + std::to_string(signalled) + " on the display adapter");
            break;
        }
        display_rig.direct().gpu_wait_fence(display_fence, signalled);
        const UINT index = chain->GetCurrentBackBufferIndex();
        if (index > 1 || !display_rig.direct().run_wait(
                             [&](ID3D12GraphicsCommandList* list) {
                                 D3d12Tools::move(list, buffers[index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
                                 D3D12_TEXTURE_COPY_LOCATION destination{};
                                 destination.pResource = buffers[index];
                                 destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                 D3D12_TEXTURE_COPY_LOCATION from{};
                                 from.pResource = imported;
                                 from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                 from.PlacedFootprint.Footprint = {DXGI_FORMAT_B8G8R8A8_UNORM, kSide, kSide, 1, kSide * 4};
                                 list->CopyTextureRegion(&destination, 0, 0, 0, &from, nullptr);
                                 D3d12Tools::move(list, buffers[index], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
                             },
                             error)) {
            problems.add("frame " + std::to_string(frame) + ": the display adapter cannot copy the shared buffer into its back buffer: " + error);
            break;
        }
        made = chain->Present(1, 0);
        if (FAILED(made)) {
            problems.add("frame " + std::to_string(frame) + ": Present returned " + Kit::hr(made));
            break;
        }
        static_cast<void>(display_rig.idle(error));
        std::string detail;
        if (!Kit::settle_screen(context, "cross_adapter_" + std::to_string(frame), ScreenCapture::client_on_screen(window.hwnd()), pixels, 0, 3000, detail)) {
            problems.add("frame " + std::to_string(frame) + " rendered on one adapter and presented on the other is wrong: " + detail);
        }
    }
    static_cast<void>(display_rig.idle(error));
    static_cast<void>(render_rig.idle(error));
    for (ID3D12Resource*& buffer : buffers) {
        if (buffer != nullptr) {
            display_rig.release(buffer);
            buffer = nullptr;
        }
    }
    chain.reset();
    chain1.reset();
    display_rig.shutdown();
    window.destroy();
    render_rig.shutdown();
    return problems.outcome();
}

Outcome D3dProcessChecks::handle_lifetime(Context& context) {
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("api excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device consumer;
    if (!TargetAdapter::pick(adapter, error) || !consumer.create_ex(adapter.luid, nullptr, 0, 0, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11Device1> consumer1 = Kit::query<ID3D11Device1>(consumer.device);
    Problems problems;
    constexpr int kCount = 8;
    constexpr UINT kSide = 128;
    int created_total = 0;
    for (int round = 0; round < 6 && problems.empty(); ++round) {
        std::vector<Com<ID3D11Texture2D>> opened;
        {
            D3d11Device creator;
            if (!creator.create_ex(adapter.luid, nullptr, 0, 0, error)) {
                problems.add("round " + std::to_string(round) + ": the creator device cannot be created: " + error);
                break;
            }
            std::vector<Com<ID3D11Texture2D>> made;
            std::vector<HANDLE> handles;
            for (int i = 0; i < kCount; ++i) {
                const bool nt = (i & 1) != 0;
                Com<ID3D11Texture2D> texture = D3d11Tools::texture(creator, shared_desc(kSide, nt ? (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE) : D3D11_RESOURCE_MISC_SHARED));
                HANDLE handle = nullptr;
                if (!texture) {
                    problems.add("round " + std::to_string(round) + ": creating shared texture " + std::to_string(i) + " failed");
                    break;
                }
                creator.context->UpdateSubresource(texture.get(), 0, nullptr, Kit::frame_bytes(Kit::seed(80, i), round, kSide, kSide).data(), kSide * 4u, 0);
                Com<ID3D11Texture2D> remote;
                if (nt) {
                    Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
                    if (!resource || FAILED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) {
                        problems.add("round " + std::to_string(round) + ": CreateSharedHandle of texture " + std::to_string(i) + " failed");
                        break;
                    }
                    handles.push_back(handle);
                } else {
                    Com<IDXGIResource> resource = Kit::query<IDXGIResource>(texture.get());
                    if (!resource || FAILED(resource->GetSharedHandle(&handle)) || handle == nullptr) {
                        problems.add("round " + std::to_string(round) + ": GetSharedHandle of texture " + std::to_string(i) + " failed");
                        break;
                    }
                }
                creator.context->Flush();
                std::string wait_error;
                static_cast<void>(D3d11Tools::wait_event(creator, creator.context, wait_error));
                const HRESULT result = nt ? consumer1->OpenSharedResource1(handle, __uuidof(ID3D11Texture2D), remote.put_void())
                                          : consumer.device->OpenSharedResource(handle, __uuidof(ID3D11Texture2D), remote.put_void());
                if (FAILED(result)) {
                    problems.add("round " + std::to_string(round) + ": opening shared texture " + std::to_string(i) + " returned " + Kit::hr(result));
                    break;
                }
                opened.push_back(std::move(remote));
                made.push_back(std::move(texture));
                ++created_total;
            }
            for (HANDLE handle : handles) {
                ::CloseHandle(handle);
            }
            std::string wait_error;
            static_cast<void>(D3d11Tools::wait_event(creator, creator.context, wait_error));
            made.clear();
        }
        for (std::size_t i = 0; i < opened.size() && problems.empty(); ++i) {
            std::vector<std::uint8_t> seen;
            if (!read_bytes(consumer, opened[i].get(), seen, error) || seen != Kit::frame_bytes(Kit::seed(80, static_cast<int>(i)), round, kSide, kSide)) {
                problems.add("round " + std::to_string(round) + ": texture " + std::to_string(i) + " does not hold its data after its creator closed the handle, released it and destroyed its device" + (error.empty() ? "" : ": " + error));
            }
        }
        static_cast<void>(D3d11Tools::wait_event(consumer, consumer.context, error));
        opened.clear();
        consumer.context->Flush();
    }
    context.measure("textures_created_and_opened", created_total);
    if (problems.empty() && created_total != 6 * kCount) {
        problems.add("only " + std::to_string(created_total) + " of " + std::to_string(6 * kCount) + " shared textures could be created and opened across the rounds");
    }
    return problems.outcome();
}

Outcome D3dProcessChecks::invalid_open_errors(Context& context) {
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("api excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device av;
    D3d11Device warp;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (!TargetAdapter::pick(adapter, error) || !av.create_ex(adapter.luid, nullptr, 0, 0, error) || !warp.create_warp(&level, 1, 0, error)) {
        return Outcome::fail("the adapter or the WARP oracle is unavailable: " + error);
    }
    Problems problems;
    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE closed = nullptr;
    {
        Com<ID3D11Texture2D> texture = D3d11Tools::texture(av, shared_desc(16, D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE));
        Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
        if (resource && SUCCEEDED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &closed))) {
            ::CloseHandle(closed);
        }
    }
    const struct {
        const char* name;
        HANDLE handle;
    } handles[] = {{"a NULL handle", nullptr}, {"a garbage handle", reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x7FFF0))}, {"a closed handle", closed}, {"an event handle", event}};
    for (const auto& item : handles) {
        for (int api = 0; api < 2; ++api) {
            HRESULT results[2] = {S_OK, S_OK};
            D3d11Device* devices[2] = {&av, &warp};
            for (int side = 0; side < 2; ++side) {
                Com<ID3D11Texture2D> out;
                if (api == 0) {
                    results[side] = devices[side]->device->OpenSharedResource(item.handle, __uuidof(ID3D11Texture2D), out.put_void());
                } else {
                    Com<ID3D11Device1> device1 = Kit::query<ID3D11Device1>(devices[side]->device);
                    results[side] = device1 ? device1->OpenSharedResource1(item.handle, __uuidof(ID3D11Texture2D), out.put_void()) : E_NOINTERFACE;
                }
                if (SUCCEEDED(results[side])) {
                    problems.add(std::string(api == 0 ? "OpenSharedResource" : "OpenSharedResource1") + " of " + item.name + " succeeds on " + (side == 0 ? "the adapter" : "WARP"));
                }
            }
            context.measure(std::string(api == 0 ? "legacy." : "nt.") + Kit::sanitize(item.name) + ".adapter", Kit::hr(results[0]));
            context.measure(std::string(api == 0 ? "legacy." : "nt.") + Kit::sanitize(item.name) + ".warp", Kit::hr(results[1]));
            if (FAILED(results[0]) && FAILED(results[1]) && results[0] != results[1]) {
                problems.add(std::string(api == 0 ? "OpenSharedResource" : "OpenSharedResource1") + " of " + item.name + " returns " + Kit::hr(results[0]) + " on the adapter but " + Kit::hr(results[1]) + " on WARP");
            }
            Com<ID3D11Texture2D> probe = D3d11Tools::target(av, 8, 8);
            Com<ID3D11RenderTargetView> view = probe ? D3d11Tools::rtv(av, probe.get()) : Com<ID3D11RenderTargetView>();
            if (!view) {
                problems.add("the device cannot create a render target after opening " + std::string(item.name));
            } else {
                D3d11Tools::clear(av, view.get(), 0.25f, 0.5f, 0.75f, 1.0f);
                TexelImage image;
                if (!D3d11Tools::wait_event(av, av.context, error) || !D3d11Tools::read_texture(av, probe.get(), 0, image, error) || (image.pixel(3, 3) & 0xFFFFFFu) != 0xBF8040u) {
                    problems.add(std::string("a clear on the device after opening ") + item.name + " is wrong" + (error.empty() ? "" : ": " + error));
                }
            }
        }
    }
    if (event != nullptr) {
        ::CloseHandle(event);
    }
    if (!ApiPolicy::is_skipped("d3d12")) {
        D3d12Rig av12;
        D3d12Rig warp12;
        if (!av12.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error) || !warp12.open_warp(D3D_FEATURE_LEVEL_11_0, error)) {
            context.note("D3D12 invalid handle opening is not judged: " + error);
        } else {
            HANDLE event12 = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
            const struct {
                const char* name;
                HANDLE handle;
            } handles12[] = {{"a NULL handle", nullptr}, {"a garbage handle", reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x7FFF0))}, {"an event handle", event12}};
            for (const auto& item : handles12) {
                Com<ID3D12Resource> out_av;
                Com<ID3D12Resource> out_warp;
                const HRESULT a = av12.device()->OpenSharedHandle(item.handle, __uuidof(ID3D12Resource), out_av.put_void());
                const HRESULT b = warp12.device()->OpenSharedHandle(item.handle, __uuidof(ID3D12Resource), out_warp.put_void());
                if (SUCCEEDED(a)) {
                    problems.add(std::string("D3D12 OpenSharedHandle of ") + item.name + " succeeds");
                } else if (SUCCEEDED(b) || a != b) {
                    problems.add(std::string("D3D12 OpenSharedHandle of ") + item.name + " returns " + Kit::hr(a) + " on the adapter but " + Kit::hr(b) + " on WARP");
                }
            }
            if (event12 != nullptr) {
                ::CloseHandle(event12);
            }
            std::string idle_error;
            static_cast<void>(av12.idle(idle_error));
            static_cast<void>(warp12.idle(idle_error));
        }
    }
    static_cast<void>(D3d11Tools::wait_event(av, av.context, error));
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
