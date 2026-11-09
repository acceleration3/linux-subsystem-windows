#include "render/d3d12/scene_util.hpp"
#include <array>

namespace lsw::suites::render::d3d12 {

namespace {

struct BloomState {
    Stage stage;
    Surface bright;
    Surface blur;
    Surface output;
    ID3D12PipelineState* mesh = nullptr;
    ID3D12PipelineState* bright_pso = nullptr;
    ID3D12PipelineState* blur_h = nullptr;
    ID3D12PipelineState* blur_v = nullptr;
    ID3D12PipelineState* composite = nullptr;
    float exposure = 0.6f;
};

void register_bloom() {
    Cases::frames(rr::CaseSpec("scene", "hdr_bloom_tonemap_with_adapting_exposure").size(128, 128).frames(5, {0, 2, 4}).tolerance(rr::Tolerance::blend(5).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<BloomState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT) || !s->bright.color(kit, 64, 64, DXGI_FORMAT_R16G16B16A16_FLOAT, error) ||
                !s->blur.color(kit, 64, 64, DXGI_FORMAT_R16G16B16A16_FLOAT, error) || !s->output.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
                return false;
            }
            Binder& binder = s->stage.canvas.binder;
            binder.srv_texture(0, s->stage.canvas.color.resource, DXGI_FORMAT_R16G16B16A16_FLOAT);
            binder.srv_texture(1, s->bright.resource, DXGI_FORMAT_R16G16B16A16_FLOAT);
            binder.srv_texture(2, s->blur.resource, DXGI_FORMAT_R16G16B16A16_FLOAT);
            const char* common = "Texture2D<float4> hdr : register(t0); Texture2D<float4> bright : register(t1); Texture2D<float4> blur : register(t2); SamplerState smp : register(s2);\n";
            auto pass = [&](const char* body, DXGI_FORMAT format) {
                Pso builder(kit, binder.signature);
                builder.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root((std::string(common) + body).c_str())).layout({}).targets({format});
                return builder.build(error);
            };
            s->mesh = s->stage.pso().build(error);
            s->bright_pso = pass("float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int2 p = int2(pos.xy) * 2; float3 c = (hdr.Load(int3(p, 0)).rgb + hdr.Load(int3(p + int2(1, 0), 0)).rgb + hdr.Load(int3(p + int2(0, 1), 0)).rgb + hdr.Load(int3(p + int2(1, 1), 0)).rgb) * 0.25; return float4(max(c - 1.0, 0), 1); }\n", DXGI_FORMAT_R16G16B16A16_FLOAT);
            s->blur_h = pass("float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int2 p = int2(pos.xy); float3 sum = 0; for (int k = -4; k <= 4; ++k) sum += bright.Load(int3(clamp(p + int2(k, 0), 0, 63), 0)).rgb * exp(-0.12 * k * k); return float4(sum * 0.22, 1); }\n", DXGI_FORMAT_R16G16B16A16_FLOAT);
            s->blur_v = pass("float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int2 p = int2(pos.xy); float3 sum = 0; for (int k = -4; k <= 4; ++k) sum += blur.Load(int3(clamp(p + int2(0, k), 0, 63), 0)).rgb * exp(-0.12 * k * k); return float4(sum * 0.22, 1); }\n", DXGI_FORMAT_R16G16B16A16_FLOAT);
            s->composite = pass("float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float3 c = hdr.Load(int3(pos.xy, 0)).rgb + bright.SampleLevel(smp, uv, 0).rgb * 0.9; c = 1 - exp(-c * p0.x); return float4(pow(c, 1.0 / 2.2), 1); }\n", DXGI_FORMAT_R8G8B8A8_UNORM);
            return s->mesh != nullptr && s->bright_pso != nullptr && s->blur_h != nullptr && s->blur_v != nullptr && s->composite != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const float intensity = 2.0f + 6.0f * (frame % 3 == 1 ? 1.0f : 0.2f);
            const float target = 1.1f / (0.5f + intensity * 0.12f);
            s->exposure += (target - s->exposure) * 0.5f;
            const float exposure = s->exposure;
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                Canvas& canvas = s->stage.canvas;
                const float clear[4] = {0.02f, 0.02f, 0.04f, 1};
                canvas.begin(list, clear);
                list->SetPipelineState(s->mesh);
                s->stage.mesh(list, s->stage.torus, rr::Mat4::rotate_x(0.9f), 0.7f, 0.4f, 0.2f);
                s->stage.mesh(list, s->stage.sphere, rr::Mat4::translate(0.5f * std::cos(static_cast<float>(frame)), 0.4f, 0.3f) * rr::Mat4::scale(0.35f, 0.35f, 0.35f), intensity, intensity * 0.8f, intensity * 0.4f);
                canvas.color.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                const float none[4] = {0, 0, 0, 0};
                const float params[4] = {exposure, 0, 0, 0};
                pass_target(list, s->bright, 64, 64, nullptr);
                canvas.binder.bind_graphics(list);
                set_cbv_root(list, rr::Mat4::identity(), params);
                draw_fullscreen(list, s->bright_pso);
                s->bright.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                pass_target(list, s->blur, 64, 64, nullptr);
                canvas.binder.bind_graphics(list);
                draw_fullscreen(list, s->blur_h);
                s->blur.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                s->bright.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
                pass_target(list, s->bright, 64, 64, nullptr);
                canvas.binder.bind_graphics(list);
                draw_fullscreen(list, s->blur_v);
                s->bright.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                pass_target(list, s->output, 128, 128, nullptr);
                canvas.binder.bind_graphics(list);
                set_cbv_root(list, rr::Mat4::identity(), params);
                draw_fullscreen(list, s->composite);
                (void)none;
                s->stage.canvas.color.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                s->blur.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
            }, error);
        };
        steps.on_capture = [s](Kit& kit, unsigned, rr::CaptureSet& out, std::string& error) {
            rr::Image image;
            if (!kit.read_image(s->output.resource, s->output.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        };
    });
}

