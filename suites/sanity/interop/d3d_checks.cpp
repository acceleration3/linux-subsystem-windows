#include "sanity/interop/d3d_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/interop/kit.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_tools.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/dyn_library.hpp"

#include <d3d11on12.h>

#include <atomic>
#include <cstring>
#include <thread>

namespace lsw::suites::sanity::interop {

namespace {

constexpr UINT kEdge = 64;

template <typename Fence>
bool wait_fence(Fence* fence, std::uint64_t value, DWORD milliseconds) {
    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        return false;
    }
    bool reached = fence->GetCompletedValue() >= value;
    if (!reached && SUCCEEDED(fence->SetEventOnCompletion(value, event))) {
        reached = ::WaitForSingleObject(event, milliseconds) == WAIT_OBJECT_0;
    }
    ::CloseHandle(event);
    return reached;
}

D3D11_TEXTURE2D_DESC shared_desc(UINT edge, UINT misc, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(edge, edge, format, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = misc;
    return desc;
}

bool put(D3d11Device& device, ID3D11Texture2D* texture, const std::vector<std::uint8_t>& data, UINT edge) {
    device.context->UpdateSubresource(texture, 0, nullptr, data.data(), edge * 4u, 0);
    return true;
}

bool take(D3d11Device& device, ID3D11Texture2D* texture, std::vector<std::uint8_t>& out, std::string& error) {
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

struct Bridge {
    D3d11Device a;
    Com<ID3D11Device5> a5;
    Com<ID3D11DeviceContext4> ac;
    D3d12Rig rig;
    ID3D12Resource* res12 = nullptr;
    ID3D12Fence* f12 = nullptr;
    Com<ID3D11Fence> f11;
    Com<ID3D11Texture2D> texture;
    std::string skip;

    ~Bridge() { close(); }

    bool open(const AvAdapter& adapter, std::string& error) {
        if (!a.create_ex(adapter.luid, nullptr, 0, 0, error)) {
            return false;
        }
        a5 = Kit::query<ID3D11Device5>(a.device);
        ac = Kit::query<ID3D11DeviceContext4>(a.context);
        if (!a5 || !ac) {
            skip = "ID3D11Device5 or ID3D11DeviceContext4 is not exposed, so shared fences are not offered";
            return false;
        }
        if (!rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            return false;
        }
        texture = D3d11Tools::texture(a, shared_desc(kEdge, D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE));
        Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
        HANDLE handle = nullptr;
        if (!texture || !resource || FAILED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) {
            error = "a D3D11 texture with SHARED_NTHANDLE and a shared handle cannot be created";
            return false;
        }
        Com<ID3D12Resource> opened;
        const HRESULT opened_result = rig.device()->OpenSharedHandle(handle, __uuidof(ID3D12Resource), opened.put_void());
        ::CloseHandle(handle);
        if (FAILED(opened_result)) {
            error = "ID3D12Device::OpenSharedHandle of the D3D11 NT shared texture returned " + Kit::hr(opened_result);
            return false;
        }
        res12 = rig.hold(std::move(opened));
        Com<ID3D12Fence> fence;
        HANDLE fence_handle = nullptr;
        if (FAILED(rig.device()->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), fence.put_void())) ||
            FAILED(rig.device()->CreateSharedHandle(fence.get(), nullptr, GENERIC_ALL, nullptr, &fence_handle))) {
            error = "a shared D3D12 fence cannot be created";
            return false;
        }
        f12 = rig.hold(std::move(fence));
        const HRESULT fence_result = a5->OpenSharedFence(fence_handle, __uuidof(ID3D11Fence), f11.put_void());
        ::CloseHandle(fence_handle);
        if (FAILED(fence_result)) {
            error = "ID3D11Device5::OpenSharedFence of the D3D12 fence returned " + Kit::hr(fence_result);
            return false;
        }
        return true;
    }

    void close() {
        std::string error;
        if (a.device != nullptr) {
            static_cast<void>(D3d11Tools::wait_event(a, a.context, error));
        }
        if (rig.device() != nullptr) {
            static_cast<void>(rig.idle(error));
        }
        f11.reset();
        rig.shutdown();
        texture.reset();
        ac.reset();
        a5.reset();
        a.destroy();
    }
};

}  // namespace

