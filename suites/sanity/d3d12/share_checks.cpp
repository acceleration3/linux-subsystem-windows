#include "sanity/d3d12/share_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/d3d12/kit.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"

#include <dxgi1_2.h>

#include <cstring>

namespace lsw::suites::sanity::d3d12 {

namespace {

constexpr UINT64 kBytes = 256 * 1024;

bool shareable(HRESULT result) {
    return result != E_NOTIMPL && result != D3d12Tools::kUnsupported;
}

}  // namespace

Outcome ShareChecks::heap_resource_nt(Context& context) {
    const std::string name_text = ChildMode::variable(L"LSW_SUITES_SHARE_NAME");
    if (ChildMode::active() && !name_text.empty()) {
        return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
            HANDLE handle = nullptr;
            if (!Kit::succeeds(problems, "OpenSharedHandleByName", rig.device()->OpenSharedHandleByName(Text::wide(name_text).c_str(), GENERIC_ALL, &handle))) {
                return {};
            }
            Com<ID3D12Resource> opened;
            const HRESULT result = rig.device()->OpenSharedHandle(handle, __uuidof(ID3D12Resource), opened.put_void());
            ::CloseHandle(handle);
            if (!Kit::succeeds(problems, "OpenSharedHandle", result)) {
                return {};
            }
            ID3D12Resource* resource = rig.hold(std::move(opened));
            std::vector<std::uint8_t> back;
            std::string error;
            if (!D3d12Tools::read_buffer(rig, resource, D3D12_RESOURCE_STATE_COMMON, 0, kBytes, back, error)) {
                problems.add("reading the shared buffer in the second process failed: " + error);
                return {};
            }
            const std::vector<std::uint8_t> expected = Kit::bytes(1100, static_cast<std::size_t>(kBytes));
            static_cast<void>(Kit::same_bytes(context, problems, "shared buffer read by the second process", expected.data(), back.data(), expected.size()));
            return {};
        });
    }
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        D3d12Rig other;
        if (!other.open_luid(rig.luid, D3D_FEATURE_LEVEL_11_0, error)) {
            problems.add("the second device cannot be created: " + error);
            return {};
        }
        ID3D12Device* d = rig.device();
        const std::string name = "Local\\lsw-suites-d3d12-share-" + std::to_string(::GetCurrentProcessId());
        HRESULT made = S_OK;
        ID3D12Resource* buffer = D3d12Tools::committed(rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_SHARED, D3d12Tools::buffer_desc(kBytes),
                                                       D3D12_RESOURCE_STATE_COMMON, nullptr, &made);
        if (buffer == nullptr) {
            problems.add("a shared committed buffer cannot be created: " + Kit::hr(made));
            return {};
        }
        HANDLE handle = nullptr;
        made = d->CreateSharedHandle(buffer, nullptr, GENERIC_ALL, Text::wide(name).c_str(), &handle);
        if (FAILED(made)) {
            if (!shareable(made)) {
                return "resource sharing is unsupported: CreateSharedHandle returned " + Kit::hr(made);
            }
            problems.add("CreateSharedHandle of a shared buffer returned " + Kit::hr(made));
            return {};
        }
        Com<ID3D12Fence> fence_owner;
        Com<ID3D12Fence> fence_twin_owner;
        HANDLE fence_handle = nullptr;
        if (!Kit::succeeds(problems, "CreateFence(SHARED)", d->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), fence_owner.put_void())) ||
            !Kit::succeeds(problems, "CreateSharedHandle(fence)", d->CreateSharedHandle(fence_owner.get(), nullptr, GENERIC_ALL, nullptr, &fence_handle)) ||
            !Kit::succeeds(problems, "OpenSharedHandle(fence)", other.device()->OpenSharedHandle(fence_handle, __uuidof(ID3D12Fence), fence_twin_owner.put_void()))) {
            ::CloseHandle(handle);
            return {};
        }
        ::CloseHandle(fence_handle);
        ID3D12Fence* fence = rig.hold(std::move(fence_owner));
        ID3D12Fence* twin_fence = other.hold(std::move(fence_twin_owner));
        Com<ID3D12Resource> opened_owner;
        if (!Kit::succeeds(problems, "OpenSharedHandle(resource)", other.device()->OpenSharedHandle(handle, __uuidof(ID3D12Resource), opened_owner.put_void()))) {
            ::CloseHandle(handle);
            return {};
        }
        ID3D12Resource* opened = other.hold(std::move(opened_owner));
        const std::vector<std::uint8_t> data = Kit::bytes(1100, static_cast<std::size_t>(kBytes));
        ID3D12Resource* upload = D3d12Tools::upload(rig, kBytes);
        ID3D12Resource* readback = D3d12Tools::readback(other, kBytes);
        if (upload == nullptr || readback == nullptr) {
            problems.add("creating the staging buffers failed");
            ::CloseHandle(handle);
            return {};
        }
        void* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        upload->Map(0, &none, &mapped);
        std::memcpy(mapped, data.data(), data.size());
        upload->Unmap(0, nullptr);
        std::uint64_t written = 0;
        if (!rig.direct().run(
                [&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                    list->CopyBufferRegion(buffer, 0, upload, 0, kBytes);
                    D3d12Tools::move(list, buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                },
                written, error)) {
            problems.add("writing the shared buffer failed: " + error);
            ::CloseHandle(handle);
            return {};
        }
        rig.direct().queue()->Signal(fence, 1);
        rig.direct().touch();
        other.direct().gpu_wait_fence(twin_fence, 1);
        std::vector<std::uint8_t> back;
        if (!other.direct().run_wait([&](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(readback, 0, opened, 0, kBytes); }, error) ||
            !D3d12Tools::map_bytes(readback, 0, kBytes, back, error)) {
            problems.add("reading the shared buffer in the second device failed: " + error);
            ::CloseHandle(handle);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "shared buffer read by the second device after fence ordering", data.data(), back.data(), data.size()));
        ChildRunOptions options;
        options.timeout_seconds = 120;
        options.env = {{"LSW_SUITES_SHARE_NAME", name}};
        const CheckResult child = ChildRuns::run_self("sanity.d3d12.share.heap_resource_nt", options);
        if (child.status != Status::Ok) {
            problems.add("the second process failed: " + child.reason);
        }
        ::CloseHandle(handle);
        handle = nullptr;
        const UINT64 heap_size = 1024 * 1024;
        D3D12_HEAP_DESC heap_desc{};
        heap_desc.SizeInBytes = heap_size;
        heap_desc.Properties = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
        heap_desc.Flags = static_cast<D3D12_HEAP_FLAGS>(Kit::buffer_heap_flags(rig) | D3D12_HEAP_FLAG_SHARED);
        Com<ID3D12Heap> heap_owner;
        made = d->CreateHeap(&heap_desc, __uuidof(ID3D12Heap), heap_owner.put_void());
        if (FAILED(made)) {
            problems.add("a shared heap cannot be created: " + Kit::hr(made));
            return {};
        }
        ID3D12Heap* heap = rig.hold(std::move(heap_owner));
        HANDLE heap_handle = nullptr;
        Com<ID3D12Heap> heap_twin_owner;
        if (!Kit::succeeds(problems, "CreateSharedHandle(heap)", d->CreateSharedHandle(heap, nullptr, GENERIC_ALL, nullptr, &heap_handle)) ||
            !Kit::succeeds(problems, "OpenSharedHandle(heap)", other.device()->OpenSharedHandle(heap_handle, __uuidof(ID3D12Heap), heap_twin_owner.put_void()))) {
            return {};
        }
        ::CloseHandle(heap_handle);
        ID3D12Heap* heap_twin = other.hold(std::move(heap_twin_owner));
        Com<ID3D12Resource> first;
        Com<ID3D12Resource> second;
        const D3D12_RESOURCE_DESC placed_desc = D3d12Tools::buffer_desc(kBytes);
        if (!Kit::succeeds(problems, "CreatePlacedResource in the first device", d->CreatePlacedResource(heap, 64 * 1024, &placed_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), first.put_void())) ||
            !Kit::succeeds(problems, "CreatePlacedResource in the second device",
                           other.device()->CreatePlacedResource(heap_twin, 64 * 1024, &placed_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), second.put_void()))) {
            return {};
        }
        ID3D12Resource* writer = rig.hold(std::move(first));
        ID3D12Resource* reader = other.hold(std::move(second));
        const std::vector<std::uint8_t> heap_data = Kit::bytes(1101, static_cast<std::size_t>(kBytes));
        if (!D3d12Tools::write_buffer(rig, writer, D3D12_RESOURCE_STATE_COPY_DEST, 0, heap_data.data(), heap_data.size(), error)) {
            problems.add("writing through the shared heap failed: " + error);
            return {};
        }
        rig.direct().queue()->Signal(fence, 2);
        rig.direct().touch();
        other.direct().gpu_wait_fence(twin_fence, 2);
        if (!D3d12Tools::read_buffer(other, reader, D3D12_RESOURCE_STATE_COPY_DEST, 0, kBytes, back, error)) {
            problems.add("reading through the shared heap failed: " + error);
            return {};
        }
        static_cast<void>(Kit::same_bytes(context, problems, "data written through a shared heap and read through its twin placed resource", heap_data.data(), back.data(), heap_data.size()));
        D3d11Device d3d11;
        if (!d3d11.create(rig.luid, error)) {
            problems.add("the D3D11 device cannot be created: " + error);
            return {};
        }
        const UINT edge = 64;
        ID3D12Resource* texture = D3d12Tools::committed(rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_SHARED,
                                                        D3d12Tools::texture_desc(edge, edge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS),
                                                        D3D12_RESOURCE_STATE_COMMON, nullptr, &made);
        if (texture == nullptr) {
            problems.add("a shared texture cannot be created: " + Kit::hr(made));
            return {};
        }
        std::vector<std::uint8_t> pixels(edge * edge * 4);
        Pattern::fill_pixels(1102, edge, edge, edge * 4, pixels.data());
        HANDLE texture_handle = nullptr;
        Com<ID3D12Resource> texture_twin_owner;
        if (!D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COMMON, 0, pixels.data(), error) ||
            !Kit::succeeds(problems, "CreateSharedHandle(texture)", d->CreateSharedHandle(texture, nullptr, GENERIC_ALL, nullptr, &texture_handle)) ||
            !Kit::succeeds(problems, "OpenSharedHandle(texture) in the second D3D12 device", other.device()->OpenSharedHandle(texture_handle, __uuidof(ID3D12Resource), texture_twin_owner.put_void()))) {
            problems.add("sharing the texture: " + error);
            return {};
        }
        ID3D12Resource* texture_twin = other.hold(std::move(texture_twin_owner));
        TexelImage via12;
        if (D3d12Tools::read_texture(other, texture_twin, D3D12_RESOURCE_STATE_COMMON, 0, via12, error)) {
            static_cast<void>(Kit::same_bytes(context, problems, "shared texture read through the second D3D12 device", pixels.data(), via12.data.data(), pixels.size()));
        } else {
            problems.add("reading the shared texture through D3D12 failed: " + error);
        }
        Com<ID3D11Device1> device1;
        d3d11.device->QueryInterface(__uuidof(ID3D11Device1), device1.put_void());
        Com<ID3D11Texture2D> opened11;
        if (!device1 || FAILED(device1->OpenSharedResource1(texture_handle, __uuidof(ID3D11Texture2D), opened11.put_void()))) {
            problems.add("D3D11 cannot open the D3D12 shared texture with OpenSharedResource1");
        } else {
            D3D11_TEXTURE2D_DESC desc{};
            opened11->GetDesc(&desc);
            Com<ID3D11Texture2D> staging = D3d11Tools::texture(d3d11, D3d11Tools::desc2d(edge, edge, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
            TexelImage via11;
            if (desc.Width != edge || desc.Height != edge || desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                problems.add("D3D11 sees the shared texture as " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " format " + std::to_string(desc.Format));
            } else if (!staging) {
                problems.add("creating the D3D11 staging texture failed");
            } else {
                d3d11.context->CopyResource(staging.get(), opened11.get());
                if (D3d11Tools::read_texture(d3d11, staging.get(), 0, via11, error)) {
                    static_cast<void>(Kit::same_bytes(context, problems, "shared texture read through D3D11", pixels.data(), via11.data.data(), pixels.size()));
                } else {
                    problems.add("reading the shared texture through D3D11 failed: " + error);
                }
            }
        }
        ::CloseHandle(texture_handle);
        return {};
    });
}