void register_oit() {
    Cases::single(rr::CaseSpec("scene", "weighted_blended_oit_three_translucent_layers").tolerance(rr::Tolerance::blend(6).with_edge(1)).tag("scene"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        Surface accum;
        Surface reveal;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT) || !accum.color(kit, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT, error) || !reveal.color(kit, 128, 128, DXGI_FORMAT_R16_FLOAT, error)) {
            return false;
        }
        Binder& binder = stage.canvas.binder;
        binder.srv_texture(0, accum.resource, DXGI_FORMAT_R16G16B16A16_FLOAT);
        binder.srv_texture(1, reveal.resource, DXGI_FORMAT_R16_FLOAT);
        const char* ps =
            "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\nstruct Out { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };\n"
            "Out main(VsOut i) { Out o; float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.7 + 0.3; float a = p0.a; float w = clamp(0.03 / (1e-5 + pow(i.pos.z, 4.0)), 0.01, 300.0);\n"
            "  o.c0 = float4(p0.rgb * l * a, a) * w; o.c1 = float4(a, 0, 0, 0); return o; }\n";
        Pso accumulate = stage.pso(ps);
        accumulate.targets({DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16_FLOAT});
        accumulate.desc.BlendState.IndependentBlendEnable = TRUE;
        accumulate.desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        D3D12_RENDER_TARGET_BLEND_DESC& add = accumulate.desc.BlendState.RenderTarget[0];
        add = {};
        add.BlendEnable = TRUE;
        add.SrcBlend = D3D12_BLEND_ONE;
        add.DestBlend = D3D12_BLEND_ONE;
        add.SrcBlendAlpha = D3D12_BLEND_ONE;
        add.DestBlendAlpha = D3D12_BLEND_ONE;
        add.BlendOp = D3D12_BLEND_OP_ADD;
        add.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        add.RenderTargetWriteMask = 0xf;
        D3D12_RENDER_TARGET_BLEND_DESC& multiply = accumulate.desc.BlendState.RenderTarget[1];
        multiply = {};
        multiply.BlendEnable = TRUE;
        multiply.SrcBlend = D3D12_BLEND_ZERO;
        multiply.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        multiply.SrcBlendAlpha = D3D12_BLEND_ZERO;
        multiply.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        multiply.BlendOp = D3D12_BLEND_OP_ADD;
        multiply.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        multiply.RenderTargetWriteMask = 0xf;
        Pso composite(kit, binder.signature);
        composite.vs(Hlsl12::kFullVs)
            .ps(Hlsl12::with_root("Texture2D<float4> accum : register(t0); Texture2D<float> reveal : register(t1);\nfloat4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int3 p = int3(pos.xy, 0); float4 a = accum.Load(p); float r = reveal.Load(p);\n"
                                  "  float3 bg = lerp(float3(0.2, 0.35, 0.6), float3(0.8, 0.7, 0.3), uv.x) * 0.7; float3 avg = a.rgb / max(a.a, 1e-5); return float4(avg * (1 - r) + bg * r, 1); }\n"))
            .layout({})
            .targets({DXGI_FORMAT_R8G8B8A8_UNORM});
        ID3D12PipelineState* accumulate_state = accumulate.build(error);
        ID3D12PipelineState* composite_state = composite.build(error);
        if (accumulate_state == nullptr || composite_state == nullptr) {
            return false;
        }
        return kit.run([&](ID3D12GraphicsCommandList* list) {
                   const D3D12_CPU_DESCRIPTOR_HANDLE views[2] = {accum.view, reveal.view};
                   accum.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
                   reveal.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
                   stage.canvas.depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                   list->OMSetRenderTargets(2, views, FALSE, &stage.canvas.depth.view);
                   const float zero[4] = {0, 0, 0, 0};
                   const float one[4] = {1, 1, 1, 1};
                   list->ClearRenderTargetView(accum.view, zero, 0, nullptr);
                   list->ClearRenderTargetView(reveal.view, one, 0, nullptr);
                   list->ClearDepthStencilView(stage.canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                   stage.canvas.set_viewport(list);
                   binder.bind_graphics(list);
                   list->SetPipelineState(accumulate_state);
                   stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.3f, 0.1f, 0.2f), 0.9f, 0.3f, 0.2f, 0.5f);
                   stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.2f, 0.8f, 0.4f, 0.55f);
                   stage.mesh(list, stage.sphere, rr::Mat4::translate(0.35f, -0.1f, -0.2f), 0.3f, 0.4f, 0.95f, 0.45f);
                   accum.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                   reveal.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                   stage.canvas.color.bind_target(list);
                   binder.bind_graphics(list);
                   draw_fullscreen(list, composite_state);
               }, error) &&
               stage.finish(out, error);
    });
}

