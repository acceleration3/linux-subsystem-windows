#include "sanity/d3d11/share_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d11/kit.hpp"
#include "support/child_runs.hpp"

#include <thread>

namespace lsw::suites::sanity::d3d11 {

namespace {

D3D11_TEXTURE2D_DESC shared_desc(UINT misc) {
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = misc;
    return desc;
}

bool read_color(D3d11Device& d, ID3D11Texture2D* texture, std::uint32_t& center, bool& uniform, std::string& error) {
    TexelImage image;
    if (!D3d11Tools::read_texture(d, texture, 0, image, error)) {
        return false;
    }
    center = image.pixel(32, 32);
    uniform = image.pixel(0, 0) == center && image.pixel(63, 63) == center && image.pixel(0, 63) == center && image.pixel(63, 0) == center;
    return true;
}

std::uint32_t frame_color(int k) {
    return Kit::rgba((k * 2 + 1) & 255, k & 255, 255 - (k & 255), 255);
}

void clear_frame(D3d11Device& d, ID3D11RenderTargetView* view, int k) {
    D3d11Tools::clear(d, view, static_cast<float>((k * 2 + 1) & 255) / 255.0f, static_cast<float>(k & 255) / 255.0f, static_cast<float>(255 - (k & 255)) / 255.0f, 1.0f);
}

bool open_by_name(D3d11Device& d, const std::wstring& name, Com<ID3D11Texture2D>& out) {
    Com<ID3D11Device1> device1 = Kit::query<ID3D11Device1>(d.device);
    return device1 && SUCCEEDED(device1->OpenSharedResourceByName(name.c_str(), DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, __uuidof(ID3D11Texture2D), out.put_void()));
}

}  // namespace

Outcome ShareChecks::legacy_handle(Context& context) {
    AvAdapter adapter;
    D3d11Device producer;
    D3d11Device consumer;
    std::string error;
    if (!Kit::open(adapter, producer, error) || !consumer.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(producer, shared_desc(D3D11_RESOURCE_MISC_SHARED));
    Com<IDXGIResource> resource = Kit::query<IDXGIResource>(texture.get());
    HANDLE handle = nullptr;
    if (!texture || !resource || FAILED(resource->GetSharedHandle(&handle)) || handle == nullptr) {
        return Outcome::fail("a MISC_SHARED texture has no shared handle");
    }
    Com<ID3D11Texture2D> opened;
    const HRESULT result = consumer.device->OpenSharedResource(handle, __uuidof(ID3D11Texture2D), opened.put_void());
    if (FAILED(result)) {
        return Outcome::fail("OpenSharedResource failed: " + D3d11Tools::hr(result));
    }
    D3D11_TEXTURE2D_DESC a{};
    D3D11_TEXTURE2D_DESC b{};
    texture->GetDesc(&a);
    opened->GetDesc(&b);
    if (a.Width != b.Width || a.Height != b.Height || a.Format != b.Format) {
        problems.add("the opened shared texture has a different description");
    }
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(producer, texture.get());
    Com<ID3D11PixelShader> ps = Kit::color_ps(producer, "0.5, 0.25, 1.0, 1.0", error);
    for (int k = 0; k < 4; ++k) {
        if (k == 3) {
            static_cast<void>(D3d11Tools::draw_fullscreen(producer, ps.get(), view.get(), 64, 64, error));
        } else {
            clear_frame(producer, view.get(), k * 20);
        }
        producer.context->Flush();
        if (!D3d11Tools::wait_event(producer, producer.context, error)) {
            return Outcome::fail(error);
        }
        std::uint32_t center = 0;
        bool uniform = false;
        if (!read_color(consumer, opened.get(), center, uniform, error)) {
            return Outcome::fail(error);
        }
        const std::uint32_t want = k == 3 ? Kit::rgba(128, 64, 255, 255) : frame_color(k * 20);
        if (center != want || !uniform) {
            problems.add("frame " + std::to_string(k) + " read through the second device is 0x" + Text::hex(center, 8) + ", expected 0x" + Text::hex(want, 8));
        }
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome ShareChecks::nt_handle(Context& context) {
    const std::string name_text = ChildMode::variable(L"LSW_SUITES_SHARE_NAME");
    AvAdapter adapter;
    std::string error;
    if (ChildMode::active() && !name_text.empty()) {
        D3d11Device device;
        if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
            return Outcome::fail(error);
        }
        Com<ID3D11Texture2D> opened;
        if (!open_by_name(device, Text::wide(name_text), opened)) {
            return Outcome::fail("the second process cannot open the shared texture by name");
        }
        std::uint32_t center = 0;
        bool uniform = false;
        if (!read_color(device, opened.get(), center, uniform, error)) {
            return Outcome::fail(error);
        }
        if (center != frame_color(7) || !uniform) {
            return Outcome::fail("the second process reads 0x" + Text::hex(center, 8) + " instead of 0x" + Text::hex(frame_color(7), 8));
        }
        return Outcome::ok();
    }
    D3d11Device producer;
    D3d11Device consumer;
    if (!Kit::open(adapter, producer, error) || !consumer.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(producer, shared_desc(D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE));
    Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
    const std::string name = "Local\\lsw-suites-nt-" + std::to_string(::GetCurrentProcessId());
    HANDLE handle = nullptr;
    if (!texture || !resource || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, Text::wide(name).c_str(), &handle))) {
        return Outcome::fail("CreateSharedHandle with a name and access mask failed");
    }
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(producer, texture.get());
    clear_frame(producer, view.get(), 7);
    producer.context->Flush();
    static_cast<void>(D3d11Tools::wait_event(producer, producer.context, error));
    Com<ID3D11Device1> consumer1 = Kit::query<ID3D11Device1>(consumer.device);
    Com<ID3D11Texture2D> by_handle;
    Com<ID3D11Texture2D> by_name;
    if (!consumer1 || FAILED(consumer1->OpenSharedResource1(handle, __uuidof(ID3D11Texture2D), by_handle.put_void()))) {
        problems.add("OpenSharedResource1 failed");
    }
    if (!open_by_name(consumer, Text::wide(name), by_name)) {
        problems.add("OpenSharedResourceByName failed");
    }
    std::uint32_t center = 0;
    bool uniform = false;
    for (ID3D11Texture2D* opened : {by_handle.get(), by_name.get()}) {
        if (opened != nullptr && read_color(consumer, opened, center, uniform, error) && (center != frame_color(7) || !uniform)) {
            problems.add("a shared texture opened in the second device reads 0x" + Text::hex(center, 8));
        }
    }
    ChildRunOptions options;
    options.timeout_seconds = 90;
    options.env = {{"LSW_SUITES_SHARE_NAME", name}};
    const CheckResult child = ChildRuns::run_self("sanity.d3d11.share.nt_handle", options);
    if (child.status != Status::Ok) {
        problems.add("the second process failed: " + child.reason);
    }
    ::CloseHandle(handle);
    resource.reset();
    view.reset();
    texture.reset();
    producer.context->Flush();
    if (by_handle && read_color(consumer, by_handle.get(), center, uniform, error)) {
        if (center != frame_color(7)) {
            problems.add("after the producer closed its handle and released the texture the consumer's resource reads 0x" + Text::hex(center, 8));
        }
    } else if (by_handle) {
        problems.add("the consumer's resource is unusable after the producer closed its handle: " + error);
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome ShareChecks::keyed_mutex(Context& context) {
    const std::string name_text = ChildMode::variable(L"LSW_SUITES_SHARE_NAME");
    AvAdapter adapter;
    std::string error;
    constexpr int kFrames = 100;
    if (ChildMode::active() && !name_text.empty()) {
        D3d11Device device;
        if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
            return Outcome::fail(error);
        }
        Com<ID3D11Texture2D> opened;
        if (!open_by_name(device, Text::wide(name_text), opened)) {
            return Outcome::fail("the consumer process cannot open the shared texture by name");
        }
        Com<IDXGIKeyedMutex> mutex = Kit::query<IDXGIKeyedMutex>(opened.get());
        if (!mutex) {
            return Outcome::fail("the opened texture has no keyed mutex");
        }
        Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
        for (int k = 0; k < kFrames; ++k) {
            const HRESULT acquired = mutex->AcquireSync(1, 20000);
            if (acquired != S_OK) {
                return Outcome::fail("frame " + std::to_string(k) + ": AcquireSync(1) returned " + D3d11Tools::hr(acquired));
            }
            device.context->CopyResource(staging.get(), opened.get());
            std::uint32_t center = 0;
            bool uniform = false;
            const bool read = read_color(device, staging.get(), center, uniform, error);
            mutex->ReleaseSync(0);
            if (!read) {
                return Outcome::fail(error);
            }
            if (center != frame_color(k) || !uniform) {
                return Outcome::fail("frame " + std::to_string(k) + " arrived as 0x" + Text::hex(center, 8) + (uniform ? "" : " (torn)") + ", expected 0x" + Text::hex(frame_color(k), 8));
            }
        }
        return Outcome::ok();
    }
    D3d11Device producer;
    D3d11Device consumer;
    if (!Kit::open(adapter, producer, error) || !consumer.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(producer, shared_desc(D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE));
    Com<IDXGIResource1> resource = Kit::query<IDXGIResource1>(texture.get());
    Com<IDXGIKeyedMutex> producer_mutex = Kit::query<IDXGIKeyedMutex>(texture.get());
    const std::string name = "Local\\lsw-suites-km-" + std::to_string(::GetCurrentProcessId());
    HANDLE handle = nullptr;
    if (!texture || !resource || !producer_mutex || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, Text::wide(name).c_str(), &handle))) {
        return Outcome::fail("a keyed mutex texture with an NT handle cannot be created");
    }
    Com<ID3D11Texture2D> opened;
    Com<ID3D11Device1> consumer1 = Kit::query<ID3D11Device1>(consumer.device);
    if (!consumer1 || FAILED(consumer1->OpenSharedResource1(handle, __uuidof(ID3D11Texture2D), opened.put_void()))) {
        return Outcome::fail("the keyed mutex texture cannot be opened in a second device");
    }
    Com<IDXGIKeyedMutex> consumer_mutex = Kit::query<IDXGIKeyedMutex>(opened.get());
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(producer, texture.get());
    if (!consumer_mutex || !view) {
        return Outcome::fail("the consumer has no keyed mutex");
    }
    if (producer_mutex->AcquireSync(0, 1000) != S_OK) {
        problems.add("the producer cannot acquire key 0");
    }
    const HRESULT blocked = consumer_mutex->AcquireSync(1, 100);
    if (blocked != static_cast<HRESULT>(WAIT_TIMEOUT)) {
        problems.add("AcquireSync(1) while the key is held returned " + D3d11Tools::hr(blocked) + " instead of WAIT_TIMEOUT");
    }
    clear_frame(producer, view.get(), 1);
    producer.context->Flush();
    producer_mutex->ReleaseSync(1);
    if (consumer_mutex->AcquireSync(1, 5000) != S_OK) {
        problems.add("the consumer cannot acquire key 1 after the producer released it");
    } else {
        std::uint32_t center = 0;
        bool uniform = false;
        Com<ID3D11Texture2D> staging = D3d11Tools::texture(consumer, D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
        consumer.context->CopyResource(staging.get(), opened.get());
        if (!read_color(consumer, staging.get(), center, uniform, error) || center != frame_color(1) || !uniform) {
            problems.add("the consumer reads 0x" + Text::hex(center, 8) + " after acquiring key 1, expected 0x" + Text::hex(frame_color(1), 8));
        }
        consumer_mutex->ReleaseSync(0);
    }
    const HRESULT back = producer_mutex->AcquireSync(0, 5000);
    if (back != S_OK) {
        problems.add("the producer cannot reacquire key 0 after the consumer released it");
    } else {
        producer_mutex->ReleaseSync(0);
    }
    CheckResult child;
    ChildRunOptions options;
    options.timeout_seconds = 150;
    options.env = {{"LSW_SUITES_SHARE_NAME", name}};
    std::thread runner([&] { child = ChildRuns::run_self("sanity.d3d11.share.keyed_mutex", options); });
    for (int k = 0; k < kFrames; ++k) {
        const HRESULT acquired = producer_mutex->AcquireSync(0, 20000);
        if (acquired != S_OK) {
            problems.add("frame " + std::to_string(k) + ": the producer's AcquireSync(0) returned " + D3d11Tools::hr(acquired));
            break;
        }
        clear_frame(producer, view.get(), k);
        producer.context->Flush();
        producer_mutex->ReleaseSync(1);
    }
    runner.join();
    if (child.status != Status::Ok) {
        problems.add("the consumer process failed: " + child.reason);
    }
    ::CloseHandle(handle);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome ShareChecks::fence(Context& context) {
    AvAdapter adapter;
    D3d11Device producer;
    D3d11Device consumer;
    std::string error;
    if (!Kit::open(adapter, producer, error) || !consumer.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11Device5> device_a = Kit::query<ID3D11Device5>(producer.device);
    Com<ID3D11Device5> device_b = Kit::query<ID3D11Device5>(consumer.device);
    if (!device_a || !device_b) {
        context.note("ID3D11Device5 is not exposed; QueryInterface fails cleanly, which is the unsupported answer");
        return Outcome::ok();
    }
    Problems problems;
    Com<ID3D11Fence> fence;
    const HRESULT created = device_a->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), fence.put_void());
    if (FAILED(created)) {
        Com<ID3D11Fence> plain;
        const HRESULT local = device_a->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), plain.put_void());
        context.note("shared fences are unsupported: CreateFence(SHARED) returned " + D3d11Tools::hr(created) + ", CreateFence(NONE) returned " + D3d11Tools::hr(local));
        if (FAILED(local) && local != created) {
            problems.add("the unsupported answer is inconsistent: shared " + D3d11Tools::hr(created) + ", plain " + D3d11Tools::hr(local));
        }
        return problems.outcome();
    }
    HANDLE handle = nullptr;
    if (FAILED(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) {
        return Outcome::fail("CreateSharedHandle of a shared fence failed");
    }
    Com<ID3D11Fence> opened;
    if (FAILED(device_b->OpenSharedFence(handle, __uuidof(ID3D11Fence), opened.put_void()))) {
        ::CloseHandle(handle);
        return Outcome::fail("OpenSharedFence failed in the second device");
    }
    Com<ID3D11DeviceContext4> context_a = Kit::query<ID3D11DeviceContext4>(producer.context);
    Com<ID3D11DeviceContext4> context_b = Kit::query<ID3D11DeviceContext4>(consumer.context);
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(producer, shared_desc(D3D11_RESOURCE_MISC_SHARED));
    Com<IDXGIResource> resource = Kit::query<IDXGIResource>(texture.get());
    HANDLE texture_handle = nullptr;
    Com<ID3D11Texture2D> remote;
    if (!context_a || !context_b || !texture || !resource || FAILED(resource->GetSharedHandle(&texture_handle)) ||
        FAILED(consumer.device->OpenSharedResource(texture_handle, __uuidof(ID3D11Texture2D), remote.put_void()))) {
        ::CloseHandle(handle);
        return Outcome::fail("the fence scene objects cannot be created");
    }
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(producer, texture.get());
    Com<ID3D11PixelShader> heavy = D3d11Tools::ps(producer, "float4 main(float4 p : SV_Position) : SV_Target { float a = p.x; for (int i = 0; i < 200; ++i) { a = sin(a) + 1.0; } return float4(0.5, 0.25, 1.0, 1.0); }", error);
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(consumer, D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
    if (!view || !heavy || !staging) {
        ::CloseHandle(handle);
        return Outcome::fail("fence scene shaders: " + error);
    }
    clear_frame(producer, view.get(), 3);
    static_cast<void>(D3d11Tools::wait_event(producer, producer.context, error));
    for (UINT64 value = 1; value <= 3; ++value) {
        for (int i = 0; i < 20; ++i) {
            static_cast<void>(D3d11Tools::draw_fullscreen(producer, heavy.get(), view.get(), 64, 64, error));
        }
        context_a->Signal(fence.get(), value);
        producer.context->Flush();
        context_b->Wait(opened.get(), value);
        consumer.context->CopyResource(staging.get(), remote.get());
        std::uint32_t center = 0;
        bool uniform = false;
        if (!read_color(consumer, staging.get(), center, uniform, error)) {
            ::CloseHandle(handle);
            return Outcome::fail(error);
        }
        if (center != Kit::rgba(128, 64, 255, 255)) {
            problems.add("after waiting for fence value " + std::to_string(value) + " the second device reads 0x" + Text::hex(center, 8));
        }
        if (opened->GetCompletedValue() < value) {
            problems.add("the shared fence completed value is below " + std::to_string(value) + " after the wait");
        }
        clear_frame(producer, view.get(), static_cast<int>(value) * 5);
        static_cast<void>(D3d11Tools::wait_event(producer, producer.context, error));
    }
    ::CloseHandle(handle);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
