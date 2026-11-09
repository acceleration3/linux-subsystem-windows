#include "render/d3d12/scene_util.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

constexpr UINT kSize = 256;

struct Obj {
    const Geometry* geometry;
    rr::Mat4 model;
    float color[4];
};

const char* const kWorldVs =
    "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float3 wpos : WPOS; };\n"
    "float4 xf(int k, float4 v) { return mul(v, float4x4(u[k], u[k + 1], u[k + 2], u[k + 3])); }\n"
    "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.wpos = xf(0, float4(i.pos, 1)).xyz; o.nrm = xf(0, float4(i.nrm, 0)).xyz; return o; }\n";

void copy_matrix(std::uint8_t* at, UINT float4_index, const rr::Mat4& matrix) { std::memcpy(at + float4_index * 16, matrix.m, 64); }

std::vector<Obj> world_objects(Stage& stage, const Geometry& ground, float spin) {
    return {{&ground, rr::Mat4::translate(0, -0.8f, -2.0f), {0.55f, 0.6f, 0.5f, 1}},
            {&stage.cube, rr::Mat4::translate(-1.6f, -0.1f, 1.2f) * rr::Mat4::rotate_y(0.5f + spin) * rr::Mat4::scale(0.7f, 0.7f, 0.7f), {0.85f, 0.45f, 0.2f, 1}},
            {&stage.sphere, rr::Mat4::translate(1.2f, 0.0f, 0.2f) * rr::Mat4::scale(0.9f, 0.9f, 0.9f), {0.3f, 0.6f, 0.95f, 1}},
            {&stage.torus, rr::Mat4::translate(-0.6f, 0.2f, -3.0f) * rr::Mat4::rotate_x(1.1f + spin), {0.9f, 0.8f, 0.3f, 1}},
            {&stage.cube, rr::Mat4::translate(2.0f, 0.4f, -5.5f) * rr::Mat4::rotate_y(-0.4f) * rr::Mat4::scale(1.2f, 1.6f, 1.2f), {0.5f, 0.85f, 0.5f, 1}},
            {&stage.sphere, rr::Mat4::translate(-2.4f, 0.6f, -7.5f) * rr::Mat4::scale(1.6f, 1.6f, 1.6f), {0.85f, 0.35f, 0.5f, 1}}};
}

struct CsmState {
    Stage stage;
    Geometry ground;
    ID3D12Resource* shadow = nullptr;
    ID3D12DescriptorHeap* dsv_heap = nullptr;
    ID3D12PipelineState* depth_pso = nullptr;
    ID3D12PipelineState* lit_pso = nullptr;
    UploadRing ring;
    UINT dsv_increment = 0;
};

constexpr UINT kCascadeSize = 128;