struct ForestState {
    Stage stage;
    Geometry trunk;
    Geometry canopy_near;
    Geometry canopy_far;
    ID3D12Resource* near_buffer = nullptr;
    ID3D12Resource* far_buffer = nullptr;
    ID3D12PipelineState* state = nullptr;
    std::vector<std::array<float, 4>> trees;
};

void register_forest() {
    Cases::frames(rr::CaseSpec("scene", "instanced_forest_two_lods_camera_flythrough").size(256, 256).frames(4, {0, 1, 2, 3}).tolerance(rr::Tolerance::blend(5).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<ForestState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 256) || !s->trunk.create(kit, Shapes::cube(), false, error) || !s->canopy_near.create(kit, Shapes::sphere(12, 16, 1.0f), false, error) ||
                !s->canopy_far.create(kit, Shapes::sphere(4, 6, 1.0f), false, error)) {
                return false;
            }
            std::uint32_t seed = 31337;
            for (int k = 0; k < 400; ++k) {
                s->trees.push_back({(rr::Pattern::unit_random(seed) - 0.5f) * 14.0f, (rr::Pattern::unit_random(seed) - 0.5f) * 20.0f, 0.5f + rr::Pattern::unit_random(seed) * 0.6f, rr::Pattern::unit_random(seed)});
            }
            s->near_buffer = kit.make_upload(nullptr, 400 * 16, error);
            s->far_buffer = kit.make_upload(nullptr, 400 * 16, error);
            std::vector<D3D12_INPUT_ELEMENT_DESC> layout = Shapes::layout();
            layout.push_back({"INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1});
            const std::string vs = std::string(Hlsl12::kRoot) +
                "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float4 inst : INSTANCE; };\nstruct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float shade : SHADE; };\n"
                "VsOut main(VsIn i) { VsOut o; float3 local = i.pos * float3(p1.x, p1.y, p1.x) * i.inst.z; local.y += p1.z * i.inst.z; float c = cos(i.inst.w * 6.28), s = sin(i.inst.w * 6.28);\n"
                "  float3 world = float3(local.x * c - local.z * s + i.inst.x, local.y, local.x * s + local.z * c + i.inst.y); o.pos = mul(mvp, float4(world, 1)); o.nrm = i.nrm; o.shade = 0.7 + 0.3 * frac(i.inst.w * 7.0); return o; }\n";
            const char* ps =
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float shade : SHADE; };\nfloat4 main(VsOut i) : SV_Target { float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.7 + 0.3; return float4(p0.rgb * l * i.shade, 1); }\n";
            Pso builder(kit, s->stage.canvas.binder.signature);
            builder.vs(vs).ps(Hlsl12::with_root(ps)).layout(layout).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
            s->state = builder.build(error);
            return s->near_buffer != nullptr && s->far_buffer != nullptr && s->state != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const float camera_z = 7.0f - 3.0f * static_cast<float>(frame);
            std::vector<std::array<float, 4>> near_list;
            std::vector<std::array<float, 4>> far_list;
            for (const auto& tree : s->trees) {
                const float distance = std::fabs(tree[1] - camera_z);
                (distance < 6.0f ? near_list : far_list).push_back(tree);
            }
            std::uint8_t* mapped = nullptr;
            const D3D12_RANGE none{0, 0};
            s->near_buffer->Map(0, &none, reinterpret_cast<void**>(&mapped));
            std::memcpy(mapped, near_list.data(), near_list.size() * 16);
            s->near_buffer->Unmap(0, nullptr);
            s->far_buffer->Map(0, &none, reinterpret_cast<void**>(&mapped));
            std::memcpy(mapped, far_list.data(), far_list.size() * 16);
            s->far_buffer->Unmap(0, nullptr);
            const float eye[3] = {0.0f, 3.2f, camera_z + 3.0f};
            const float center[3] = {0.0f, 0.6f, camera_z - 5.0f};
            const float up[3] = {0, 1, 0};
            const rr::Mat4 vp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 60.0f) * SceneMath::look_at(eye, center, up);
            const UINT near_count = static_cast<UINT>(near_list.size());
            const UINT far_count = static_cast<UINT>(far_list.size());
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.45f, 0.6f, 0.8f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->state);
                const float trunk_color[4] = {0.45f, 0.3f, 0.18f, 1};
                const float canopy_color[4] = {0.2f, 0.55f, 0.25f, 1};
                const float trunk_shape[4] = {0.08f, 0.8f, 0.8f, 0};
                const float canopy_shape[4] = {0.55f, 0.55f, 1.6f, 0};
                const D3D12_VERTEX_BUFFER_VIEW near_view{s->near_buffer->GetGPUVirtualAddress(), near_count * 16, 16};
                const D3D12_VERTEX_BUFFER_VIEW far_view{s->far_buffer->GetGPUVirtualAddress(), far_count * 16, 16};
                set_cbv_root(list, vp, trunk_color, trunk_shape);
                s->trunk.bind(list);
                list->IASetVertexBuffers(1, 1, &near_view);
                list->DrawIndexedInstanced(s->trunk.index_count, near_count, 0, 0, 0);
                list->IASetVertexBuffers(1, 1, &far_view);
                list->DrawIndexedInstanced(s->trunk.index_count, far_count, 0, 0, 0);
                set_cbv_root(list, vp, canopy_color, canopy_shape);
                s->canopy_near.bind(list);
                list->IASetVertexBuffers(1, 1, &near_view);
                list->DrawIndexedInstanced(s->canopy_near.index_count, near_count, 0, 0, 0);
                s->canopy_far.bind(list);
                list->IASetVertexBuffers(1, 1, &far_view);
                list->DrawIndexedInstanced(s->canopy_far.index_count, far_count, 0, 0, 0);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