Outcome ShareChecks::keyed_cross_api(Context& context) {
    return Kit::one_device(context, D3D_FEATURE_LEVEL_11_0, [&](D3d12Rig& rig, Problems& problems) -> std::string {
        std::string error;
        D3d11Device d3d11;
        if (!d3d11.create(rig.luid, error)) {
            problems.add("the D3D11 device cannot be created: " + error);
            return {};
        }
        const UINT edge = 64;
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(edge, edge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        Com<ID3D11Texture2D> texture = D3d11Tools::texture(d3d11, desc);
        Com<IDXGIKeyedMutex> mutex = texture.as<IDXGIKeyedMutex>();
        Com<IDXGIResource1> resource = texture.as<IDXGIResource1>();
        HANDLE handle = nullptr;
        if (!texture || !mutex || !resource || FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle))) {
            problems.add("a keyed mutex texture with an NT handle cannot be created in D3D11");
            return {};
        }
        Com<ID3D12Resource> opened_owner;
        const HRESULT opened_result = rig.device()->OpenSharedHandle(handle, __uuidof(ID3D12Resource), opened_owner.put_void());
        ::CloseHandle(handle);
        if (!Kit::succeeds(problems, "OpenSharedHandle of a D3D11 keyed mutex texture in D3D12", opened_result)) {
            return {};
        }
        ID3D12Resource* opened = rig.hold(std::move(opened_owner));
        const D3D12_RESOURCE_DESC opened_desc = opened->GetDesc();
        if (opened_desc.Width != edge || opened_desc.Height != edge || opened_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
            problems.add("D3D12 sees the D3D11 texture as " + std::to_string(opened_desc.Width) + "x" + std::to_string(opened_desc.Height) + " format " + std::to_string(opened_desc.Format));
            return {};
        }
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d3d11, texture.get());
        Com<ID3D11Texture2D> staging = D3d11Tools::texture(d3d11, D3d11Tools::desc2d(edge, edge, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
        if (!view || !staging) {
            problems.add("creating the D3D11 views failed");
            return {};
        }
        for (int round = 0; round < 3; ++round) {
            const std::string label = "round " + std::to_string(round);
            if (mutex->AcquireSync(0, 5000) != S_OK) {
                problems.add(label + ": D3D11 cannot acquire key 0");
                return {};
            }
            D3d11Tools::clear(d3d11, view.get(), 0.2f, 0.4f, 0.8f, 1.0f);
            d3d11.context->Flush();
            const bool flushed = D3d11Tools::wait_event(d3d11, d3d11.context, error);
            mutex->ReleaseSync(1);
            if (!flushed) {
                problems.add(label + ": " + error);
                return {};
            }
            TexelImage via12;
            if (!D3d12Tools::read_texture(rig, opened, D3D12_RESOURCE_STATE_COMMON, 0, via12, error)) {
                problems.add(label + ": reading the D3D11 result through D3D12 failed: " + error);
                return {};
            }
            std::string where;
            if (!Kit::uniform(via12, Kit::rgba(51, 102, 204, 255), where)) {
                problems.add(label + ": D3D12 reads the D3D11 clear as " + where);
            }
            std::vector<std::uint8_t> pixels(edge * edge * 4);
            Pattern::fill_pixels(1200 + static_cast<std::uint64_t>(round), edge, edge, edge * 4, pixels.data());
            if (!D3d12Tools::write_texture(rig, opened, D3D12_RESOURCE_STATE_COMMON, 0, pixels.data(), error)) {
                problems.add(label + ": writing through D3D12 failed: " + error);
                return {};
            }
            if (mutex->AcquireSync(0, 50) != static_cast<HRESULT>(WAIT_TIMEOUT)) {
                problems.add(label + ": AcquireSync(0) succeeded although the key was released as 1");
                mutex->ReleaseSync(0);
            }
            if (mutex->AcquireSync(1, 5000) != S_OK) {
                problems.add(label + ": D3D11 cannot acquire key 1 after D3D12 wrote the texture");
                return {};
            }
            d3d11.context->CopyResource(staging.get(), texture.get());
            TexelImage via11;
            const bool read = D3d11Tools::read_texture(d3d11, staging.get(), 0, via11, error);
            mutex->ReleaseSync(0);
            if (!read) {
                problems.add(label + ": " + error);
                return {};
            }
            static_cast<void>(Kit::same_bytes(context, problems, label + ": D3D12 data read by D3D11 after acquiring the key", pixels.data(), via11.data.data(), pixels.size()));
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::d3d12