void register_csm() {
    Cases::frames(rr::CaseSpec("scene", "cascaded_shadow_maps_texture_array_camera_pan").size(kSize, kSize).frames(4, {0, 3}).tolerance(rr::Tolerance::blend(10).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<CsmState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, kSize) || !s->ground.create(kit, Shapes::grid(40, 9.0f), true, error) ||
                !s->ring.create(kit, 256, 8, error)) {
                return false;
            }
            HRESULT result = S_OK;
            D3D12_CLEAR_VALUE clear{};
            clear.Format = DXGI_FORMAT_D32_FLOAT;
            clear.DepthStencil.Depth = 1.0f;
            s->shadow = D3d12Tools::texture(kit.rig, D3d12Tools::texture_desc(kCascadeSize, kCascadeSize, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 1, 3, 1),
                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, &result);
            s->dsv_heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 3, false, &result);
            if (s->shadow == nullptr || s->dsv_heap == nullptr) {
                return fail_with(error, "cascade array creation: " + D3d12Tools::hr(result));
            }
            s->dsv_increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            for (UINT c = 0; c < 3; ++c) {
                D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
                dsv.Format = DXGI_FORMAT_D32_FLOAT;
                dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsv.Texture2DArray.FirstArraySlice = c;
                dsv.Texture2DArray.ArraySize = 1;
                kit.device()->CreateDepthStencilView(s->shadow, &dsv, D3d12Tools::cpu_at(s->dsv_heap, c, s->dsv_increment));
            }
            Binder& binder = s->stage.canvas.binder;
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R32_FLOAT;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2DArray.MipLevels = 1;
            srv.Texture2DArray.ArraySize = 3;
            binder.srv(0, s->shadow, &srv);
            D3D12_SAMPLER_DESC cmp = Binder::sampler_desc(D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
            cmp.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            cmp.BorderColor[0] = cmp.BorderColor[1] = cmp.BorderColor[2] = cmp.BorderColor[3] = 1.0f;
            binder.sampler(0, cmp);
            Pso depth_builder(kit, binder.signature);
            depth_builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).targets({}).depth(DXGI_FORMAT_D32_FLOAT);
            depth_builder.desc.NumRenderTargets = 0;
            depth_builder.desc.RasterizerState.DepthBias = 300;
            depth_builder.desc.RasterizerState.SlopeScaledDepthBias = 2.0f;
            s->depth_pso = depth_builder.build(error);
            const char* ps =
                "Texture2DArray<float> shadows : register(t0); SamplerComparisonState cmp : register(s0);\n"
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float3 wpos : WPOS; };\n"
                "float4 main(VsOut i) : SV_Target {\n"
                "  float d = 1.0 / i.pos.w; uint c = d < p1.x ? 0 : (d < p1.y ? 1 : 2);\n"
                "  if (p3.x > 0.5) c = min(c + 1, 2u);\n"
                "  float4 lp = mul(float4(i.wpos, 1), float4x4(u[4 + c * 4], u[5 + c * 4], u[6 + c * 4], u[7 + c * 4]));\n"
                "  float3 q = lp.xyz / lp.w; float2 uv = q.xy * float2(0.5, -0.5) + 0.5; float sh = 0;\n"
                "  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) sh += shadows.SampleCmp(cmp, float3(uv + float2(x, y) / 128.0, c), q.z);\n"
                "  sh /= 9.0; float l = saturate(dot(normalize(i.nrm), normalize(float3(0.4, 0.9, 0.3))));\n"
                "  float3 tint = c == 0 ? float3(1.0, 0.82, 0.82) : (c == 1 ? float3(0.82, 1.0, 0.82) : float3(0.82, 0.82, 1.0));\n"
                "  return float4(p0.rgb * tint * (0.25 + 0.75 * l * sh), 1); }\n";
            Pso lit(kit, binder.signature);
            lit.vs(Hlsl12::with_root(kWorldVs)).ps(Hlsl12::with_root(ps)).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
            s->lit_pso = lit.build(error);
            return s->depth_pso != nullptr && s->lit_pso != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const float up[3] = {0, 1, 0};
            const float eye[3] = {-1.6f + 0.9f * static_cast<float>(frame), 3.0f, 9.0f};
            const float look[3] = {0.0f, 0.0f, -2.0f};
            const rr::Mat4 camera_vp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * SceneMath::look_at(eye, look, up);
            float forward[3] = {look[0] - eye[0], look[1] - eye[1], look[2] - eye[2]};
            const float flen = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
            for (float& v : forward) {
                v /= flen;
            }
            const float depths[3] = {4.5f, 10.0f, 20.0f};
            const float radii[3] = {3.6f, 6.4f, 12.0f};
            rr::Mat4 light_vp[3];
            for (UINT c = 0; c < 3; ++c) {
                const float center[3] = {eye[0] + forward[0] * depths[c], eye[1] + forward[1] * depths[c], eye[2] + forward[2] * depths[c]};
                const float light_eye[3] = {center[0] + 4.0f, center[1] + 9.0f, center[2] + 3.0f};
                light_vp[c] = SceneMath::ortho_d3d(radii[c] * 2.0f, radii[c] * 2.0f, 0.5f, 30.0f) * SceneMath::look_at(light_eye, center, up);
            }
            const std::vector<Obj> objects = world_objects(s->stage, s->ground, 0.15f * static_cast<float>(frame));
            for (UINT k = 0; k < objects.size(); ++k) {
                std::uint8_t* slot = s->ring.at(k);
                copy_matrix(slot, 0, objects[k].model);
                for (UINT c = 0; c < 3; ++c) {
                    copy_matrix(slot, 4 + c * 4, light_vp[c]);
                }
            }
            const float splits[4] = {7.0f, 14.0f, 0, 0};
            const float brk[4] = {kit.broken("csm_cascade") ? 1.0f : 0.0f, 0, 0, 0};
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                Binder& binder = s->stage.canvas.binder;
                D3d12Tools::move(list, s->shadow, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                binder.bind_graphics(list);
                const D3D12_VIEWPORT vp = viewport_of(kCascadeSize, kCascadeSize);
                const D3D12_RECT scissor = rect_of(kCascadeSize, kCascadeSize);
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &scissor);
                for (UINT c = 0; c < 3; ++c) {
                    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = D3d12Tools::cpu_at(s->dsv_heap, c, s->dsv_increment);
                    list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
                    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                    list->SetPipelineState(s->depth_pso);
                    for (const Obj& o : objects) {
                        list->SetGraphicsRoot32BitConstants(0, 16, (light_vp[c] * o.model).m, 0);
                        o.geometry->bind(list);
                        o.geometry->draw(list);
                    }
                }
                D3d12Tools::move(list, s->shadow, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                const float clear[4] = {0.1f, 0.12f, 0.18f, 1};
                s->stage.canvas.begin(list, clear);
                list->SetPipelineState(s->lit_pso);
                for (UINT k = 0; k < objects.size(); ++k) {
                    set_cbv_root(list, camera_vp * objects[k].model, objects[k].color, splits);
                    list->SetGraphicsRoot32BitConstants(0, 4, brk, 28);
                    list->SetGraphicsRootConstantBufferView(1, s->ring.address(k));
                    objects[k].geometry->bind(list);
                    objects[k].geometry->draw(list);
                }
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

constexpr UINT kLights = 48;
constexpr UINT kTiles = 16;

struct LightData {
    float world[4];
    float color[4];
    float view[4];
};

struct ForwardPlusState {
    Stage stage;
    Cs cull;
    Geometry ground;
    ID3D12Resource* lists = nullptr;
    D3D12_RESOURCE_STATES lists_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12PipelineState* depth_pso = nullptr;
    ID3D12PipelineState* lit_pso = nullptr;
    UploadRing lights;
    UploadRing models;
};

void register_forward_plus() {
    Cases::frames(rr::CaseSpec("scene", "forward_plus_compute_light_culling_48_lights").size(kSize, kSize).frames(4, {0, 3}).tolerance(rr::Tolerance::blend(8).with_edge(1)).tag("scene"), [](Cases::Steps& steps) {
        auto s = std::make_shared<ForwardPlusState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, kSize) || !s->ground.create(kit, Shapes::grid(40, 9.0f), true, error) ||
                !s->lights.create(kit, kLights * sizeof(LightData), 1, error) || !s->models.create(kit, 256, 8, error)) {
                return false;
            }
            const char* cs =
                "struct L { float4 w; float4 c; float4 v; };\n"
                "Texture2D<float> depth : register(t0); StructuredBuffer<L> lights : register(t1); RWStructuredBuffer<uint> lists : register(u0);\n"
                "groupshared uint zmin; groupshared uint zmax; groupshared uint count;\n"
                "[numthreads(16, 16, 1)] void main(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex, uint3 dt : SV_DispatchThreadID) {\n"
                "  if (gi == 0) { zmin = 0x7f7fffff; zmax = 0; count = 0; }\n"
                "  GroupMemoryBarrierWithGroupSync();\n"
                "  float d = depth.Load(int3(dt.xy, 0)); float z = 0.5 * 40.0 / (40.0 - d * 39.5);\n"
                "  InterlockedMin(zmin, asuint(z)); InterlockedMax(zmax, asuint(z));\n"
                "  GroupMemoryBarrierWithGroupSync();\n"
                "  float zlo = asfloat(zmin); float zhi = asfloat(zmax); uint tile = gid.y * 16 + gid.x;\n"
                "  float2 tmin = float2(gid.xy) * 16.0; float2 tmax = tmin + 16.0;\n"
                "  if (gi < 48) { L l = lights[gi]; float r = l.v.w; float w = -l.v.z; bool vis = true;\n"
                "    if (w - r > zhi || w + r < zlo) vis = false;\n"
                "    if (w > r) { float s = 0.5 * 256.0 * p0.x / w; float2 c = float2((l.v.x * p0.x / w * 0.5 + 0.5) * 256.0, (0.5 - l.v.y * p0.x / w * 0.5) * 256.0);\n"
                "      float rad = r * s; if (p3.x > 0.5) rad *= 0.45; float2 q = clamp(c, tmin, tmax); if (length(q - c) > rad) vis = false; }\n"
                "    if (vis) { uint slot; InterlockedAdd(count, 1, slot); lists[tile * 65 + 1 + slot] = gi; } }\n"
                "  GroupMemoryBarrierWithGroupSync();\n"
                "  if (gi == 0) lists[tile * 65] = count; }\n";
            if (!s->cull.open(kit, cs, error)) {
                return false;
            }
            HRESULT result = S_OK;
            s->lists = D3d12Tools::device_buffer(kit.rig, kTiles * kTiles * 65 * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
            if (s->lists == nullptr) {
                return fail_with(error, "tile list buffer: " + D3d12Tools::hr(result));
            }
            Binder& binder = s->stage.canvas.binder;
            s->cull.binder.srv_texture(0, s->stage.canvas.depth.resource, DXGI_FORMAT_R32_FLOAT);
            s->cull.binder.srv_buffer(1, s->lights.buffer, DXGI_FORMAT_UNKNOWN, 0, kLights, sizeof(LightData));
            s->cull.binder.uav_buffer(0, s->lists, DXGI_FORMAT_UNKNOWN, 0, kTiles * kTiles * 65, 4);
            binder.srv_buffer(1, s->lights.buffer, DXGI_FORMAT_UNKNOWN, 0, kLights, sizeof(LightData));
            binder.srv_buffer(2, s->lists, DXGI_FORMAT_UNKNOWN, 0, kTiles * kTiles * 65, 4);
            Pso depth_builder(kit, binder.signature);
            depth_builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).targets({}).depth(DXGI_FORMAT_D32_FLOAT);
            depth_builder.desc.NumRenderTargets = 0;
            s->depth_pso = depth_builder.build(error);
            const char* ps =
                "struct L { float4 w; float4 c; float4 v; };\n"
                "StructuredBuffer<L> lights : register(t1); StructuredBuffer<uint> lists : register(t2);\n"
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float3 wpos : WPOS; };\n"
                "float4 main(VsOut i) : SV_Target {\n"
                "  uint2 t = uint2(i.pos.xy) / 16; uint base = (t.y * 16 + t.x) * 65; uint n = lists[base]; float3 N = normalize(i.nrm); float3 acc = 0;\n"
                "  for (uint k = 0; k < n; ++k) { L l = lights[lists[base + 1 + k]]; float3 d = l.w.xyz - i.wpos; float dist = length(d);\n"
                "    float att = saturate(1.0 - dist / l.w.w); att *= att; acc += l.c.rgb * att * saturate(dot(N, d / max(dist, 0.001))) * 2.2; }\n"
                "  return float4(p0.rgb * (0.08 + acc), 1); }\n";
            Pso lit(kit, binder.signature);
            lit.vs(Hlsl12::with_root(kWorldVs)).ps(Hlsl12::with_root(ps)).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
            lit.desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            s->lit_pso = lit.build(error);
            return s->depth_pso != nullptr && s->lit_pso != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const float up[3] = {0, 1, 0};
            const float eye[3] = {0.0f, 3.2f, 7.5f};
            const float look[3] = {0.0f, -0.2f, -1.0f};
            const rr::Mat4 view = SceneMath::look_at(eye, look, up);
            const rr::Mat4 projection = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f);
            const rr::Mat4 camera_vp = projection * view;
            const float t = static_cast<float>(frame);
            auto* data = reinterpret_cast<LightData*>(s->lights.at(0));
            for (UINT k = 0; k < kLights; ++k) {
                const float gx = static_cast<float>(k % 8) - 3.5f;
                const float gz = static_cast<float>(k / 8) - 2.5f;
                const float a = 0.5f * t + static_cast<float>(k) * 0.7f;
                const float wx = gx * 1.15f + 0.35f * std::cos(a);
                const float wz = gz * 1.3f - 1.5f + 0.35f * std::sin(a);
                const float wy = 0.3f + 0.35f * (0.5f + 0.5f * std::sin(a * 1.3f));
                const float radius = 1.0f + 0.5f * static_cast<float>(k % 3);
                const float hue = static_cast<float>(k) / static_cast<float>(kLights);
                LightData& l = data[k];
                l.world[0] = wx;
                l.world[1] = wy;
                l.world[2] = wz;
                l.world[3] = radius;
                l.color[0] = 0.5f + 0.5f * std::sin(6.2832f * hue);
                l.color[1] = 0.5f + 0.5f * std::sin(6.2832f * hue + 2.094f);
                l.color[2] = 0.5f + 0.5f * std::sin(6.2832f * hue + 4.188f);
                l.color[3] = 1.0f;
                const float* m = view.m;
                l.view[0] = m[0] * wx + m[4] * wy + m[8] * wz + m[12];
                l.view[1] = m[1] * wx + m[5] * wy + m[9] * wz + m[13];
                l.view[2] = m[2] * wx + m[6] * wy + m[10] * wz + m[14];
                l.view[3] = radius;
            }
            const std::vector<Obj> objects = world_objects(s->stage, s->ground, 0.15f * t);
            for (UINT k = 0; k < objects.size(); ++k) {
                copy_matrix(s->models.at(k), 0, objects[k].model);
            }
            const float cull_constants[4] = {projection.m[0], 0, 0, 0};
            const float brk[4] = {kit.broken("forward_plus_cull") ? 1.0f : 0.0f, 0, 0, 0};
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                Canvas& canvas = s->stage.canvas;
                const float clear[4] = {0.03f, 0.03f, 0.05f, 1};
                canvas.depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                list->OMSetRenderTargets(0, nullptr, FALSE, &canvas.depth.view);
                list->ClearDepthStencilView(canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                canvas.set_viewport(list);
                canvas.binder.bind_graphics(list);
                list->SetPipelineState(s->depth_pso);
                for (const Obj& o : objects) {
                    list->SetGraphicsRoot32BitConstants(0, 16, (camera_vp * o.model).m, 0);
                    o.geometry->bind(list);
                    o.geometry->draw(list);
                }
                canvas.depth.to(list, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if (s->lists_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                    D3d12Tools::move(list, s->lists, s->lists_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    s->lists_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                }
                s->cull.bind(list);
                list->SetComputeRoot32BitConstants(0, 4, cull_constants, 16);
                list->SetComputeRoot32BitConstants(0, 4, brk, 28);
                list->Dispatch(kTiles, kTiles, 1);
                D3d12Tools::move(list, s->lists, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                s->lists_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                canvas.depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                canvas.color.bind_target(list, &canvas.depth);
                list->ClearRenderTargetView(canvas.color.view, clear, 0, nullptr);
                canvas.binder.bind_graphics(list);
                list->SetPipelineState(s->lit_pso);
                for (UINT k = 0; k < objects.size(); ++k) {
                    set_cbv_root(list, camera_vp * objects[k].model, objects[k].color);
                    list->SetGraphicsRootConstantBufferView(1, s->models.address(k));
                    objects[k].geometry->bind(list);
                    objects[k].geometry->draw(list);
                }
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

void register_ssao() {
    Cases::single(rr::CaseSpec("scene", "ssao_depth_hemisphere_blur_composite").size(kSize, kSize).tolerance(rr::Tolerance::blend(10).with_edge(1).with_outliers(0.005)).tag("scene"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        Surface albedo;
        Surface linear;
        Surface occlusion;
        Surface blurred;
        Geometry ground;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, kSize) || !ground.create(kit, Shapes::grid(40, 9.0f), true, error) ||
            !albedo.color(kit, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, error) || !linear.color(kit, kSize, kSize, DXGI_FORMAT_R32_FLOAT, error) ||
            !occlusion.color(kit, kSize, kSize, DXGI_FORMAT_R8_UNORM, error) || !blurred.color(kit, kSize, kSize, DXGI_FORMAT_R8_UNORM, error)) {
            return false;
        }
        Binder& binder = stage.canvas.binder;
        binder.srv_texture(0, linear.resource, DXGI_FORMAT_R32_FLOAT);
        binder.srv_texture(1, albedo.resource, DXGI_FORMAT_R8G8B8A8_UNORM);
        binder.srv_texture(2, occlusion.resource, DXGI_FORMAT_R8_UNORM);
        binder.srv_texture(3, blurred.resource, DXGI_FORMAT_R8_UNORM);
        const char* geometry_ps =
            "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float3 wpos : WPOS; };\n"
            "struct PsOut { float4 c : SV_Target0; float d : SV_Target1; };\n"
            "PsOut main(VsOut i) { PsOut o; float l = saturate(dot(normalize(i.nrm), normalize(float3(0.3, 0.9, 0.4)))) * 0.55 + 0.45; o.c = float4(p0.rgb * l, 1); o.d = 1.0 / i.pos.w; return o; }\n";
        Pso scene_builder(kit, binder.signature);
        scene_builder.vs(Hlsl12::with_root(kWorldVs)).ps(Hlsl12::with_root(geometry_ps)).targets({DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_FLOAT}).depth(DXGI_FORMAT_D32_FLOAT);
        ID3D12PipelineState* scene_pso = scene_builder.build(error);
        auto full = [&](const char* ps, DXGI_FORMAT format) -> ID3D12PipelineState* {
            Pso builder(kit, binder.signature);
            builder.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(ps)).layout({}).targets({format});
            return builder.build(error);
        };
        const char* ao_ps =
            "Texture2D<float> lin : register(t0);\n"
            "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
            "  int2 p = int2(pos.xy); float d = lin.Load(int3(p, 0)); float occ = 0; float rad = clamp(p0.x / d, 2.0, 40.0); if (p3.x > 0.5) rad *= 0.15;\n"
            "  [unroll] for (int k = 0; k < 16; ++k) { float a = k * 2.399963; float r = rad * (k + 1) / 16.0;\n"
            "    int2 q = clamp(p + int2(round(float2(cos(a), sin(a)) * r)), 0, 255); float sd = lin.Load(int3(q, 0)); float diff = d - sd;\n"
            "    occ += (diff > 0.04 && diff < 1.2) ? 1.0 : 0.0; }\n"
            "  return float4(saturate(1.0 - occ / 16.0 * 1.5), 0, 0, 1); }\n";
        const char* blur_ps =
            "Texture2D<float> ao : register(t2);\n"
            "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
            "  int2 p = int2(pos.xy); float sum = 0; for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) sum += ao.Load(int3(clamp(p + int2(x, y), 0, 255), 0));\n"
            "  return float4(sum / 25.0, 0, 0, 1); }\n";
        const char* composite_ps =
            "Texture2D<float4> albedo : register(t1); Texture2D<float> ao : register(t3);\n"
            "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
            "  int3 p = int3(pos.xy, 0); return float4(albedo.Load(p).rgb * ao.Load(p), 1); }\n";
        ID3D12PipelineState* ao_pso = full(ao_ps, DXGI_FORMAT_R8_UNORM);
        ID3D12PipelineState* blur_pso = full(blur_ps, DXGI_FORMAT_R8_UNORM);
        ID3D12PipelineState* composite_pso = full(composite_ps, DXGI_FORMAT_R8G8B8A8_UNORM);
        if (scene_pso == nullptr || ao_pso == nullptr || blur_pso == nullptr || composite_pso == nullptr) {
            return false;
        }
        HRESULT result = S_OK;
        UploadRing models;
        if (!models.create(kit, 256, 8, error)) {
            return false;
        }
        const float up[3] = {0, 1, 0};
        const float eye[3] = {0.0f, 3.4f, 7.0f};
        const float look[3] = {0.0f, -0.4f, -1.0f};
        const rr::Mat4 camera_vp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * SceneMath::look_at(eye, look, up);
        const std::vector<Obj> objects = world_objects(stage, ground, 0.0f);
        for (UINT k = 0; k < objects.size(); ++k) {
            copy_matrix(models.at(k), 0, objects[k].model);
        }
        const float ao_constants[4] = {200.0f, 0, 0, 0};
        const float brk[4] = {kit.broken("ssao_radius") ? 1.0f : 0.0f, 0, 0, 0};
        auto pass_full = [&](ID3D12GraphicsCommandList* list, Surface& target, ID3D12PipelineState* state) {
            target.bind_target(list);
            stage.canvas.set_viewport(list);
            list->SetPipelineState(state);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3, 1, 0, 0);
        };
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                Canvas& canvas = stage.canvas;
                const float clear[4] = {0.1f, 0.12f, 0.16f, 1};
                const float far_clear[4] = {40.0f, 0, 0, 0};
                const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {albedo.view, linear.view};
                canvas.depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                list->OMSetRenderTargets(2, rtvs, FALSE, &canvas.depth.view);
                list->ClearRenderTargetView(albedo.view, clear, 0, nullptr);
                list->ClearRenderTargetView(linear.view, far_clear, 0, nullptr);
                list->ClearDepthStencilView(canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                canvas.set_viewport(list);
                canvas.binder.bind_graphics(list);
                list->SetPipelineState(scene_pso);
                for (UINT k = 0; k < objects.size(); ++k) {
                    set_cbv_root(list, camera_vp * objects[k].model, objects[k].color);
                    list->SetGraphicsRootConstantBufferView(1, models.address(k));
                    objects[k].geometry->bind(list);
                    objects[k].geometry->draw(list);
                }
                albedo.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                linear.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                list->SetGraphicsRoot32BitConstants(0, 4, ao_constants, 16);
                list->SetGraphicsRoot32BitConstants(0, 4, brk, 28);
                pass_full(list, occlusion, ao_pso);
                occlusion.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                pass_full(list, blurred, blur_pso);
                blurred.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                pass_full(list, canvas.color, composite_pso);
            },
            error);
        (void)result;
        return ok && stage.finish(out, error);
    });
}

const struct Registrar {
    Registrar() {
        register_csm();
        register_forward_plus();
        register_ssao();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
