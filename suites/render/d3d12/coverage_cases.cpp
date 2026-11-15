#include "render/d3d12/scene_util.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

struct MeshStream {
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE> as;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> ms;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> mask;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> rasterizer;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depth_stencil;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> targets;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depth_format;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> sample;
};

std::string needs_mesh(Kit& kit) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
    const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS7, options);
    return SUCCEEDED(result) && options.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED ? std::string() : std::string("mesh shaders");
}

const char* kMeshCommon =
    "struct Vert { float4 pos : SV_Position; float3 col : COLOR; };\n"
    "uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; return x; }\n"
    "Vert make(uint m, uint t) { uint mx = m % 4, my = m / 4; uint lx = t % 5, ly = t / 5; float x = -0.9 + (mx * 4 + lx) * 0.1125; float y = -0.9 + (my * 4 + ly) * 0.1125;\n"
    "  float z = 0.25 * sin(x * 4.0 + p0.x) * cos(y * 3.0); Vert v; v.pos = mul(mvp, float4(x, y, z, 1)); uint h = hash(m * 13u + 5u);\n"
    "  v.col = float3((h & 255) / 255.0, ((h >> 8) & 255) / 255.0, ((h >> 16) & 255) / 255.0) * (0.55 + 0.45 * z * 3.0); return v; }\n"
    "void emit(uint t, uint m, out vertices Vert verts[25], out indices uint3 tris[32]) { SetMeshOutputCounts(25, 32); if (t < 25) verts[t] = make(m, t);\n"
    "  uint q = t / 2; uint base = (q / 4) * 5 + (q % 4); tris[t] = (t % 2 == 0) ? uint3(base, base + 1, base + 5) : uint3(base + 1, base + 6, base + 5); }\n";

std::string mesh_only_source() {
    return Hlsl12::with_root((std::string(kMeshCommon) + "[numthreads(32, 1, 1)][outputtopology(\"triangle\")] void main(uint t : SV_GroupThreadID, uint gid : SV_GroupID, out vertices Vert verts[25], out indices uint3 tris[32]) { emit(t, gid, verts, tris); }\n").c_str());
}

std::string mesh_with_payload_source() {
    return Hlsl12::with_root((std::string(kMeshCommon) + "struct Payload { uint ids[16]; };\n[numthreads(32, 1, 1)][outputtopology(\"triangle\")] void main(uint t : SV_GroupThreadID, uint gid : SV_GroupID, in payload Payload pl, out vertices Vert verts[25], out indices uint3 tris[32]) { emit(t, pl.ids[gid], verts, tris); }\n").c_str());
}

std::string amplification_source() {
    return Hlsl12::with_root("struct Payload { uint ids[16]; };\ngroupshared Payload pl; groupshared uint cnt;\n"
                             "[numthreads(16, 1, 1)] void main(uint t : SV_GroupThreadID) { if (t == 0) cnt = 0; GroupMemoryBarrierWithGroupSync(); bool vis = ((t * 7u + (uint)p0.y) % 3u) != 0;\n"
                             "  if (vis) { uint i; InterlockedAdd(cnt, 1, i); pl.ids[i] = t; } GroupMemoryBarrierWithGroupSync(); DispatchMesh(cnt, 1, 1, pl); }\n");
}

struct MeshRig {
    Stage stage;
    ID3D12PipelineState* state = nullptr;

