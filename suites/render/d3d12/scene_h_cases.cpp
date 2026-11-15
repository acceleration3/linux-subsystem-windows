#include "render/d3d12/scene_util.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

constexpr UINT kSize = 256;
constexpr UINT kEnvSize = 32;
constexpr UINT kEnvMips = 5;
constexpr UINT kIrradianceSize = 16;

const char* const kIblPrelude =
    "static const float PI = 3.14159265;\n"
    "float3 sky(float3 d) { float3 horizon = float3(1.0, 0.75, 0.5); float3 zenith = float3(0.15, 0.35, 0.9); float3 ground = float3(0.12, 0.1, 0.09);\n"
    "  float3 c = d.y > 0 ? lerp(horizon, zenith, pow(saturate(d.y), 0.5)) : lerp(horizon * 0.4, ground, saturate(-d.y * 3.0));\n"
    "  float sun = pow(saturate(dot(d, normalize(float3(0.6, 0.5, 0.4)))), 120.0); return c + float3(1.0, 0.9, 0.7) * sun * 12.0; }\n"
    "float3 face_dir(float face, float2 uv) { float2 s = uv * 2.0 - 1.0;\n"
    "  if (face < 0.5) return float3(1, -s.y, -s.x); if (face < 1.5) return float3(-1, -s.y, s.x); if (face < 2.5) return float3(s.x, 1, s.y);\n"
    "  if (face < 3.5) return float3(s.x, -1, -s.y); if (face < 4.5) return float3(s.x, -s.y, 1); return float3(-s.x, -s.y, -1); }\n"
    "float2 hammersley(uint i, uint n) { return float2((i + 0.5) / n, reversebits(i) * 2.3283064365386963e-10); }\n"
    "void basis(float3 N, out float3 T, out float3 B) { float3 up = abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0); T = normalize(cross(up, N)); B = cross(N, T); }\n";

const char* const kPrefilterPs =
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
    "  float3 N = normalize(face_dir(p0.x, uv)); float r = p0.y; if (r < 0.001) return float4(sky(N), 1);\n"
    "  float a = r * r; float3 T, B; basis(N, T, B); float3 sum = 0; float w = 0;\n"
    "  for (uint i = 0; i < 48; ++i) { float2 xi = hammersley(i, 48); float phi = 2.0 * PI * xi.x; float ct = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y)); float st = sqrt(1.0 - ct * ct);\n"
    "    float3 H = T * (st * cos(phi)) + B * (st * sin(phi)) + N * ct; float3 L = 2.0 * dot(N, H) * H - N; float ndl = saturate(dot(N, L)); if (ndl > 0) { sum += sky(L) * ndl; w += ndl; } }\n"
    "  return float4(sum / w, 1); }\n";

const char* const kIrradiancePs =
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
    "  float3 N = normalize(face_dir(p0.x, uv)); float3 T, B; basis(N, T, B); float3 sum = 0;\n"
    "  for (uint i = 0; i < 64; ++i) { float2 xi = hammersley(i, 64); float phi = 2.0 * PI * xi.x; float ct = sqrt(1.0 - xi.y); float st = sqrt(xi.y);\n"
    "    sum += sky(T * (st * cos(phi)) + B * (st * sin(phi)) + N * ct); }\n"
    "  return float4(sum / 64.0, 1); }\n";

const char* const kLutPs =
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
    "  float nv = max(uv.x, 0.02); float r = max(uv.y, 0.04); float a = r * r; float3 V = float3(sqrt(1.0 - nv * nv), 0, nv); float A = 0; float Bv = 0;\n"
    "  for (uint i = 0; i < 48; ++i) { float2 xi = hammersley(i, 48); float phi = 2.0 * PI * xi.x; float ct = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y)); float st = sqrt(1.0 - ct * ct);\n"
    "    float3 H = float3(st * cos(phi), st * sin(phi), ct); float3 L = 2.0 * dot(V, H) * H - V; float nl = saturate(L.z); float nh = saturate(H.z); float vh = saturate(dot(V, H));\n"
    "    if (nl > 0) { float k = a / 2.0; float gv = nv / (nv * (1.0 - k) + k); float gl = nl / (nl * (1.0 - k) + k); float vis = gv * gl * vh / (nh * nv); float fc = pow(1.0 - vh, 5.0); A += (1.0 - fc) * vis; Bv += fc * vis; } }\n"
    "  return float4(A / 48.0, Bv / 48.0, 0, 1); }\n";

