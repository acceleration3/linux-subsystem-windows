#include "sanity/d3d11/tiled_checks.hpp"

#include "sanity/d3d11/kit.hpp"
#include "support/api_policy.hpp"
#include "support/d3d12_device.hpp"
#include "support/dyn_library.hpp"
#include "support/pattern.hpp"

#include <d3d11on12.h>

#include <array>

namespace lsw::suites::sanity::d3d11 {

Outcome TiledChecks::caps_driven(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 options{};
    if (FAILED(device.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &options, sizeof(options)))) {
        options.TiledResourcesTier = D3D11_TILED_RESOURCES_NOT_SUPPORTED;
    }
    context.measure("tiled_resources_tier", static_cast<long long>(options.TiledResourcesTier));
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = D3D11_RESOURCE_MISC_TILED;
    Com<ID3D11Texture2D> texture;
    const HRESULT texture_result = device.device->CreateTexture2D(&desc, nullptr, texture.put());
    HRESULT buffer_result = S_OK;
    Com<ID3D11Buffer> tiled_buffer = D3d11Tools::buffer(device, 65536 * 2, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_TILED, 0, nullptr, &buffer_result);
    HRESULT pool_result = S_OK;
    Com<ID3D11Buffer> pool = D3d11Tools::buffer(device, 65536 * 4, D3D11_USAGE_DEFAULT, 0, 0, D3D11_RESOURCE_MISC_TILE_POOL, 0, nullptr, &pool_result);
    if (options.TiledResourcesTier == D3D11_TILED_RESOURCES_NOT_SUPPORTED) {
        if (SUCCEEDED(texture_result) || SUCCEEDED(buffer_result) || SUCCEEDED(pool_result)) {
            problems.add("tiled resources are reported unsupported but a MISC_TILED texture, buffer or tile pool is created");
        }
        for (HRESULT code : {texture_result, buffer_result, pool_result}) {
            if (code != E_INVALIDARG && code != E_NOTIMPL && code != DXGI_ERROR_UNSUPPORTED && SUCCEEDED(code) == false && code != E_FAIL) {
                problems.add("an unsupported tiled creation returned the undocumented " + D3d11Tools::hr(code));
            }
        }
        return problems.outcome();
    }
    if (FAILED(texture_result) || FAILED(buffer_result) || FAILED(pool_result)) {
        return Outcome::fail("tiled resources are reported but creation fails: texture " + D3d11Tools::hr(texture_result) + ", buffer " + D3d11Tools::hr(buffer_result) + ", pool " + D3d11Tools::hr(pool_result));
    }
    Com<ID3D11Device2> device2 = Kit::query<ID3D11Device2>(device.device);
    Com<ID3D11DeviceContext2> ctx = Kit::query<ID3D11DeviceContext2>(device.context);
    if (!device2 || !ctx) {
        return Outcome::fail("tiled resources are reported but ID3D11DeviceContext2 is missing");
    }
    UINT total_tiles = 0;
    D3D11_PACKED_MIP_DESC packed{};
    D3D11_TILE_SHAPE shape{};
    UINT subresource_count = 1;
    D3D11_SUBRESOURCE_TILING tiling{};
    device2->GetResourceTiling(texture.get(), &total_tiles, &packed, &shape, &subresource_count, 0, &tiling);
    if (shape.WidthInTexels * shape.HeightInTexels * shape.DepthInTexels * 4 != 65536) {
        problems.add("the standard tile shape " + std::to_string(shape.WidthInTexels) + "x" + std::to_string(shape.HeightInTexels) + " is not 64 KiB for a 4-byte format");
    }
    if (total_tiles != 16 || tiling.WidthInTiles != 4 || tiling.HeightInTiles != 4 || tiling.StartTileIndexInOverallResource != 0) {
        problems.add("a 512x512 RGBA8 texture reports " + std::to_string(total_tiles) + " tiles of " + std::to_string(tiling.WidthInTiles) + "x" + std::to_string(tiling.HeightInTiles));
    }
    D3D11_TILED_RESOURCE_COORDINATE coordinate{1, 2, 0, 0};
    D3D11_TILE_REGION_SIZE region{1, FALSE, 0, 0, 0};
    const UINT range_flags[1] = {0};
    const UINT pool_offsets[1] = {0};
    const UINT range_counts[1] = {1};
    const HRESULT mapped = ctx->UpdateTileMappings(texture.get(), 1, &coordinate, &region, pool.get(), 1, range_flags, pool_offsets, range_counts, 0);
    if (FAILED(mapped)) {
        problems.add("UpdateTileMappings failed: " + D3d11Tools::hr(mapped));
        return problems.outcome();
    }
    std::vector<std::uint8_t> data(65536);
    Pattern::fill(91, 0, data.data(), data.size());
    ctx->UpdateTiles(texture.get(), &coordinate, &region, data.data(), 0);
    Com<ID3D11Buffer> staging_buffer = D3d11Tools::buffer(device, 65536, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0, nullptr);
    Com<ID3D11Buffer> linear = D3d11Tools::buffer(device, 65536, D3D11_USAGE_DEFAULT, 0, 0, 0, 0, nullptr);
    if (linear) {
        (void)staging_buffer;
        std::vector<std::uint8_t> back;
        ctx->CopyTiles(texture.get(), &coordinate, &region, linear.get(), 0, D3D11_TILE_COPY_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
        if (D3d11Tools::read_buffer(device, linear.get(), back, error) && back != data) {
            problems.add("a tile written with UpdateTiles reads back differently through CopyTiles");
        }
    }
    if (options.TiledResourcesTier >= D3D11_TILED_RESOURCES_TIER_2) {
        Com<ID3D11ShaderResourceView> srv = D3d11Tools::srv(device, texture.get());
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "Texture2D t : register(t0);\nfloat4 main() : SV_Target { return t.Load(int3(5, 5, 0)) + float4(0, 0, 0, 0); }", error);
        Com<ID3D11Texture2D> target = D3d11Tools::target(device, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
        if (srv && ps && view) {
            ID3D11ShaderResourceView* views[1] = {srv.get()};
            device.context->PSSetShaderResources(0, 1, views);
            D3d11Tools::clear(device, view.get(), 1, 1, 1, 1);
            static_cast<void>(D3d11Tools::draw_fullscreen(device, ps.get(), view.get(), 4, 4, error));
            TexelImage image;
            if (D3d11Tools::read_texture(device, target.get(), 0, image, error) && image.pixel(1, 1) != 0) {
                problems.add("a read from an unmapped tile returns 0x" + Text::hex(image.pixel(1, 1), 8) + " instead of zero on tier 2");
            }
        }
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome TiledChecks::d3d11_1(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11Device1> device1 = Kit::query<ID3D11Device1>(device.device);
    Com<ID3D11DeviceContext1> ctx1 = Kit::query<ID3D11DeviceContext1>(device.context);
    if (!device1 || !ctx1) {
        return Outcome::fail("ID3D11Device1 or ID3D11DeviceContext1 is not available");
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    device.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
    {
        D3D11_BLEND_DESC1 desc{};
        desc.RenderTarget[0].LogicOpEnable = TRUE;
        desc.RenderTarget[0].LogicOp = D3D11_LOGIC_OP_XOR;
        desc.RenderTarget[0].RenderTargetWriteMask = 0xf;
        Com<ID3D11BlendState1> state;
        const HRESULT result = device1->CreateBlendState1(&desc, state.put());
        if (options.OutputMergerLogicOp == FALSE) {
            if (SUCCEEDED(result)) {
                problems.add("logic ops are reported unsupported but CreateBlendState1 accepts a logic op");
            }
        } else if (FAILED(result)) {
            problems.add("logic ops are reported but CreateBlendState1 fails: " + D3d11Tools::hr(result));
        } else {
            Com<ID3D11Texture2D> target = D3d11Tools::target(device, 4, 4, DXGI_FORMAT_R32_UINT, 0);
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
            Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "uint main() : SV_Target { return 0xFF00; }", error);
            const UINT initial[4] = {0x0FF0, 0, 0, 0};
            device.context->ClearRenderTargetView(view.get(), std::array<float, 4>{4080.0f, 0, 0, 0}.data());
            static_cast<void>(initial);
            ctx1->OMSetBlendState(state.get(), nullptr, 0xffffffff);
            static_cast<void>(D3d11Tools::draw_fullscreen(device, ps.get(), view.get(), 4, 4, error));
            ctx1->OMSetBlendState(nullptr, nullptr, 0xffffffff);
            TexelImage image;
            if (D3d11Tools::read_texture(device, target.get(), 0, image, error) && image.pixel(1, 1) != (0x0FF0u ^ 0xFF00u)) {
                problems.add("a XOR logic op produced 0x" + Text::hex(image.pixel(1, 1), 8) + ", expected 0xF0F0");
            }
        }
    }
    {
        std::vector<float> data(4096 / 4);
        for (std::size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<float>(i / 4) / 255.0f;
        }
        Com<ID3D11Buffer> cb = D3d11Tools::buffer(device, 4096, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, data.data());
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "cbuffer C : register(b0) { float4 c[16]; }\nfloat4 main() : SV_Target { return float4(c[0].x, c[15].x, 0, 1); }", error);
        Com<ID3D11Texture2D> target = D3d11Tools::target(device, 4, 4);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
        if (cb && ps && view) {
            ID3D11Buffer* raw = cb.get();
            const UINT first = 32;
            const UINT count = 16;
            ctx1->PSSetConstantBuffers1(0, 1, &raw, &first, &count);
            static_cast<void>(D3d11Tools::draw_fullscreen(device, ps.get(), view.get(), 4, 4, error));
            TexelImage image;
            if (D3d11Tools::read_texture(device, target.get(), 0, image, error)) {
                if (image.pixel(1, 1) != Kit::rgba(32, 47, 0, 255)) {
                    if (options.ConstantBufferOffsetting != FALSE) {
                        problems.add("a constant buffer range at offset 32 constants read 0x" + Text::hex(image.pixel(1, 1), 8) + ", expected 0xFF002F20");
                    } else {
                        context.note("constant buffer offsetting is not reported and the offset is not applied");
                    }
                }
            }
        }
    }
    {
        Com<ID3D11Buffer> source = D3d11Tools::buffer(device, 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr);
        Com<ID3D11Buffer> dest = D3d11Tools::buffer(device, 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr);
        std::vector<std::uint8_t> a(256);
        std::vector<std::uint8_t> b(256, 0x77);
        Pattern::fill(5, 0, a.data(), 256);
        device.context->UpdateSubresource(source.get(), 0, nullptr, a.data(), 0, 0);
        for (UINT flags : {0u, (UINT)D3D11_COPY_NO_OVERWRITE, (UINT)D3D11_COPY_DISCARD}) {
            device.context->UpdateSubresource(dest.get(), 0, nullptr, b.data(), 0, 0);
            const D3D11_BOX box{16, 0, 0, 80, 1, 1};
            ctx1->CopySubresourceRegion1(dest.get(), 0, 100, 0, 0, source.get(), 0, &box, flags);
            std::vector<std::uint8_t> back;
            if (!D3d11Tools::read_buffer(device, dest.get(), back, error)) {
                problems.add(error);
                continue;
            }
            if (std::memcmp(back.data() + 100, a.data() + 16, 64) != 0) {
                problems.add("CopySubresourceRegion1 with flags " + std::to_string(flags) + " copied the wrong bytes");
            }
            if (flags != D3D11_COPY_DISCARD && (back[0] != 0x77 || back[200] != 0x77)) {
                problems.add("CopySubresourceRegion1 with flags " + std::to_string(flags) + " changed bytes outside the region");
            }
        }
    }
    {
        const D3D_FEATURE_LEVEL level = device.level;
        D3D_FEATURE_LEVEL chosen = D3D_FEATURE_LEVEL_9_1;
        Com<ID3DDeviceContextState> state;
        const HRESULT result = device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), &chosen, state.put());
        if (FAILED(result)) {
            problems.add("CreateDeviceContextState failed: " + D3d11Tools::hr(result));
        } else {
            Com<ID3D11Texture2D> target = D3d11Tools::target(device, 4, 4);
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
            ID3D11RenderTargetView* list[1] = {view.get()};
            device.context->OMSetRenderTargets(1, list, nullptr);
            Com<ID3DDeviceContextState> previous;
            ctx1->SwapDeviceContextState(state.get(), previous.put());
            ID3D11RenderTargetView* bound = nullptr;
            device.context->OMGetRenderTargets(1, &bound, nullptr);
            if (bound != nullptr) {
                problems.add("a fresh device context state still has the render target of the previous state bound");
                bound->Release();
            }
            if (device.device->GetFeatureLevel() != device.level) {
                problems.add("swapping the context state changed the device feature level");
            }
            Com<ID3DDeviceContextState> mine;
            ctx1->SwapDeviceContextState(previous.get(), mine.put());
            device.context->OMGetRenderTargets(1, &bound, nullptr);
            if (bound != view.get()) {
                problems.add("swapping back did not restore the original render target binding");
            }
            if (bound != nullptr) {
                bound->Release();
            }
        }
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome TiledChecks::d3d11_2_3_4(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11Device2> d2 = Kit::query<ID3D11Device2>(device.device);
    Com<ID3D11Device3> d3 = Kit::query<ID3D11Device3>(device.device);
    Com<ID3D11Device4> d4 = Kit::query<ID3D11Device4>(device.device);
    Com<ID3D11Device5> d5 = Kit::query<ID3D11Device5>(device.device);
    Com<ID3D11DeviceContext3> c3 = Kit::query<ID3D11DeviceContext3>(device.context);
    context.measure("interfaces", (d2 ? 2 : 0) + (d3 ? 3 : 0) + (d4 ? 4 : 0) + (d5 ? 5 : 0));
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2{};
    const HRESULT options2_result = device.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options2, sizeof(options2));
    if (d3 && FAILED(options2_result)) {
        problems.add("ID3D11Device3 is exposed but CheckFeatureSupport(D3D11_OPTIONS2) fails");
    }
    if (!d3 && SUCCEEDED(options2_result)) {
        problems.add("CheckFeatureSupport(D3D11_OPTIONS2) succeeds but ID3D11Device3 is not exposed");
    }
    if (d3) {
        D3D11_RASTERIZER_DESC2 desc{};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_ON;
        Com<ID3D11RasterizerState2> state;
        const HRESULT result = d3->CreateRasterizerState2(&desc, state.put());
        const bool supported = options2.ConservativeRasterizationTier != D3D11_CONSERVATIVE_RASTERIZATION_NOT_SUPPORTED;
        if (supported != SUCCEEDED(result)) {
            problems.add(std::string("conservative rasterization tier ") + (supported ? "is reported" : "is not reported") + " but CreateRasterizerState2 " + (SUCCEEDED(result) ? "accepts" : "rejects") + " it (" + D3d11Tools::hr(result) + ")");
        }
        D3D11_TEXTURE2D_DESC1 td{};
        td.Width = 16;
        td.Height = 16;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
        Com<ID3D11Texture2D1> t1;
        if (FAILED(d3->CreateTexture2D1(&td, nullptr, t1.put()))) {
            problems.add("CreateTexture2D1 with an undefined layout fails");
        }
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
        td.TextureLayout = D3D11_TEXTURE_LAYOUT_ROW_MAJOR;
        Com<ID3D11Texture2D1> mappable;
        const HRESULT mappable_result = d3->CreateTexture2D1(&td, nullptr, mappable.put());
        if ((options2.MapOnDefaultTextures != FALSE) != SUCCEEDED(mappable_result)) {
            problems.add(std::string("MapOnDefaultTextures is ") + (options2.MapOnDefaultTextures ? "reported" : "not reported") + " but a row-major CPU-accessible default texture " + (SUCCEEDED(mappable_result) ? "is created" : "is refused"));
        } else if (mappable) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(device.context->Map(mappable.get(), 0, D3D11_MAP_WRITE, 0, &mapped))) {
                problems.add("a default texture created with MapOnDefaultTextures cannot be mapped");
            } else {
                device.context->Unmap(mappable.get(), 0);
            }
        }
        HRESULT buffer_result = S_OK;
        Com<ID3D11Buffer> default_buffer = D3d11Tools::buffer(device, 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr, &buffer_result);
        context.note(std::string("a default buffer with CPU write access is ") + (SUCCEEDED(buffer_result) ? "created" : "refused"));
        if (!c3) {
            problems.add("ID3D11Device3 is exposed without ID3D11DeviceContext3");
        }
    }
    if (d4) {
        HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        DWORD cookie = 0;
        const HRESULT registered = d4->RegisterDeviceRemovedEvent(event, &cookie);
        if (FAILED(registered)) {
            problems.add("RegisterDeviceRemovedEvent failed: " + D3d11Tools::hr(registered));
        } else {
            d4->UnregisterDeviceRemoved(cookie);
        }
        ::CloseHandle(event);
    }
    if (d5) {
        Com<ID3D11Fence> fence;
        const HRESULT created = d5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), fence.put_void());
        Com<ID3D11DeviceContext4> c4 = Kit::query<ID3D11DeviceContext4>(device.context);
        if (SUCCEEDED(created) && c4) {
            c4->Signal(fence.get(), 5);
            HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
            fence->SetEventOnCompletion(5, event);
            device.context->Flush();
            if (::WaitForSingleObject(event, 10000) != WAIT_OBJECT_0 || fence->GetCompletedValue() < 5) {
                problems.add("a fence signalled with value 5 does not complete");
            }
            ::CloseHandle(event);
        } else if (FAILED(created)) {
            context.note("CreateFence is refused: " + D3d11Tools::hr(created));
        }
    }
    return problems.outcome();
}