Outcome D3dChecks::d3d9ex_shared_to_d3d11(Context& context) {
    if (ApiPolicy::is_skipped("d3d9") || ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("this row needs both Direct3D 9Ex and Direct3D 11");
    }
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    settings.visible = false;
    if (!rig.open(true, error)) {
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    if (!rig.create(settings, error)) {
        return Outcome::fail(error);
    }
    D3d11Device d3d11;
    if (!d3d11.create(rig.adapter.luid, error)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device9 = rig.dev();
    HANDLE handle = nullptr;
    HRESULT made = S_OK;
    Com<IDirect3DTexture9> texture = D3d9Tools::texture(device9, kEdge, kEdge, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &made, &handle);
    if (!texture) {
        return Outcome::fail("a shared render target texture cannot be created on the Direct3D 9Ex device: " + Kit::hr(made));
    }
    if (handle == nullptr) {
        return Outcome::fail("CreateTexture with a shared handle request succeeded without returning a handle");
    }
    Com<ID3D11Texture2D> opened;
    const HRESULT result = d3d11.device->OpenSharedResource(handle, __uuidof(ID3D11Texture2D), opened.put_void());
    if (FAILED(result)) {
        return Outcome::fail("ID3D11Device::OpenSharedResource of the Direct3D 9Ex shared texture returned " + Kit::hr(result));
    }
    Problems problems;
    D3D11_TEXTURE2D_DESC desc{};
    opened->GetDesc(&desc);
    if (desc.Width != kEdge || desc.Height != kEdge || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        problems.add("the opened texture is " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " format " + std::to_string(desc.Format) + ", expected 64x64 B8G8R8A8_UNORM");
    }
    Com<IDirect3DSurface9> level = D3d9Tools::level(texture.get(), 0);
    Com<IDirect3DSurface9> saved;
    device9->GetRenderTarget(0, saved.put());
    for (int frame = 0; frame < 8 && problems.empty(); ++frame) {
        device9->SetRenderTarget(0, level.get());
        for (int q = 0; q < 4; ++q) {
            const RECT r = FrameImage::quadrant(kEdge, kEdge, q);
            const D3DRECT rect{r.left, r.top, r.right, r.bottom};
            device9->Clear(1, &rect, D3DCLEAR_TARGET, FrameImage::color(frame, q), 1.0f, 0);
        }
        device9->SetRenderTarget(0, saved.get());
        if (!D3d9Tools::wait_event(device9, error)) {
            problems.add(error);
            break;
        }
        TexelImage image;
        if (!D3d11Tools::read_texture(d3d11, opened.get(), 0, image, error)) {
            problems.add("frame " + std::to_string(frame) + ": " + error);
            break;
        }
        for (UINT y = 0; y < kEdge && problems.empty(); ++y) {
            for (UINT x = 0; x < kEdge; ++x) {
                const std::uint32_t want = FrameImage::at(frame, kEdge, kEdge, static_cast<int>(x), static_cast<int>(y));
                if ((image.pixel(x, y) & 0xFFFFFFu) != (want & 0xFFFFFFu)) {
                    problems.add("frame " + std::to_string(frame) + ": D3D11 reads pixel (" + std::to_string(x) + "," + std::to_string(y) + ") as " + Kit::rgb(image.pixel(x, y)) +
                                 " after the D3D9Ex event query completed, D3D9Ex rendered " + Kit::rgb(want));
                    break;
                }
            }
        }
    }
    static_cast<void>(D3d11Tools::wait_event(d3d11, d3d11.context, error));
    opened.reset();
    static_cast<void>(D3d9Tools::wait_event(device9, error));
    static_cast<void>(context);
    return problems.outcome();
}

Outcome D3dChecks::d3d11_nt_handle_d3d12(Context& context) {
    if (ApiPolicy::is_skipped("d3d11") || ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("this row needs both Direct3D 11 and Direct3D 12");
    }
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    Bridge bridge;
    if (!bridge.open(adapter, error)) {
        const std::string skip = bridge.skip;
        bridge.close();
        return skip.empty() ? Outcome::fail(error) : Context::skip(skip);
    }
    Problems problems;
    for (int round = 0; round < 4 && problems.empty(); ++round) {
        const std::vector<std::uint8_t> first = Kit::frame_bytes(Kit::seed(50), round, kEdge, kEdge);
        const std::vector<std::uint8_t> second = Kit::frame_bytes(Kit::seed(51), round, kEdge, kEdge);
        put(bridge.a, bridge.texture.get(), first, kEdge);
        bridge.a.context->Flush();
        if (!D3d11Tools::wait_event(bridge.a, bridge.a.context, error)) {
            problems.add(error);
            break;
        }
        TexelImage image;
        if (!D3d12Tools::read_texture(bridge.rig, bridge.res12, D3D12_RESOURCE_STATE_COMMON, 0, image, error)) {
            problems.add("round " + std::to_string(round) + ": reading the D3D11 texture in D3D12 failed: " + error);
            break;
        }
        if (image.data != first) {
            problems.add("round " + std::to_string(round) + ": D3D12 reads different pixels than D3D11 wrote");
            ArtifactWriter writer(context);
            writer.bytes("d3d12_read." + std::to_string(round), image.data.data(), image.data.size());
            writer.bytes("d3d11_wrote." + std::to_string(round), first.data(), first.size());
        }
        if (!D3d12Tools::write_texture(bridge.rig, bridge.res12, D3D12_RESOURCE_STATE_COMMON, 0, second.data(), error)) {
            problems.add("round " + std::to_string(round) + ": writing the shared texture in D3D12 failed: " + error);
            break;
        }
        const std::uint64_t value = static_cast<std::uint64_t>(round) + 1;
        bridge.rig.direct().queue()->Signal(bridge.f12, value);
        bridge.rig.direct().touch();
        if (!wait_fence(bridge.f11.get(), value, 20000)) {
            problems.add("round " + std::to_string(round) + ": the shared fence never reached " + std::to_string(value) + " in D3D11 after D3D12 signalled it");
            break;
        }
        bridge.ac->Wait(bridge.f11.get(), value);
        std::vector<std::uint8_t> back;
        if (!take(bridge.a, bridge.texture.get(), back, error)) {
            problems.add(error);
            break;
        }
        if (back != second) {
            problems.add("round " + std::to_string(round) + ": D3D11 does not read what D3D12 wrote before signalling the shared fence");
        }
        if (bridge.f11->GetCompletedValue() < value) {
            problems.add("round " + std::to_string(round) + ": the opened fence reports a completed value below " + std::to_string(value) + " after the wait completed");
        }
    }
    bridge.close();
    return problems.outcome();
}

Outcome D3dChecks::shared_fence_ordering(Context& context) {
    constexpr int kSteps = 1000;
    constexpr UINT kSmall = 16;
    Problems problems;
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device a;
        D3d11Device b;
        if (!a.create_ex(adapter.luid, nullptr, 0, 0, error) || !b.create_ex(adapter.luid, nullptr, 0, 0, error)) {
            return Outcome::fail(error);
        }
        Com<ID3D11Device5> a5 = Kit::query<ID3D11Device5>(a.device);
        Com<ID3D11Device5> b5 = Kit::query<ID3D11Device5>(b.device);
        Com<ID3D11Device1> b1 = Kit::query<ID3D11Device1>(b.device);
        Com<ID3D11DeviceContext4> ac = Kit::query<ID3D11DeviceContext4>(a.context);
        Com<ID3D11DeviceContext4> bc = Kit::query<ID3D11DeviceContext4>(b.context);
        if (!a5 || !b5 || !b1 || !ac || !bc) {
            context.note("shared fences are not exposed by this D3D11 runtime");
        } else {
            Com<ID3D11Fence> fa;
            Com<ID3D11Fence> fb;
            HANDLE fence_handle = nullptr;
            Com<ID3D11Texture2D> ta = D3d11Tools::texture(a, shared_desc(kSmall, D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE));
            Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(ta.get());
            HANDLE texture_handle = nullptr;
            Com<ID3D11Texture2D> tb;
            if (FAILED(a5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), fa.put_void())) || FAILED(fa->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle)) ||
                FAILED(b5->OpenSharedFence(fence_handle, __uuidof(ID3D11Fence), fb.put_void())) || !ta || !resource ||
                FAILED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &texture_handle)) || FAILED(b1->OpenSharedResource1(texture_handle, __uuidof(ID3D11Texture2D), tb.put_void()))) {
                problems.add("D3D11 to D3D11: the shared fence or texture cannot be set up");
            } else {
                for (int step = 0; step < kSteps; ++step) {
                    const std::vector<std::uint8_t> first = Kit::frame_bytes(Kit::seed(52), step, kSmall, kSmall);
                    const std::vector<std::uint8_t> second = inverted(first);
                    const std::uint64_t odd = static_cast<std::uint64_t>(step) * 2 + 1;
                    put(a, ta.get(), first, kSmall);
                    ac->Signal(fa.get(), odd);
                    a.context->Flush();
                    if (!wait_fence(fb.get(), odd, 20000)) {
                        problems.add("D3D11 to D3D11 step " + std::to_string(step) + ": fence value " + std::to_string(odd) + " never reached the second device");
                        break;
                    }
                    bc->Wait(fb.get(), odd);
                    std::vector<std::uint8_t> seen;
                    if (!take(b, tb.get(), seen, error) || seen != first) {
                        problems.add("D3D11 to D3D11 step " + std::to_string(step) + ": the second device does not see the first device's data after the fence" + (error.empty() ? "" : ": " + error));
                        break;
                    }
                    put(b, tb.get(), second, kSmall);
                    bc->Signal(fb.get(), odd + 1);
                    b.context->Flush();
                    if (!wait_fence(fa.get(), odd + 1, 20000)) {
                        problems.add("D3D11 to D3D11 step " + std::to_string(step) + ": fence value " + std::to_string(odd + 1) + " never reached the first device");
                        break;
                    }
                    ac->Wait(fa.get(), odd + 1);
                    if (!take(a, ta.get(), seen, error) || seen != second) {
                        problems.add("D3D11 to D3D11 step " + std::to_string(step) + ": the first device does not see the second device's data after the fence" + (error.empty() ? "" : ": " + error));
                        break;
                    }
                }
            }
            if (fence_handle != nullptr) {
                ::CloseHandle(fence_handle);
            }
            if (texture_handle != nullptr) {
                ::CloseHandle(texture_handle);
            }
            static_cast<void>(D3d11Tools::wait_event(b, b.context, error));
            static_cast<void>(D3d11Tools::wait_event(a, a.context, error));
            tb.reset();
            fb.reset();
            ta.reset();
            fa.reset();
        }
    }
    if (!ApiPolicy::is_skipped("d3d11") && !ApiPolicy::is_skipped("d3d12")) {
        Bridge bridge;
        if (!bridge.open(adapter, error)) {
            if (bridge.skip.empty()) {
                problems.add("D3D11 to D3D12: " + error);
            }
        } else {
            for (int step = 0; step < kSteps; ++step) {
                const std::vector<std::uint8_t> first = Kit::frame_bytes(Kit::seed(53), step, kEdge, kEdge);
                const std::vector<std::uint8_t> second = inverted(first);
                const std::uint64_t odd = static_cast<std::uint64_t>(step) * 2 + 1;
                put(bridge.a, bridge.texture.get(), first, kEdge);
                bridge.ac->Signal(bridge.f11.get(), odd);
                bridge.a.context->Flush();
                if (!wait_fence(bridge.f12, odd, 20000)) {
                    problems.add("D3D11 to D3D12 step " + std::to_string(step) + ": fence value " + std::to_string(odd) + " never reached D3D12");
                    break;
                }
                bridge.rig.direct().gpu_wait_fence(bridge.f12, odd);
                TexelImage image;
                if (!D3d12Tools::read_texture(bridge.rig, bridge.res12, D3D12_RESOURCE_STATE_COMMON, 0, image, error) || image.data != first) {
                    problems.add("D3D11 to D3D12 step " + std::to_string(step) + ": D3D12 does not see D3D11's data after the fence" + (error.empty() ? "" : ": " + error));
                    break;
                }
                if (!D3d12Tools::write_texture(bridge.rig, bridge.res12, D3D12_RESOURCE_STATE_COMMON, 0, second.data(), error)) {
                    problems.add("D3D11 to D3D12 step " + std::to_string(step) + ": " + error);
                    break;
                }
                bridge.rig.direct().queue()->Signal(bridge.f12, odd + 1);
                bridge.rig.direct().touch();
                if (!wait_fence(bridge.f11.get(), odd + 1, 20000)) {
                    problems.add("D3D11 to D3D12 step " + std::to_string(step) + ": fence value " + std::to_string(odd + 1) + " never reached D3D11");
                    break;
                }
                bridge.ac->Wait(bridge.f11.get(), odd + 1);
                std::vector<std::uint8_t> seen;
                if (!take(bridge.a, bridge.texture.get(), seen, error) || seen != second) {
                    problems.add("D3D11 to D3D12 step " + std::to_string(step) + ": D3D11 does not see D3D12's data after the fence" + (error.empty() ? "" : ": " + error));
                    break;
                }
                if (bridge.f11->GetCompletedValue() < odd + 1) {
                    problems.add("D3D11 to D3D12 step " + std::to_string(step) + ": the opened fence reports a completed value below " + std::to_string(odd + 1) + " after the wait");
                    break;
                }
            }
            bridge.close();
        }
    }
    return problems.outcome();
}