const char* const kPbrVs =
    "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float3 wpos : WPOS; };\n"
    "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.wpos = i.pos * p2.w + p2.xyz; o.nrm = i.nrm; return o; }\n";

const char* const kPbrPs =
    "TextureCube<float4> spec : register(t0); TextureCube<float4> irr : register(t1); Texture2D<float2> lut : register(t2); SamplerState lin : register(s0);\n"
    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float3 wpos : WPOS; };\n"
    "float4 main(VsOut i) : SV_Target {\n"
    "  float3 N = normalize(i.nrm); float3 V = normalize(float3(0, 0, 5.2) - i.wpos); float r = p0.w; float metal = p1.x; float3 albedo = p0.rgb;\n"
    "  float3 F0 = lerp(0.04, albedo, metal); float ndv = saturate(dot(N, V)); float3 R = reflect(-V, N); float lod = p3.x > 0.5 ? 0.0 : r * 4.0;\n"
    "  float3 pre = spec.SampleLevel(lin, R, lod).rgb; float2 ab = lut.SampleLevel(lin, float2(ndv, r), 0);\n"
    "  float3 specular = pre * (F0 * ab.x + ab.y); float3 kd = (1.0 - F0) * (1.0 - metal); float3 diffuse = irr.SampleLevel(lin, N, 0).rgb * albedo * kd;\n"
    "  float3 c = diffuse + specular; c = c / (1.0 + c); return float4(pow(c, 1.0 / 2.2), 1); }\n";

const char* const kSkyPs =
    "TextureCube<float4> spec : register(t0); SamplerState lin : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
    "  float2 s = (uv * 2.0 - 1.0) * float2(1.0, -1.0) * 0.9; float3 d = normalize(float3(s.x, s.y + 0.25, -1.0));\n"
    "  float3 c = spec.SampleLevel(lin, d, 1).rgb; c = c / (1.0 + c); return float4(pow(c, 1.0 / 2.2), 1); }\n";

