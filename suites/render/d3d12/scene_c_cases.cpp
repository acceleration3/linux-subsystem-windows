#include "render/d3d12/scene_util.hpp"

#include <dxgi1_4.h>

namespace lsw::suites::render::d3d12 {

namespace {

struct BundleState {
    Stage stage;
    ID3D12PipelineState* state = nullptr;
    ID3D12GraphicsCommandList* bundle = nullptr;
};

void register_bundles() {
    Cases::frames(rr::CaseSpec("scene", "bundle_recorded_once_executed_every_frame").size(256, 256).frames(6, {0, 3, 5}).tolerance(rr::Tolerance::blend(4).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<BundleState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 256)) {
                return false;
            }
            Pso builder = s->stage.pso();
            s->state = builder.build(error);
            if (s->state == nullptr) {
                return false;
            }
            Com<ID3D12CommandAllocator> allocator;
            HRESULT result = kit.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, __uuidof(ID3D12CommandAllocator), allocator.put_void());
            if (FAILED(result)) {
                return fail_with(error, "CreateCommandAllocator (bundle): " + D3d12Tools::hr(result));
            }
            Com<ID3D12GraphicsCommandList> bundle;
            result = kit.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, allocator.get(), s->state, __uuidof(ID3D12GraphicsCommandList), bundle.put_void());
            if (FAILED(result)) {
                return fail_with(error, "CreateCommandList (bundle): " + D3d12Tools::hr(result));
            }
            bundle->SetGraphicsRootSignature(s->stage.canvas.binder.signature);
            bundle->SetPipelineState(s->state);
            s->stage.torus.bind(bundle.get());
            s->stage.torus.draw(bundle.get());
            result = bundle->Close();
            if (FAILED(result)) {
                return fail_with(error, "closing the bundle: " + D3d12Tools::hr(result));
            }
            kit.rig.hold(std::move(allocator));
            s->bundle = kit.rig.hold(std::move(bundle));
            return true;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                s->stage.canvas.begin(list, clear);
                for (UINT k = 0; k < 3; ++k) {
                    const float a = static_cast<float>(frame) * 0.4f + static_cast<float>(k) * 2.1f;
                    const rr::Mat4 model = rr::Mat4::translate(0.7f * std::cos(a), 0.7f * std::sin(a), 0) * rr::Mat4::rotate_x(a) * rr::Mat4::scale(0.55f, 0.55f, 0.55f);
                    const float color[4] = {0.9f - 0.3f * static_cast<float>(k), 0.4f + 0.25f * static_cast<float>(k), 0.3f + 0.2f * static_cast<float>(k), 1};
                    set_cbv_root(list, s->stage.view_projection() * model, color);
                    list->ExecuteBundle(s->bundle);
                }
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

struct AsyncState {
    Stage stage;
    Cs fill;
    ID3D12Resource* buffer = nullptr;
    ID3D12PipelineState* draw = nullptr;
};

void register_queues() {
    Cases::frames(rr::CaseSpec("scene", "async_compute_queue_feeds_direct_queue_with_fences").frames(6, {0, 2, 5}).tolerance(rr::Tolerance::blend(4).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<AsyncState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!kit.rig.open_queue(D3D12_COMMAND_LIST_TYPE_COMPUTE, error) || !s->stage.open(kit, error)) {
                return false;
            }
            HRESULT result = S_OK;
            s->buffer = D3d12Tools::device_buffer(kit.rig, 64 * 64 * 16, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
            if (s->buffer == nullptr) {
                return fail_with(error, "buffer: " + D3d12Tools::hr(result));
            }
            const char* source =
                "RWStructuredBuffer<float4> dst : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { float t = p0.x; float2 p = (id.xy + 0.5) / 64.0 - 0.5;\n"
                "  float r = length(p); dst[id.y * 64 + id.x] = float4(0.5 + 0.5 * sin(r * 40.0 - t * 2.0), 0.5 + 0.5 * cos(p.x * 12.0 + t), 0.5 + 0.5 * sin(p.y * 15.0 - t), 1); }\n";
            if (!s->fill.open(kit, source, error)) {
                return false;
            }
            s->fill.binder.uav_buffer(0, s->buffer, DXGI_FORMAT_UNKNOWN, 0, 4096, 16);
            s->stage.canvas.binder.srv_buffer(0, s->buffer, DXGI_FORMAT_UNKNOWN, 0, 4096, 16);
            const char* ps =
                "StructuredBuffer<float4> data : register(t0);\nstruct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "float4 main(VsOut i) : SV_Target { uint2 c = (uint2)(saturate(i.uv) * 63.0); return data[c.y * 64 + c.x]; }\n";
            Pso builder = s->stage.pso(ps);
            s->draw = builder.build(error);
            return s->draw != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            D3d12Queue& direct = kit.rig.direct();
            D3d12Queue& compute = kit.rig.compute();
            if (frame > 0 && !compute.gpu_wait(direct, direct.last_signalled())) {
                return fail_with(error, "compute queue wait on the direct queue failed");
            }
            std::uint64_t value = 0;
            if (!compute.run([&](ID3D12GraphicsCommandList* list) {
                    s->fill.bind(list);
                    const float t[4] = {static_cast<float>(frame), 0, 0, 0};
                    list->SetComputeRoot32BitConstants(0, 4, t, 16);
                    list->Dispatch(8, 8, 1);
                }, value, error)) {
                return false;
            }
            if (!direct.gpu_wait(compute, value)) {
                return fail_with(error, "direct queue wait on the compute queue failed");
            }
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->draw);
                const float color[4] = {1, 1, 1, 1};
                set_cbv_root(list, s->stage.view_projection() * rr::Mat4::rotate_y(0.3f * static_cast<float>(frame) - 0.5f), color);
                s->stage.plane.bind(list);
                s->stage.plane.draw(list);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
    Cases::frames(rr::CaseSpec("scene", "copy_queue_patches_accumulate_in_atlas").frames(6, {0, 2, 5}).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        struct State {
            Stage stage;
            ID3D12Resource* atlas = nullptr;
            ID3D12Resource* upload = nullptr;
            ID3D12PipelineState* draw = nullptr;
        };
        auto s = std::make_shared<State>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!kit.rig.open_queue(D3D12_COMMAND_LIST_TYPE_COPY, error) || !s->stage.open(kit, error)) {
                return false;
            }
            HRESULT result = S_OK;
            const D3D12_RESOURCE_DESC desc = D3d12Tools::texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM);
            s->atlas = D3d12Tools::committed(kit.rig, D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, desc, D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
            if (s->atlas == nullptr) {
                return fail_with(error, "atlas: " + D3d12Tools::hr(result));
            }
            const Texels::Bytes dark(64 * 64 * 4, 40);
            if (!D3d12Tools::write_texture(kit.rig, s->atlas, D3D12_RESOURCE_STATE_COMMON, 0, dark.data(), error)) {
                return false;
            }
            std::vector<std::uint8_t> staging(6 * 4096);
            for (UINT k = 0; k < 6; ++k) {
                const Texels::Bytes patch = Texels::content(k, 16, 16);
                for (UINT y = 0; y < 16; ++y) {
                    std::memcpy(staging.data() + k * 4096 + y * 256, patch.data() + y * 64, 64);
                }
            }
            s->upload = kit.make_upload(staging.data(), staging.size(), error);
            s->stage.canvas.binder.srv_texture(0, s->atlas, DXGI_FORMAT_R8G8B8A8_UNORM);
            s->stage.canvas.binder.sampler(0, Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
            Pso builder = s->stage.pso(Hlsl12::kTexPs);
            s->draw = builder.build(error);
            return s->upload != nullptr && s->draw != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            D3d12Queue& direct = kit.rig.direct();
            D3d12Queue& copy = kit.rig.copy();
            if (frame > 0 && !copy.gpu_wait(direct, direct.last_signalled())) {
                return fail_with(error, "copy queue wait failed");
            }
            std::uint64_t value = 0;
            if (!copy.run([&](ID3D12GraphicsCommandList* list) {
                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                    footprint.Offset = static_cast<UINT64>(frame) * 4096;
                    footprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 256};
                    D3D12_TEXTURE_COPY_LOCATION source{};
                    source.pResource = s->upload;
                    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    source.PlacedFootprint = footprint;
                    D3D12_TEXTURE_COPY_LOCATION destination{};
                    destination.pResource = s->atlas;
                    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    list->CopyTextureRegion(&destination, (frame % 3) * 20, (frame / 3) * 24, 0, &source, nullptr);
                }, value, error)) {
                return false;
            }
            if (!direct.gpu_wait(copy, value)) {
                return fail_with(error, "direct queue wait on the copy queue failed");
            }
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, s->atlas, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->draw);
                const float color[4] = {1, 1, 1, 1};
                const float scale[4] = {1, 1, 0, 0};
                set_cbv_root(list, s->stage.view_projection() * rr::Mat4::rotate_y(-0.2f), color, scale);
                s->stage.plane.bind(list);
                s->stage.plane.draw(list);
                D3d12Tools::move(list, s->atlas, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

struct SwapState {
    TestWindow window;
    Com<IDXGISwapChain3> chain;
    ID3D12Resource* buffers[3] = {nullptr, nullptr, nullptr};
    ID3D12DescriptorHeap* heap = nullptr;
    Stage stage;
    ID3D12PipelineState* state = nullptr;
    rr::Image last;
    UINT increment = 0;
};

void register_swap() {
    Cases::frames(rr::CaseSpec("scene", "flip_swap_chain_present_loop_back_buffers").frames(6, {0, 3, 5}).tolerance(rr::Tolerance::blend(4).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<SwapState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error)) {
                return false;
            }
            TestWindowConfig config;
            TestWindow::fit_config(config, 128, 128);
            if (!s->window.create(config, error)) {
                return false;
            }
            Com<IDXGIFactory2> factory;
            HRESULT result = ::CreateDXGIFactory1(__uuidof(IDXGIFactory2), factory.put_void());
            if (FAILED(result)) {
                return fail_with(error, "IDXGIFactory2: " + D3d12Tools::hr(result));
            }
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = 128;
            desc.Height = 128;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 3;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            Com<IDXGISwapChain1> chain1;
            result = factory->CreateSwapChainForHwnd(kit.rig.direct().queue(), s->window.hwnd(), &desc, nullptr, nullptr, chain1.put());
            if (FAILED(result)) {
                return fail_with(error, "CreateSwapChainForHwnd: " + D3d12Tools::hr(result));
            }
            result = chain1->QueryInterface(__uuidof(IDXGISwapChain3), s->chain.put_void());
            if (FAILED(result)) {
                return fail_with(error, "IDXGISwapChain3: " + D3d12Tools::hr(result));
            }
            s->heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, false, &result);
            if (s->heap == nullptr) {
                return fail_with(error, "RTV heap: " + D3d12Tools::hr(result));
            }
            s->increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            for (UINT k = 0; k < 3; ++k) {
                Com<ID3D12Resource> buffer;
                result = s->chain->GetBuffer(k, __uuidof(ID3D12Resource), buffer.put_void());
                if (FAILED(result)) {
                    return fail_with(error, "GetBuffer: " + D3d12Tools::hr(result));
                }
                s->buffers[k] = kit.rig.hold(std::move(buffer));
                kit.device()->CreateRenderTargetView(s->buffers[k], nullptr, D3d12Tools::cpu_at(s->heap, k, s->increment));
            }
            Pso builder(kit, s->stage.canvas.binder.signature);
            builder.vs(Hlsl12::with_root(Hlsl12::kLitVs)).ps(Hlsl12::with_root(Hlsl12::kLitPs)).targets({DXGI_FORMAT_B8G8R8A8_UNORM});
            s->state = builder.build(error);
            return s->state != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const UINT index = s->chain->GetCurrentBackBufferIndex();
            ID3D12Resource* buffer = s->buffers[index];
            const D3D12_CPU_DESCRIPTOR_HANDLE view = D3d12Tools::cpu_at(s->heap, index, s->increment);
            if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                    list->OMSetRenderTargets(1, &view, FALSE, nullptr);
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                    list->ClearRenderTargetView(view, clear, 0, nullptr);
                    s->stage.canvas.set_viewport(list);
                    s->stage.canvas.binder.bind_graphics(list);
                    list->SetPipelineState(s->state);
                    const float angle = static_cast<float>(frame) * 0.5f;
                    s->stage.mesh(list, s->stage.torus, rr::Mat4::rotate_x(0.6f + angle) * rr::Mat4::rotate_y(angle), 0.9f, 0.5f - 0.05f * static_cast<float>(frame), 0.2f);
                }, error)) {
                return false;
            }
            if (!kit.read_image(buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, DXGI_FORMAT_B8G8R8A8_UNORM, s->last, error)) {
                return false;
            }
            if (!kit.run([&](ID3D12GraphicsCommandList* list) { D3d12Tools::move(list, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT); }, error)) {
                return false;
            }
            const HRESULT result = s->chain->Present(0, 0);
            if (FAILED(result)) {
                return fail_with(error, "Present: " + D3d12Tools::hr(result));
            }
            return true;
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string&) {
            out.add("", s->last);
            return true;
        };
    });
}