Outcome D3dChecks::keyed_mutex(Context& context) {
    const std::string name_text = ChildMode::variable(L"LSW_SUITES_KM_NAME");
    constexpr int kFrames = 50;
    AvAdapter adapter;
    std::string error;
    if (ChildMode::active() && !name_text.empty()) {
        D3d11Device device;
        if (!TargetAdapter::pick(adapter, error) || !device.create_ex(adapter.luid, nullptr, 0, 0, error)) {
            return Outcome::fail(error);
        }
        Com<ID3D11Device1> device1 = Kit::query<ID3D11Device1>(device.device);
        Com<ID3D11Texture2D> opened;
        if (!device1 || FAILED(device1->OpenSharedResourceByName(Text::wide(name_text).c_str(), DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, __uuidof(ID3D11Texture2D), opened.put_void()))) {
            return Outcome::fail("the consumer process cannot open the keyed mutex texture by name");
        }
        Com<IDXGIKeyedMutex> mutex = Kit::query<IDXGIKeyedMutex>(opened.get());
        if (!mutex) {
            return Outcome::fail("the opened texture has no keyed mutex");
        }
        Outcome outcome = Outcome::ok();
        for (int frame = 0; frame < kFrames; ++frame) {
            const HRESULT acquired = mutex->AcquireSync(1, 20000);
            if (acquired != S_OK) {
                outcome = Outcome::fail("frame " + std::to_string(frame) + ": AcquireSync(1) returned " + Kit::hr(acquired));
                break;
            }
            std::vector<std::uint8_t> seen;
            const bool read = take(device, opened.get(), seen, error);
            mutex->ReleaseSync(0);
            if (!read || seen != Kit::frame_bytes(Kit::seed(60), frame, kEdge, kEdge)) {
                outcome = Outcome::fail("frame " + std::to_string(frame) + " arrived wrong or stale in the consumer process" + (error.empty() ? "" : ": " + error));
                break;
            }
        }
        static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
        return outcome;
    }
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    if (ApiPolicy::is_skipped("d3d11")) {
        return Context::skip("api excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    D3d11Device producer;
    D3d11Device consumer;
    if (!producer.create_ex(adapter.luid, nullptr, 0, 0, error) || !consumer.create_ex(adapter.luid, nullptr, 0, 0, error)) {
        return Outcome::fail(error);
    }
    const UINT misc = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(producer, shared_desc(kEdge, misc));
    Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
    Com<IDXGIKeyedMutex> producer_mutex = Kit::query<IDXGIKeyedMutex>(texture.get());
    const std::string name = "Local\\lsw-suites-interop-km-" + std::to_string(::GetCurrentProcessId());
    HANDLE handle = nullptr;
    if (!texture || !resource || !producer_mutex || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, Text::wide(name).c_str(), &handle))) {
        return Outcome::fail("a keyed mutex texture with a named NT handle cannot be created");
    }
    Com<ID3D11Device1> consumer1 = Kit::query<ID3D11Device1>(consumer.device);
    Com<ID3D11Texture2D> opened;
    if (!consumer1 || FAILED(consumer1->OpenSharedResource1(handle, __uuidof(ID3D11Texture2D), opened.put_void()))) {
        ::CloseHandle(handle);
        return Outcome::fail("the keyed mutex texture cannot be opened in a second device");
    }
    Com<IDXGIKeyedMutex> consumer_mutex = Kit::query<IDXGIKeyedMutex>(opened.get());
    if (!consumer_mutex) {
        ::CloseHandle(handle);
        return Outcome::fail("the second device has no keyed mutex on the opened texture");
    }
    const HRESULT wrong_key = consumer_mutex->AcquireSync(7, 150);
    if (wrong_key != static_cast<HRESULT>(WAIT_TIMEOUT)) {
        problems.add("AcquireSync with key 7 while the mutex holds key 0 returned " + Kit::hr(wrong_key) + " instead of WAIT_TIMEOUT");
        if (SUCCEEDED(wrong_key)) {
            consumer_mutex->ReleaseSync(0);
        }
    }
    std::atomic<bool> producer_ok{true};
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    std::string consumer_error;
    std::thread producer_thread([&] {
        for (int frame = 0; frame < 200; ++frame) {
            if (stop) {
                break;
            }
            if (producer_mutex->AcquireSync(0, 20000) != S_OK) {
                producer_ok = false;
                break;
            }
            put(producer, texture.get(), Kit::frame_bytes(Kit::seed(61), frame, kEdge, kEdge), kEdge);
            producer.context->Flush();
            producer_mutex->ReleaseSync(1);
        }
        finished = true;
    });
    for (int frame = 0; frame < 200; ++frame) {
        if (consumer_mutex->AcquireSync(1, 20000) != S_OK) {
            consumer_error = "frame " + std::to_string(frame) + ": the consumer never acquired key 1";
            break;
        }
        std::vector<std::uint8_t> seen;
        const bool read = take(consumer, opened.get(), seen, error);
        consumer_mutex->ReleaseSync(0);
        if (!read || seen != Kit::frame_bytes(Kit::seed(61), frame, kEdge, kEdge)) {
            consumer_error = "frame " + std::to_string(frame) + " read after AcquireSync(1) is not the frame the producer wrote before ReleaseSync(1)";
            break;
        }
    }
    if (!consumer_error.empty()) {
        problems.add(consumer_error);
        stop = true;
        while (!finished) {
            if (consumer_mutex->AcquireSync(1, 100) == S_OK) {
                consumer_mutex->ReleaseSync(0);
            }
        }
    }
    producer_thread.join();
    if (!producer_ok) {
        problems.add("the producer thread could not acquire key 0 in time");
    }
    static_cast<void>(D3d11Tools::wait_event(consumer, consumer.context, error));
    static_cast<void>(D3d11Tools::wait_event(producer, producer.context, error));
    if (producer_mutex->AcquireSync(0, 5000) == S_OK) {
        producer_mutex->ReleaseSync(0);
    }
    CheckResult child;
    ChildRunOptions options;
    options.timeout_seconds = 150;
    options.env = {{"LSW_SUITES_KM_NAME", name}};
    std::thread runner([&] { child = ChildRuns::run_self("sanity.interop.d3d.keyed_mutex", options); });
    for (int frame = 0; frame < kFrames; ++frame) {
        if (producer_mutex->AcquireSync(0, 20000) != S_OK) {
            problems.add("cross-process frame " + std::to_string(frame) + ": the producer's AcquireSync(0) timed out");
            break;
        }
        put(producer, texture.get(), Kit::frame_bytes(Kit::seed(60), frame, kEdge, kEdge), kEdge);
        producer.context->Flush();
        producer_mutex->ReleaseSync(1);
    }
    runner.join();
    if (child.status != Status::Ok) {
        problems.add("the consumer process failed: " + child.reason);
    }
    static_cast<void>(D3d11Tools::wait_event(producer, producer.context, error));
    opened.reset();
    consumer_mutex.reset();
    ::CloseHandle(handle);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome D3dChecks::d3d11on12(Context& context) {
    if (ApiPolicy::is_skipped("d3d11") || ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("this row needs both Direct3D 11 and Direct3D 12");
    }
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    DynLibrary library;
    using CreateOn12 = HRESULT(WINAPI*)(IUnknown*, UINT, const D3D_FEATURE_LEVEL*, UINT, IUnknown* const*, UINT, UINT, ID3D11Device**, ID3D11DeviceContext**, D3D_FEATURE_LEVEL*);
    CreateOn12 create = library.load("d3d11.dll") ? library.symbol<CreateOn12>("D3D11On12CreateDevice") : nullptr;
    if (create == nullptr) {
        return Context::skip("d3d11.dll has no D3D11On12CreateDevice");
    }
    TestWindow window;
    TestWindowConfig config;
    config.x = 60;
    config.y = 60;
    TestWindow::outer_size(config.x, config.y, 128, 128, config.width, config.height);
    if (!window.create(config, error)) {
        return Outcome::fail(error);
    }
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 128;
    desc.Height = 128;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    Com<IDXGISwapChain1> chain1;
    HRESULT result = rig.factory->CreateSwapChainForHwnd(rig.direct().queue(), window.hwnd(), &desc, nullptr, nullptr, chain1.put());
    Com<IDXGISwapChain3> chain = chain1.as<IDXGISwapChain3>();
    if (FAILED(result) || !chain) {
        return Outcome::fail("CreateSwapChainForHwnd on a D3D12 queue returned " + Kit::hr(result));
    }
    ID3D12Resource* buffers[2] = {nullptr, nullptr};
    for (UINT i = 0; i < 2; ++i) {
        Com<ID3D12Resource> buffer;
        if (FAILED(chain->GetBuffer(i, __uuidof(ID3D12Resource), buffer.put_void()))) {
            return Outcome::fail("GetBuffer failed");
        }
        buffers[i] = rig.hold(std::move(buffer));
    }
    D3d11Device device11;
    IUnknown* queues[1] = {rig.direct().queue()};
    result = create(rig.device(), D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, queues, 1, 0, &device11.device, &device11.context, &device11.level);
    if (FAILED(result)) {
        return Outcome::fail("D3D11On12CreateDevice returned " + Kit::hr(result));
    }
    Com<ID3D11On12Device> on12 = Kit::query<ID3D11On12Device>(device11.device);
    Problems problems;
    Com<ID3D11Texture2D> wrapped[2];
    if (!on12) {
        problems.add("the D3D11On12 device does not expose ID3D11On12Device");
    } else {
        for (UINT i = 0; i < 2; ++i) {
            D3D11_RESOURCE_FLAGS flags{};
            flags.BindFlags = D3D11_BIND_RENDER_TARGET;
            result = on12->CreateWrappedResource(buffers[i], &flags, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT, __uuidof(ID3D11Texture2D), wrapped[i].put_void());
            if (FAILED(result)) {
                problems.add("CreateWrappedResource of swap chain buffer " + std::to_string(i) + " returned " + Kit::hr(result));
            }
        }
    }
    D3d11FrameDraw draw;
    if (problems.empty() && !draw.open(device11, error)) {
        problems.add(error);
    }
    for (int frame = 0; problems.empty() && frame < 6; ++frame) {
        const UINT index = chain->GetCurrentBackBufferIndex();
        ID3D11Resource* acquired[1] = {wrapped[index].get()};
        on12->AcquireWrappedResources(acquired, 1);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device11, wrapped[index].get());
        if (!view) {
            problems.add("no render target view over the wrapped back buffer");
            on12->ReleaseWrappedResources(acquired, 1);
            break;
        }
        draw.frame(view.get(), 128, 128, frame + 4);
        device11.context->OMSetRenderTargets(0, nullptr, nullptr);
        view.reset();
        on12->ReleaseWrappedResources(acquired, 1);
        device11.context->Flush();
        result = chain->Present(1, 0);
        if (FAILED(result)) {
            problems.add("frame " + std::to_string(frame) + ": Present returned " + Kit::hr(result));
            break;
        }
        std::string wait_error;
        static_cast<void>(rig.idle(wait_error));
        std::string detail;
        if (!Kit::settle_screen(context, "on12_frame_" + std::to_string(frame), ScreenCapture::client_on_screen(window.hwnd()), FrameImage::render(frame + 4, 128, 128), 0, 3000, detail)) {
            problems.add("frame " + std::to_string(frame) + " rendered by D3D11 on D3D12 and presented by D3D12 is wrong: " + detail);
        }
    }
    static_cast<void>(rig.idle(error));
    draw.release();
    wrapped[0].reset();
    wrapped[1].reset();
    on12.reset();
    device11.destroy();
    static_cast<void>(rig.idle(error));
    for (ID3D12Resource*& buffer : buffers) {
        if (buffer != nullptr) {
            rig.release(buffer);
            buffer = nullptr;
        }
    }
    chain.reset();
    chain1.reset();
    rig.shutdown();
    window.destroy();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