void register_pbr() {
    Cases::single(rr::CaseSpec("scene", "pbr_ibl_prefiltered_cube_mips_brdf_lut").size(kSize, kSize).tolerance(rr::Tolerance::blend(10).with_edge(1).with_outliers(0.01)).tag("scene"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, kSize)) {
            return false;
        }
        HRESULT result = S_OK;
        D3D12_CLEAR_VALUE clear_value{};
        clear_value.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ID3D12Resource* env = D3d12Tools::texture(kit.rig, D3d12Tools::texture_desc(kEnvSize, kEnvSize, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, kEnvMips, 6, 1),
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear_value, &result);
        ID3D12Resource* irradiance = D3d12Tools::texture(kit.rig, D3d12Tools::texture_desc(kIrradianceSize, kIrradianceSize, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1, 6, 1),
                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear_value, &result);
        Surface lut;
        if (env == nullptr || irradiance == nullptr || !lut.color(kit, 32, 32, DXGI_FORMAT_R16G16_FLOAT, error)) {
            return error.empty() ? fail_with(error, "cube creation: " + D3d12Tools::hr(result)) : false;
        }
        ID3D12DescriptorHeap* rtv_heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kEnvMips * 6 + 6, false, &result);
        if (rtv_heap == nullptr) {
            return fail_with(error, "RTV heap creation: " + D3d12Tools::hr(result));
        }
        const UINT rtv_increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT m = 0; m < kEnvMips; ++m) {
            for (UINT f = 0; f < 6; ++f) {
                D3D12_RENDER_TARGET_VIEW_DESC rtv{};
                rtv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv.Texture2DArray.MipSlice = m;
                rtv.Texture2DArray.FirstArraySlice = f;
                rtv.Texture2DArray.ArraySize = 1;
                kit.device()->CreateRenderTargetView(env, &rtv, D3d12Tools::cpu_at(rtv_heap, m * 6 + f, rtv_increment));
            }
        }
        for (UINT f = 0; f < 6; ++f) {
            D3D12_RENDER_TARGET_VIEW_DESC rtv{};
            rtv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtv.Texture2DArray.FirstArraySlice = f;
            rtv.Texture2DArray.ArraySize = 1;
            kit.device()->CreateRenderTargetView(irradiance, &rtv, D3d12Tools::cpu_at(rtv_heap, kEnvMips * 6 + f, rtv_increment));
        }
        Binder& binder = stage.canvas.binder;
        D3D12_SHADER_RESOURCE_VIEW_DESC cube{};
        cube.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        cube.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        cube.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        cube.TextureCube.MipLevels = kEnvMips;
        binder.srv(0, env, &cube);
        cube.TextureCube.MipLevels = 1;
        binder.srv(1, irradiance, &cube);
        binder.srv_texture(2, lut.resource, DXGI_FORMAT_R16G16_FLOAT);
        binder.sampler(0, Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
        auto full = [&](const char* body, DXGI_FORMAT format) -> ID3D12PipelineState* {
            Pso builder(kit, binder.signature);
            builder.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root((std::string(kIblPrelude) + body).c_str())).layout({}).targets({format});
            builder.desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
            return builder.build(error);
        };
        ID3D12PipelineState* prefilter = full(kPrefilterPs, DXGI_FORMAT_R16G16B16A16_FLOAT);
        ID3D12PipelineState* irradiance_pso = full(kIrradiancePs, DXGI_FORMAT_R16G16B16A16_FLOAT);
        ID3D12PipelineState* lut_pso = full(kLutPs, DXGI_FORMAT_R16G16_FLOAT);
        Pso sky_builder(kit, binder.signature);
        sky_builder.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(kSkyPs)).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
        sky_builder.desc.DepthStencilState.DepthEnable = FALSE;
        ID3D12PipelineState* sky_pso = sky_builder.build(error);
        Pso sphere_builder(kit, binder.signature);
        sphere_builder.vs(Hlsl12::with_root(kPbrVs)).ps(Hlsl12::with_root(kPbrPs)).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
        ID3D12PipelineState* sphere_pso = sphere_builder.build(error);
        if (prefilter == nullptr || irradiance_pso == nullptr || lut_pso == nullptr || sky_pso == nullptr || sphere_pso == nullptr) {
            return false;
        }
        const rr::Mat4 view_proj = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -5.2f);
        const float brk[4] = {kit.broken("pbr_ibl_lod") ? 1.0f : 0.0f, 0, 0, 0};
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                Canvas& canvas = stage.canvas;
                D3d12Tools::move(list, env, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                D3d12Tools::move(list, irradiance, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                canvas.binder.bind_graphics(list);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                auto face_pass = [&](ID3D12PipelineState* state, UINT descriptor, UINT size, float face, float roughness) {
                    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = D3d12Tools::cpu_at(rtv_heap, descriptor, rtv_increment);
                    const D3D12_VIEWPORT vp = viewport_of(size, size);
                    const D3D12_RECT scissor = rect_of(size, size);
                    const float constants[4] = {face, roughness, 0, 0};
                    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                    list->RSSetViewports(1, &vp);
                    list->RSSetScissorRects(1, &scissor);
                    list->SetGraphicsRoot32BitConstants(0, 4, constants, 16);
                    list->SetPipelineState(state);
                    list->DrawInstanced(3, 1, 0, 0);
                };
                for (UINT m = 0; m < kEnvMips; ++m) {
                    for (UINT f = 0; f < 6; ++f) {
                        face_pass(prefilter, m * 6 + f, kEnvSize >> m, static_cast<float>(f), static_cast<float>(m) / static_cast<float>(kEnvMips - 1));
                    }
                }
                for (UINT f = 0; f < 6; ++f) {
                    face_pass(irradiance_pso, kEnvMips * 6 + f, kIrradianceSize, static_cast<float>(f), 0.0f);
                }
                lut.bind_target(list);
                const D3D12_VIEWPORT lut_vp = viewport_of(32, 32);
                const D3D12_RECT lut_scissor = rect_of(32, 32);
                list->RSSetViewports(1, &lut_vp);
                list->RSSetScissorRects(1, &lut_scissor);
                list->SetPipelineState(lut_pso);
                list->DrawInstanced(3, 1, 0, 0);
                D3d12Tools::move(list, env, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                D3d12Tools::move(list, irradiance, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                lut.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                const float clear[4] = {0, 0, 0, 1};
                canvas.begin(list, clear);
                list->SetPipelineState(sky_pso);
                list->DrawInstanced(3, 1, 0, 0);
                list->SetPipelineState(sphere_pso);
                const float roughness[5] = {0.05f, 0.25f, 0.5f, 0.75f, 1.0f};
                for (UINT row = 0; row < 2; ++row) {
                    for (UINT col = 0; col < 5; ++col) {
                        const float tx = (static_cast<float>(col) - 2.0f) * 0.95f;
                        const float ty = row == 0 ? 0.7f : -0.7f;
                        const float color[4] = {row == 0 ? 1.0f : 0.8f, row == 0 ? 0.78f : 0.12f, row == 0 ? 0.34f : 0.1f, roughness[col]};
                        const float metal[4] = {row == 0 ? 1.0f : 0.0f, 0, 0, 0};
                        const float placement[4] = {tx, ty, 0, 0.55f};
                        set_cbv_root(list, view_proj * rr::Mat4::translate(tx, ty, 0) * rr::Mat4::scale(0.55f, 0.55f, 0.55f), color, metal);
                        list->SetGraphicsRoot32BitConstants(0, 4, placement, 24);
                        list->SetGraphicsRoot32BitConstants(0, 4, brk, 28);
                        stage.sphere.bind(list);
                        stage.sphere.draw(list);
                    }
                }
            },
            error);
        return ok && stage.finish(out, error);
    });
}