    bool open(Kit& kit, bool amplified, std::string& error) {
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        Kit::Bytes as_code;
        Kit::Bytes ms_code;
        Kit::Bytes ps_code;
        if ((amplified && !kit.compile_as(amplification_source(), "main", "as", as_code, error)) ||
            !kit.compile_as(amplified ? mesh_with_payload_source() : mesh_only_source(), "main", "ms", ms_code, error) ||
            !kit.compile_as("struct Vert { float4 pos : SV_Position; float3 col : COLOR; };\nfloat4 main(Vert v) : SV_Target { return float4(v.col, 1); }\n", "main", "ps", ps_code, error)) {
            return false;
        }
        Com<ID3D12Device2> device2 = kit.rig.as<ID3D12Device2>();
        if (!device2) {
            return fail_with(error, "ID3D12Device2 is not available");
        }
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC base = D3d12Tools::graphics_desc(stage.canvas.binder.signature, {}, {}, DXGI_FORMAT_R8G8B8A8_UNORM);
        MeshStream stream;
        stream.root = stage.canvas.binder.signature;
        stream.as = amplified ? D3d12Tools::bytecode(as_code) : D3D12_SHADER_BYTECODE{};
        stream.ms = D3d12Tools::bytecode(ms_code);
        stream.ps = D3d12Tools::bytecode(ps_code);
        stream.blend = base.BlendState;
        stream.mask = 0xffffffffu;
        stream.rasterizer = base.RasterizerState;
        D3D12_DEPTH_STENCIL_DESC ds = base.DepthStencilState;
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        stream.depth_stencil = ds;
        D3D12_RT_FORMAT_ARRAY formats{};
        formats.NumRenderTargets = 1;
        formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        stream.targets = formats;
        stream.depth_format = DXGI_FORMAT_D32_FLOAT;
        stream.sample = DXGI_SAMPLE_DESC{1, 0};
        D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof stream, &stream};
        Com<ID3D12PipelineState> created;
        const HRESULT result = device2->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), created.put_void());
        if (FAILED(result)) {
            return fail_with(error, "CreatePipelineState (mesh): " + D3d12Tools::hr(result));
        }
        state = kit.rig.hold(std::move(created));
        return true;
    }

    void record(ID3D12GraphicsCommandList* list, bool amplified, unsigned frame, bool clear) {
        ID3D12GraphicsCommandList6* list6 = nullptr;
        if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList6), reinterpret_cast<void**>(&list6)))) {
            return;
        }
        const float color[4] = {0.05f, 0.05f, 0.1f, 1};
        if (clear) {
            stage.canvas.begin(list, color);
        }
        list->SetPipelineState(state);
        const float params[4] = {static_cast<float>(frame) * 0.7f, static_cast<float>(frame), 0, 0};
        set_cbv_root(list, stage.view_projection() * rr::Mat4::rotate_x(-0.7f) * rr::Mat4::rotate_y(0.3f), params);
        list6->DispatchMesh(amplified ? 1 : 16, 1, 1);
        list6->Release();
    }
};

void register_mesh() {
    Cases::single(rr::CaseSpec("stages", "mesh_shader_meshlet_grid").variant(6).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("mesh").tag("x64-only"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        MeshRig rig;
        if (!rig.open(kit, false, error) || !kit.run([&](ID3D12GraphicsCommandList* list) { rig.record(list, false, 0, true); }, error)) {
            return false;
        }
        return rig.stage.finish(out, error);
    }, [](Kit& kit) { return needs_mesh(kit); });
    Cases::frames(rr::CaseSpec("stages", "amplification_shader_meshlet_culling_per_frame").variant(6).frames(4, {0, 1, 2, 3}).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("mesh").tag("x64-only"), [](Cases::Steps& steps) {
        auto rig = std::make_shared<MeshRig>();
        steps.on_setup = [rig](Kit& kit, std::string& error) { return rig->open(kit, true, error); };
        steps.on_frame = [rig](Kit& kit, unsigned frame, std::string& error) { return kit.submit([&](ID3D12GraphicsCommandList* list) { rig->record(list, true, frame, true); }, error); };
        steps.on_capture = [rig](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return rig->stage.finish(out, error); };
    }, [](Kit& kit) { return needs_mesh(kit); });
}