Outcome TiledChecks::on12(Context& context) {
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("Direct3D 12 is excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    D3d12Device device12;
    if (!device12.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    ID3D12CommandQueue* queue = device12.make_queue(error);
    if (queue == nullptr) {
        return Outcome::fail(error);
    }
    DynLibrary library;
    if (!library.load("d3d11.dll")) {
        return Outcome::fail("d3d11.dll cannot be loaded");
    }
    auto create = library.symbol<PFN_D3D11ON12_CREATE_DEVICE>("D3D11On12CreateDevice");
    if (create == nullptr) {
        return Context::skip("d3d11.dll has no D3D11On12CreateDevice, so the runtime does not offer 11-on-12");
    }
    Problems problems;
    Com<ID3D11Device> device11;
    Com<ID3D11DeviceContext> context11;
    IUnknown* queues[1] = {queue};
    const HRESULT created = create(device12.device, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, queues, 1, 0, device11.put(), context11.put(), nullptr);
    if (FAILED(created)) {
        queue->Release();
        return Outcome::fail("D3D11On12CreateDevice failed: " + D3d11Tools::hr(created));
    }
    Com<ID3D11On12Device> on12 = Kit::query<ID3D11On12Device>(device11.get());
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = 64;
    rd.Height = 64;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource* texture12 = nullptr;
    if (!on12 || FAILED(device12.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&texture12)))) {
        queue->Release();
        return Outcome::fail("the D3D12 texture or the 11-on-12 interface is missing");
    }
    D3D11_RESOURCE_FLAGS flags{};
    flags.BindFlags = D3D11_BIND_RENDER_TARGET;
    Com<ID3D11Texture2D> wrapped;
    if (FAILED(on12->CreateWrappedResource(texture12, &flags, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE, __uuidof(ID3D11Texture2D), wrapped.put_void()))) {
        texture12->Release();
        queue->Release();
        return Outcome::fail("CreateWrappedResource failed");
    }
    ID3D11Resource* resources[1] = {wrapped.get()};
    on12->AcquireWrappedResources(resources, 1);
    D3d11Device scratch;
    Com<ID3D11RenderTargetView> view;
    device11->CreateRenderTargetView(wrapped.get(), nullptr, view.put());
    const float color[4] = {0.2f, 0.4f, 0.6f, 1.0f};
    context11->ClearRenderTargetView(view.get(), color);
    on12->ReleaseWrappedResources(resources, 1);
    context11->Flush();
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    ID3D12Resource* readback = nullptr;
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = 256 * 64;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device12.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocator))) ||
        FAILED(device12.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list))) ||
        FAILED(device12.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence))) ||
        FAILED(device12.device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&readback)))) {
        problems.add("the D3D12 copy objects cannot be created");
    } else {
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 256};
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = texture12;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        list->Close();
        ID3D12CommandList* lists[1] = {list};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence, 1);
        HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(1, event);
        if (::WaitForSingleObject(event, 20000) != WAIT_OBJECT_0) {
            problems.add("the D3D12 copy did not complete");
        } else {
            void* data = nullptr;
            if (SUCCEEDED(readback->Map(0, nullptr, &data))) {
                const std::uint8_t* px = static_cast<const std::uint8_t*>(data) + 256 * 32 + 32 * 4;
                if (std::abs(px[0] - 51) > 1 || std::abs(px[1] - 102) > 1 || std::abs(px[2] - 153) > 1 || px[3] != 255) {
                    problems.add("D3D12 reads " + std::to_string(px[0]) + "," + std::to_string(px[1]) + "," + std::to_string(px[2]) + " after D3D11 cleared the wrapped texture to 51,102,153");
                }
                readback->Unmap(0, nullptr);
            }
        }
        ::CloseHandle(event);
    }
    for (IUnknown* object : std::initializer_list<IUnknown*>{readback, fence, list, allocator}) {
        if (object != nullptr) {
            object->Release();
        }
    }
    view.reset();
    wrapped.reset();
    context11->Flush();
    texture12->Release();
    queue->Release();
    static_cast<void>(context);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