struct BlurState {
    Stage stage;
    Surface albedo;
    Surface velocity;
    Surface history;
    Geometry ground;
    ID3D12PipelineState* scene_pso = nullptr;
    ID3D12PipelineState* blur_pso = nullptr;
    UploadRing previous;
    rr::Mat4 previous_mvp[4];
};

struct Placement {
    const Geometry* geometry;
    rr::Mat4 model;
    float color[4];
};

std::vector<Placement> blur_objects(BlurState& s, unsigned frame) {
    const float t = static_cast<float>(frame);
    return {{&s.ground, rr::Mat4::translate(0, -0.9f, -1.5f), {0.5f, 0.55f, 0.5f, 1}},
            {&s.stage.torus, rr::Mat4::translate(-0.9f, 0.1f, 0.0f) * rr::Mat4::rotate_z(0.55f * t) * rr::Mat4::rotate_x(0.8f), {0.9f, 0.6f, 0.2f, 1}},
            {&s.stage.cube, rr::Mat4::translate(-1.4f + 0.36f * t, 0.0f, -1.5f) * rr::Mat4::rotate_y(0.5f) * rr::Mat4::scale(0.5f, 0.5f, 0.5f), {0.3f, 0.8f, 0.5f, 1}},
            {&s.stage.sphere, rr::Mat4::translate(1.1f, 0.2f, 0.3f), {0.4f, 0.5f, 0.95f, 1}}};
}