void register_shader_features() {
    Cases::single(rr::CaseSpec("stages", "sv_barycentrics_ps_sm6_1").variant(2).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("shader").tag("x64-only"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        const char* ps =
            "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
            "float4 main(VsOut i, float3 bary : SV_Barycentrics) : SV_Target { float edge = min(bary.x, min(bary.y, bary.z)); return float4(bary * (edge < 0.08 ? 1.0 : 0.6), 1); }\n";
        Pso builder = stage.pso(ps);
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, clear);
                list->SetPipelineState(state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(0.9f), 1, 1, 1);
            }, error)) {
            return false;
        }
        return stage.finish(out, error);
    }, [](Kit& kit) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{};
        const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS3, options);
        return SUCCEEDED(result) && options.BarycentricsSupported ? std::string() : std::string("SV_Barycentrics");
    });
    Cases::single(rr::CaseSpec("stages", "view_instancing_two_views_render_target_array").variant(2).tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("shader").tag("x64-only"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Binder binder;
        Surface target;
        if (!binder.create(kit, error) || !target.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error, 1, 1, 2)) {
            return false;
        }
        Geometry torus;
        if (!torus.create(kit, Shapes::torus(24, 12, 0.7f, 0.25f), true, error)) {
            return false;
        }
        const char* vs =
            "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\nstruct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; nointerpolation uint view : VIEW; };\n"
            "VsOut main(VsIn i, uint view : SV_ViewID) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.pos.x += (view == 0 ? -0.3 : 0.3) * o.pos.w; o.nrm = i.nrm; o.uv = i.uv; o.view = view; return o; }\n";
        const char* ps =
            "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; nointerpolation uint view : VIEW; };\n"
            "float4 main(VsOut i) : SV_Target { float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.8 + 0.2; return float4((i.view == 0 ? float3(0.9, 0.4, 0.3) : float3(0.3, 0.5, 0.95)) * l, 1); }\n";
        Kit::Bytes vs_code;
        Kit::Bytes ps_code;
        if (!kit.compile(Hlsl12::with_root(vs), 'v', vs_code, error) || !kit.compile(ps, 'p', ps_code, error)) {
            return false;
        }
        struct Stream {
            Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
            Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> vs;
            Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
            Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> layout;
            Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> targets;
            Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
            Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, D3D12_VIEW_INSTANCING_DESC> views;
        } stream;
        const std::vector<D3D12_INPUT_ELEMENT_DESC> layout = Shapes::layout();
        D3D12_VIEW_INSTANCE_LOCATION locations[2] = {{0, 0}, {0, 1}};
        stream.root = binder.signature;
        stream.vs = D3d12Tools::bytecode(vs_code);
        stream.ps = D3d12Tools::bytecode(ps_code);
        stream.layout = D3D12_INPUT_LAYOUT_DESC{layout.data(), static_cast<UINT>(layout.size())};
        D3D12_RT_FORMAT_ARRAY formats{};
        formats.NumRenderTargets = 1;
        formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        stream.targets = formats;
        stream.topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        stream.views = D3D12_VIEW_INSTANCING_DESC{2, locations, D3D12_VIEW_INSTANCING_FLAG_NONE};
        Com<ID3D12Device2> device2 = kit.rig.as<ID3D12Device2>();
        D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof stream, &stream};
        Com<ID3D12PipelineState> created;
        const HRESULT result = device2 ? device2->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), created.put_void()) : E_NOINTERFACE;
        if (FAILED(result)) {
            return fail_with(error, "CreatePipelineState (view instancing): " + D3d12Tools::hr(result));
        }
        ID3D12PipelineState* state = kit.rig.hold(std::move(created));
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                list->OMSetRenderTargets(1, &target.view, FALSE, nullptr);
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                list->ClearRenderTargetView(target.view, clear, 0, nullptr);
                const D3D12_VIEWPORT vp = viewport_of(128, 128);
                const D3D12_RECT scissor = rect_of(128, 128);
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &scissor);
                binder.bind_graphics(list);
                list->SetPipelineState(state);
                const float color[4] = {1, 1, 1, 1};
                set_cbv_root(list, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -3.0f) * rr::Mat4::rotate_x(0.8f), color);
                torus.bind(list);
                torus.draw(list);
            }, error)) {
            return false;
        }
        for (UINT slice = 0; slice < 2; ++slice) {
            rr::Image image;
            if (!kit.read_image(target.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, slice, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("view" + std::to_string(slice), std::move(image));
        }
        return true;
    }, [](Kit& kit) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{};
        const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS3, options);
        return SUCCEEDED(result) && options.ViewInstancingTier != D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED ? std::string() : std::string("view instancing");
    });
}