struct BindlessState {
    Stage stage;
    ID3D12PipelineState* state = nullptr;
};

void register_bindless() {
    Cases::frames(rr::CaseSpec("scene", "descriptor_array_material_indexing").size(256, 256).frames(3, {0, 2}).tolerance(loose_sampler_tolerance()).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<BindlessState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 256)) {
                return false;
            }
            for (UINT k = 0; k < 8; ++k) {
                TextureSpec spec;
                spec.width = 32;
                spec.height = 32;
                spec.mips = 6;
                spec.content = k;
                ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
                if (texture == nullptr) {
                    return false;
                }
                s->stage.canvas.binder.srv_texture(k, texture, DXGI_FORMAT_R8G8B8A8_UNORM, 6);
            }
            const char* ps =
                "Texture2D<float4> textures[8] : register(t0); SamplerState smp : register(s0);\nstruct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "float4 main(VsOut i) : SV_Target { uint m = (uint)p1.x; float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.6 + 0.4; return float4(textures[m].Sample(smp, i.uv * 2.0).rgb * l, 1); }\n";
            Pso builder = s->stage.pso(ps);
            s->state = builder.build(error);
            return s->state != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->state);
                for (UINT k = 0; k < 12; ++k) {
                    const float x = -0.8f + 0.53f * static_cast<float>(k % 4);
                    const float y = -0.6f + 0.6f * static_cast<float>(k / 4);
                    const rr::Mat4 model = rr::Mat4::translate(x, y, 0) * rr::Mat4::rotate_y(0.5f + 0.3f * static_cast<float>(frame)) * rr::Mat4::rotate_x(0.4f) * rr::Mat4::scale(0.22f, 0.22f, 0.22f);
                    const float color[4] = {1, 1, 1, 1};
                    const float material[4] = {static_cast<float>((k + frame * 3) % 8), 0, 0, 0};
                    set_cbv_root(list, s->stage.view_projection() * model, color, material);
                    s->stage.cube.bind(list);
                    s->stage.cube.draw(list);
                }
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

const struct Registrar {
    Registrar() {
        register_bundles();
        register_queues();
        register_swap();
        register_bindless();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