void register_motion_blur() {
    Cases::frames(rr::CaseSpec("scene", "motion_blur_velocity_buffer_history_blend_6_frames").size(kSize, kSize).frames(6, {0, 2, 5}).tolerance(rr::Tolerance::blend(10).with_edge(2).with_outliers(0.006)).tag("scene"),
                  [](Cases::Steps& steps) {
        auto s = std::make_shared<BlurState>();
        steps.on_setup = [s](Kit& kit, std::string& error) {
            if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, kSize) || !s->ground.create(kit, Shapes::grid(30, 5.0f), true, error) ||
                !s->albedo.color(kit, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, error) || !s->velocity.color(kit, kSize, kSize, DXGI_FORMAT_R16G16_FLOAT, error) ||
                !s->history.color(kit, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, error) || !s->previous.create(kit, 256, 8, error)) {
                return false;
            }
            Binder& binder = s->stage.canvas.binder;
            binder.srv_texture(0, s->albedo.resource, DXGI_FORMAT_R8G8B8A8_UNORM);
            binder.srv_texture(1, s->velocity.resource, DXGI_FORMAT_R16G16_FLOAT);
            binder.srv_texture(2, s->history.resource, DXGI_FORMAT_R8G8B8A8_UNORM);
            binder.sampler(0, Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
            const char* vs =
                "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 cur : CUR; float4 prv : PRV; };\n"
                "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.cur = o.pos; o.prv = mul(float4(i.pos, 1), float4x4(u[0], u[1], u[2], u[3])); o.nrm = i.nrm; return o; }\n";
            const char* ps =
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 cur : CUR; float4 prv : PRV; };\n"
                "struct PsOut { float4 c : SV_Target0; float2 v : SV_Target1; };\n"
                "PsOut main(VsOut i) { PsOut o; float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.6 + 0.4;\n"
                "  float2 cp = i.cur.xy / i.cur.w; float2 pp = i.prv.xy / i.prv.w; o.c = float4(p0.rgb * l, 1); o.v = (cp - pp) * float2(0.5, -0.5); return o; }\n";
            Pso scene(kit, binder.signature);
            scene.vs(Hlsl12::with_root(vs)).ps(Hlsl12::with_root(ps)).targets({DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16_FLOAT}).depth(DXGI_FORMAT_D32_FLOAT);
            s->scene_pso = scene.build(error);
            const char* blur =
                "Texture2D<float4> color : register(t0); Texture2D<float2> vel : register(t1); Texture2D<float4> hist : register(t2); SamplerState lin : register(s0);\n"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
                "  int3 p = int3(pos.xy, 0); float2 v = vel.Load(p) * p0.x; float3 sum = 0;\n"
                "  for (int k = 0; k < 10; ++k) { float t = (k + 0.5) / 10.0 - 0.5; sum += color.SampleLevel(lin, uv + v * t, 0).rgb; }\n"
                "  return float4(lerp(sum / 10.0, hist.Load(p).rgb, p0.y), 1); }\n";
            Pso blur_builder(kit, binder.signature);
            blur_builder.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(blur)).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
            blur_builder.desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            s->blur_pso = blur_builder.build(error);
            return s->scene_pso != nullptr && s->blur_pso != nullptr;
        };
        steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
            const float up[3] = {0, 1, 0};
            auto camera = [&](unsigned f) {
                const float eye[3] = {-0.25f * static_cast<float>(f), 2.2f, 6.0f};
                const float look[3] = {-0.25f * static_cast<float>(f) + 0.0f, 0.0f, 0.0f};
                return rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * SceneMath::look_at(eye, look, up);
            };
            const rr::Mat4 current_vp = camera(frame);
            const rr::Mat4 previous_vp = camera(frame == 0 ? 0 : frame - 1);
            const std::vector<Placement> now = blur_objects(*s, frame);
            const std::vector<Placement> before = blur_objects(*s, frame == 0 ? 0 : frame - 1);
            for (UINT k = 0; k < now.size(); ++k) {
                const rr::Mat4 previous = (kit.broken("motion_blur_velocity") ? current_vp * now[k].model : previous_vp * before[k].model);
                std::memcpy(s->previous.at(k), previous.m, 64);
            }
            const float constants[4] = {0.6f, frame == 0 ? 0.0f : 0.3f, 0, 0};
            return kit.submit([&](ID3D12GraphicsCommandList* list) {
                Canvas& canvas = s->stage.canvas;
                const float clear[4] = {0.12f, 0.15f, 0.22f, 1};
                const float zero[4] = {0, 0, 0, 0};
                if (frame == 0) {
                    list->ClearRenderTargetView(s->history.view, zero, 0, nullptr);
                    s->history.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
                s->albedo.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
                s->velocity.to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
                canvas.depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {s->albedo.view, s->velocity.view};
                list->OMSetRenderTargets(2, rtvs, FALSE, &canvas.depth.view);
                list->ClearRenderTargetView(s->albedo.view, clear, 0, nullptr);
                list->ClearRenderTargetView(s->velocity.view, zero, 0, nullptr);
                list->ClearDepthStencilView(canvas.depth.view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                canvas.set_viewport(list);
                canvas.binder.bind_graphics(list);
                list->SetPipelineState(s->scene_pso);
                for (UINT k = 0; k < now.size(); ++k) {
                    set_cbv_root(list, current_vp * now[k].model, now[k].color);
                    list->SetGraphicsRootConstantBufferView(1, s->previous.address(k));
                    now[k].geometry->bind(list);
                    now[k].geometry->draw(list);
                }
                s->albedo.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                s->velocity.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                s->history.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                canvas.color.bind_target(list, nullptr);
                list->SetGraphicsRoot32BitConstants(0, 4, constants, 16);
                list->SetPipelineState(s->blur_pso);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->DrawInstanced(3, 1, 0, 0);
                canvas.color.to(list, D3D12_RESOURCE_STATE_COPY_SOURCE);
                s->history.to(list, D3D12_RESOURCE_STATE_COPY_DEST);
                list->CopyResource(s->history.resource, canvas.color.resource);
                s->history.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }, error);
        };
        steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
    });
}

const struct Registrar {
    Registrar() {
        register_pbr();
        register_motion_blur();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