void register_barriers_and_misc() {
    Cases::single(rr::CaseSpec("barrier", "enhanced_barriers_layout_render_target_to_shader_resource").tolerance(rr::Tolerance::blend(3).with_edge(1)).tag("barrier"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error)) {
            return false;
        }
        Com<ID3D12Device10> device10 = kit.rig.as<ID3D12Device10>();
        if (!device10) {
            return fail_with(error, "ID3D12Device10 is not available");
        }
        D3D12_RESOURCE_DESC1 desc{};
        const D3D12_RESOURCE_DESC base = D3d12Tools::texture_desc(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        std::memcpy(&desc, &base, sizeof base);
        D3D12_CLEAR_VALUE clear_value{};
        clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
        Com<ID3D12Resource> created;
        HRESULT result = device10->CreateCommittedResource3(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_BARRIER_LAYOUT_RENDER_TARGET, &clear_value, nullptr, 0, nullptr, __uuidof(ID3D12Resource), created.put_void());
        if (FAILED(result)) {
            return fail_with(error, "CreateCommittedResource3: " + D3d12Tools::hr(result));
        }
        ID3D12Resource* texture = kit.rig.hold(std::move(created));
        HRESULT heap_result = S_OK;
        ID3D12DescriptorHeap* rtv_heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, &heap_result);
        if (rtv_heap == nullptr) {
            return fail_with(error, "RTV heap: " + D3d12Tools::hr(heap_result));
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE view = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        kit.device()->CreateRenderTargetView(texture, nullptr, view);
        stage.canvas.binder.srv_texture(0, texture, DXGI_FORMAT_R8G8B8A8_UNORM);
        stage.canvas.binder.sampler(0, Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
        Pso mesh_pso = stage.pso();
        Pso sample_pso = stage.pso(Hlsl12::kTexPs);
        ID3D12PipelineState* mesh_state = mesh_pso.build(error);
        ID3D12PipelineState* sample_state = sample_pso.build(error);
        if (mesh_state == nullptr || sample_state == nullptr) {
            return false;
        }
        bool supported = true;
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                ID3D12GraphicsCommandList7* list7 = nullptr;
                if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList7), reinterpret_cast<void**>(&list7)))) {
                    supported = false;
                    return;
                }
                list->OMSetRenderTargets(1, &view, FALSE, nullptr);
                const float clear[4] = {0.1f, 0.1f, 0.2f, 1};
                list->ClearRenderTargetView(view, clear, 0, nullptr);
                const D3D12_VIEWPORT vp = viewport_of(128, 128);
                const D3D12_RECT scissor = rect_of(128, 128);
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &scissor);
                stage.canvas.binder.bind_graphics(list);
                list->SetPipelineState(mesh_state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.9f, 0.5f, 0.2f);
                D3D12_TEXTURE_BARRIER texture_barrier{};
                texture_barrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
                texture_barrier.SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING;
                texture_barrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
                texture_barrier.AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
                texture_barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
                texture_barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
                texture_barrier.pResource = texture;
                texture_barrier.Subresources = {0xffffffffu, 0, 0, 0, 0, 0};
                D3D12_BARRIER_GROUP group{};
                group.Type = D3D12_BARRIER_TYPE_TEXTURE;
                group.NumBarriers = 1;
                group.pTextureBarriers = &texture_barrier;
                list7->Barrier(1, &group);
                const float background[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, background);
                list->SetPipelineState(sample_state);
                const float tint[4] = {1, 1, 1, 1};
                const float scale[4] = {1, 1, 0, 0};
                set_cbv_root(list, stage.view_projection(), tint, scale);
                stage.plane.bind(list);
                stage.plane.draw(list);
                list7->Release();
            },
            error);
        if (!supported) {
            return fail_with(error, "ID3D12GraphicsCommandList7 is not available");
        }
        return ok && stage.finish(out, error);
    }, [](Kit& kit) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
        const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS12, options);
        return SUCCEEDED(result) && options.EnhancedBarriersSupported ? std::string() : std::string("enhanced barriers");
    });
    Cases::single(rr::CaseSpec("copy", "discard_resource_then_clear_and_redraw").tolerance(rr::Tolerance::blend(2)).tag("copy"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        Pso builder = stage.pso();
        ID3D12PipelineState* state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                const float first[4] = {0.9f, 0.1f, 0.1f, 1};
                stage.canvas.begin(list, first);
                list->SetPipelineState(state);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.2f, 0.9f, 0.3f);
                list->DiscardResource(stage.canvas.color.resource, nullptr);
                list->DiscardResource(stage.canvas.depth.resource, nullptr);
                const float second[4] = {0.05f, 0.05f, 0.1f, 1};
                stage.canvas.begin(list, second);
                list->SetPipelineState(state);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(0.1f, 0.0f, 0.0f), 0.3f, 0.6f, 0.95f);
            }, error)) {
            return false;
        }
        return stage.finish(out, error);
    });
}

const struct Registrar {
    Registrar() {
        register_mesh();
        register_shader_features();
        register_barriers_and_misc();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