struct CubeState {
    Stage stage;
    Surface cube;
    Surface face_depth;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    UINT increment = 0;
    ID3D12PipelineState* lit = nullptr;
    ID3D12PipelineState* reflective = nullptr;
};

void register_cube() {
    Cases::frames(rr::CaseSpec("scene", "dynamic_cube_map_reflections_six_faces_per_frame").size(128, 128).frames(3, {0, 1, 2}).tolerance(rr::Tolerance::blend(6).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<CubeState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT) || !s->cube.color(kit, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, error, 1, 1, 6) || !s->face_depth.depth(kit, 64, 64, DXGI_FORMAT_D32_FLOAT, error)) {
                return false;
            }
            HRESULT result = S_OK;
            s->rtv_heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6, false, &result);
            if (s->rtv_heap == nullptr) {
                return fail_with(error, "RTV heap: " + D3d12Tools::hr(result));
            }
            s->increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            for (UINT face = 0; face < 6; ++face) {
                D3D12_RENDER_TARGET_VIEW_DESC rtv{};
                rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv.Texture2DArray.FirstArraySlice = face;
                rtv.Texture2DArray.ArraySize = 1;
                kit.device()->CreateRenderTargetView(s->cube.resource, &rtv, D3d12Tools::cpu_at(s->rtv_heap, face, s->increment));
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.TextureCube.MipLevels = 1;
            s->stage.canvas.binder.srv(0, s->cube.resource, &srv);
            Pso lit = s->stage.pso();
            lit.depth(DXGI_FORMAT_D32_FLOAT);
            const char* reflect_ps =
                "TextureCube<float4> env : register(t0); SamplerState smp : register(s0);\nstruct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "float4 main(VsOut i) : SV_Target { float3 n = normalize(i.nrm); float3 v = normalize(float3(0, 0, -1)); float3 r = reflect(v, n); return float4(env.Sample(smp, r).rgb * 0.9 + 0.05, 1); }\n";
            Pso reflective = s->stage.pso(reflect_ps);
            s->lit = lit.build(error);
            s->reflective = reflective.build(error);
            return s->lit != nullptr && s->reflective != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const float forwards[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const float ups[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
            const float origin[3] = {0, 0, 0};
            const float t = static_cast<float>(frame);
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                Canvas& canvas = s->stage.canvas;
                s->cube.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
                s->face_depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                const rr::Mat4 projection = rr::Mat4::perspective_d3d(1.5707963f, 1.0f, 0.1f, 20.0f);
                const float colors[6][4] = {{0.9f, 0.2f, 0.2f, 1}, {0.2f, 0.9f, 0.3f, 1}, {0.3f, 0.4f, 0.95f, 1}, {0.95f, 0.9f, 0.2f, 1}, {0.9f, 0.4f, 0.9f, 1}, {0.2f, 0.9f, 0.9f, 1}};
                for (UINT face = 0; face < 6; ++face) {
                    const D3D12_CPU_DESCRIPTOR_HANDLE view = D3d12Tools::cpu_at(s->rtv_heap, face, s->increment);
                    list->OMSetRenderTargets(1, &view, FALSE, &s->face_depth.view);
                    const float clear[4] = {0.1f, 0.12f, 0.2f, 1};
                    list->ClearRenderTargetView(view, clear, 0, nullptr);
                    list->ClearDepthStencilView(s->face_depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                    const D3D12_VIEWPORT vp = viewport_of(64, 64);
                    const D3D12_RECT scissor = rect_of(64, 64);
                    list->RSSetViewports(1, &vp);
                    list->RSSetScissorRects(1, &scissor);
                    canvas.binder.bind_graphics(list);
                    list->SetPipelineState(s->lit);
                    const float target[3] = {forwards[face][0], forwards[face][1], forwards[face][2]};
                    const rr::Mat4 vp_matrix = projection * SceneMath::look_at(origin, target, ups[face]);
                    for (UINT k = 0; k < 6; ++k) {
                        const float angle = t * 0.5f + static_cast<float>(k);
                        const rr::Mat4 model = rr::Mat4::translate(2.2f * forwards[k][0] + 0.3f * std::sin(angle), 2.2f * forwards[k][1] + 0.3f * std::cos(angle), 2.2f * forwards[k][2]) * rr::Mat4::scale(0.8f, 0.8f, 0.8f);
                        set_cbv_root(list, vp_matrix * model, colors[k]);
                        s->stage.sphere.bind(list);
                        s->stage.sphere.draw(list);
                    }
                }
                s->cube.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                const float background[4] = {0.05f, 0.05f, 0.1f, 1};
                canvas.begin(list, background);
                list->SetPipelineState(s->reflective);
                s->stage.mesh(list, s->stage.sphere, rr::Mat4::scale(1.2f, 1.2f, 1.2f), 1, 1, 1);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

const struct Registrar {
    Registrar() {
        register_bloom();
        register_oit();
        register_forest();
        register_cube();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
