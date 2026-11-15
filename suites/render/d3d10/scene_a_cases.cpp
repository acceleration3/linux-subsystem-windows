#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

const char* kWorldVs =
    "cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\n"
    "cbuffer Light : register(b1) { float4x4 lvp; float4 ldir; float4 lpos; };\n"
    "struct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; float4 sh : TEXCOORD2; };\n"
    "VO main(VI i) { VO o; float4 w = mul(model, float4(i.p, 1)); o.p = mul(vp, w); o.n = mul((float3x3)model, i.n); o.uv = i.uv; o.w = w.xyz; o.sh = mul(lvp, w); return o; }\n";

struct WorldPass {
    explicit WorldPass(Rig& r) : world(r) {
        vs = r.vs(kWorldVs);
        layout = r.layout(mesh_elements());
        obj = r.cbuf(sizeof(ObjectConstants));
        light = r.cbuf(sizeof(LightConstants));
    }

    void bind(Rig& r) {
        r.d->IASetInputLayout(layout.get());
        r.d->VSSetShader(vs.get());
        r.vs_cb(0, obj.get());
        r.vs_cb(1, light.get());
        r.ps_cb(0, obj.get());
        r.ps_cb(1, light.get());
        r.gs_cb(0, obj.get());
    }

    void set_light(Rig& r, const rr::Mat4& lvp, V3 dir, V3 pos) {
        LightConstants lc{};
        std::memcpy(lc.lvp, lvp.m, 64);
        lc.dir[0] = dir.x; lc.dir[1] = dir.y; lc.dir[2] = dir.z;
        lc.pos[0] = pos.x; lc.pos[1] = pos.y; lc.pos[2] = pos.z; lc.pos[3] = 1;
        r.d->UpdateSubresource(light.get(), 0, nullptr, &lc, 0, 0);
    }

    void draw(Rig& r, const rr::Mat4& vp, float t, const float extra[4] = nullptr) {
        for (const WorldObject& o : world.objects(t)) {
            ObjectConstants c{};
            std::memcpy(c.vp, vp.m, 64);
            std::memcpy(c.model, o.model.m, 64);
            std::memcpy(c.color, o.color, 16);
            if (extra != nullptr) std::memcpy(c.extra, extra, 16);
            r.d->UpdateSubresource(obj.get(), 0, nullptr, &c, 0, 0);
            draw_mesh(r, *o.mesh);
        }
    }

    World world;
    Com<ID3D10VertexShader> vs;
    Com<ID3D10InputLayout> layout;
    Com<ID3D10Buffer> obj, light;
};

const char* kShadowPs =
    "cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\n"
    "cbuffer Light : register(b1) { float4x4 lvp; float4 ldir; float4 lpos; };\n"
    "Texture2D shadow : register(t0); SamplerComparisonState cmp : register(s0);\n"
    "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1, float4 sh : TEXCOORD2) : SV_Target {\n"
    "  float3 s = sh.xyz / sh.w; float2 st = float2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5); float lit = shadow.SampleCmpLevelZero(cmp, st, s.z - 0.004);\n"
    "  float d = saturate(dot(normalize(n), normalize(ldir.xyz))); return float4(color.rgb * (0.22 + 0.78 * d * lit), 1); }\n";

struct ShadowState {
    std::unique_ptr<WorldPass> pass;
    Com<ID3D10Texture2D> map;
    Com<ID3D10DepthStencilView> map_dsv;
    Com<ID3D10ShaderResourceView> map_srv;
    Com<ID3D10SamplerState> cmp;
    Com<ID3D10PixelShader> lit_ps;
    Com<ID3D10DepthStencilState> depth_on;
    Com<ID3D10RasterizerState> bias_raster;
    rr::Mat4 lvp;
    V3 dir{0, 1, 0};
};

const Registrar kShadow{[] {
    MultiBody<ShadowState> body;
    body.setup = [](Rig& r, ShadowState& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        s.pass = std::make_unique<WorldPass>(r);
        s.map = r.tex2d(512, 512, DXGI_FORMAT_R32_TYPELESS, D3D10_BIND_DEPTH_STENCIL | D3D10_BIND_SHADER_RESOURCE);
        D3D10_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.ViewDimension = D3D10_DSV_DIMENSION_TEXTURE2D;
        D3D10_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        s.map_dsv = r.dsv(s.map.get(), &dd);
        s.map_srv = r.srv(s.map.get(), &sd);
        auto smp = Rig::sampler_desc(D3D10_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D10_TEXTURE_ADDRESS_BORDER);
        smp.ComparisonFunc = D3D10_COMPARISON_LESS_EQUAL;
        smp.BorderColor[0] = smp.BorderColor[1] = smp.BorderColor[2] = smp.BorderColor[3] = 1.0f;
        s.cmp = r.sampler(smp);
        s.lit_ps = r.ps(kShadowPs);
        auto dp = Rig::depth_desc();
        dp.DepthEnable = TRUE;
        s.depth_on = r.depth(dp);
        r.no_cull();
    };
    body.frame = [](Rig& r, ShadowState& s, unsigned f) {
        const float t = 0.6f * static_cast<float>(f);
        const bool refresh = (f % 2) == 0;
        s.pass->bind(r);
        r.d->OMSetDepthStencilState(s.depth_on.get(), 0);
        if (refresh) {
            const float lt = t;
            s.dir = norm3({std::cos(lt * 0.7f) * 0.8f, 1.2f, std::sin(lt * 0.7f) * 0.8f});
            const V3 eye = {s.dir.x * 7, s.dir.y * 7, s.dir.z * 7};
            s.lvp = ortho(9, 9, 1.0f, 16.0f) * look_at(eye, {0, 0, 0}, {0, 1, 0});
            s.pass->set_light(r, s.lvp, s.dir, eye);
            ID3D10ShaderResourceView* none = nullptr;
            r.d->PSSetShaderResources(0, 1, &none);
            r.d->OMSetRenderTargets(0, nullptr, s.map_dsv.get());
            r.d->ClearDepthStencilView(s.map_dsv.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
            r.viewport(512, 512);
            r.d->PSSetShader(nullptr);
            s.pass->draw(r, s.lvp, t);
        } else if (r.mutate("scene_shadow")) {
            r.d->ClearDepthStencilView(s.map_dsv.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
        }
        r.bind_rt(r.main, &r.depth_main);
        r.clear(r.main, 0.4f, 0.55f, 0.75f, 1);
        r.clear_depth(r.depth_main);
        r.viewport(256, 256);
        r.d->PSSetShader(s.lit_ps.get());
        r.ps_srv(0, s.map_srv.get());
        r.ps_smp(0, s.cmp.get());
        s.pass->draw(r, camera(0.5f + 0.08f * static_cast<float>(f), 0.6f, 7.0f, 1.0f, 0.9f), t);
    };
    body.capture = [](Rig& r, ShadowState& s, unsigned, rr::CaptureSet& o) {
        r.cap_main(o);
        Depth d;
        d.texture = Com<ID3D10Texture2D>(s.map.get());
        s.map->AddRef();
        d.format = DXGI_FORMAT_D32_FLOAT;
        r.cap_depth(o, "shadow_map", d);
    };
    multi<ShadowState>(spec("scene", "shadow_map_cached_for_two_frames_light_orbit").size(256, 256).frames(6, {1, 3, 5}).tolerance(scene_tol(24)).tolerance_for("shadow_map", rr::Tolerance::floats(2e-3f, 2e-3f).with_outliers(1e-4)), body);
}};

struct CubeShadowState {
    std::unique_ptr<WorldPass> pass;
    Com<ID3D10Texture2D> cube, depth;
    Com<ID3D10RenderTargetView> rtv;
    Com<ID3D10DepthStencilView> dsv;
    Com<ID3D10ShaderResourceView> srv;
    Com<ID3D10GeometryShader> gs;
    Com<ID3D10PixelShader> dist_ps, lit_ps;
    Com<ID3D10Buffer> faces;
    Com<ID3D10SamplerState> point;
    Com<ID3D10DepthStencilState> depth_on;
};

const Registrar kCubeShadow{[] {
    MultiBody<CubeShadowState> body;
    body.setup = [](Rig& r, CubeShadowState& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        s.pass = std::make_unique<WorldPass>(r);
        s.cube = r.tex2d(128, 128, DXGI_FORMAT_R32_FLOAT, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, 1, 6, {}, {}, D3D10_RESOURCE_MISC_TEXTURECUBE);
        s.depth = r.tex2d(128, 128, DXGI_FORMAT_D32_FLOAT, D3D10_BIND_DEPTH_STENCIL, 1, 6);
        D3D10_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = DXGI_FORMAT_R32_FLOAT;
        rd.ViewDimension = D3D10_RTV_DIMENSION_TEXTURE2DARRAY;
        rd.Texture2DArray.ArraySize = 6;
        s.rtv = r.rtv(s.cube.get(), &rd);
        D3D10_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.ViewDimension = D3D10_DSV_DIMENSION_TEXTURE2DARRAY;
        dd.Texture2DArray.ArraySize = 6;
        s.dsv = r.dsv(s.depth.get(), &dd);
        D3D10_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension = D3D10_SRV_DIMENSION_TEXTURECUBE;
        sd.TextureCube.MipLevels = 1;
        s.srv = r.srv(s.cube.get(), &sd);
        s.gs = r.gs("cbuffer Faces : register(b2) { float4x4 face[6]; };\n"
                    "struct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; float4 sh : TEXCOORD2; };\n"
                    "struct GO { float4 p : SV_Position; float3 w : TEXCOORD1; uint rt : SV_RenderTargetArrayIndex; };\n"
                    "[maxvertexcount(18)] void main(triangle VO v[3], inout TriangleStream<GO> s) { GO o; [unroll] for (uint f = 0; f < 6; ++f) { [unroll] for (int i = 0; i < 3; ++i) { o.p = mul(face[f], float4(v[i].w, 1)); o.w = v[i].w; o.rt = f; s.Append(o); } s.RestartStrip(); } }\n");
        s.dist_ps = r.ps("cbuffer Light : register(b1) { float4x4 lvp; float4 ldir; float4 lpos; };\nfloat4 main(float4 p : SV_Position, float3 w : TEXCOORD1) : SV_Target { return length(w - lpos.xyz); }\n");
        s.lit_ps = r.ps("cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\n"
                        "cbuffer Light : register(b1) { float4x4 lvp; float4 ldir; float4 lpos; };\n"
                        "TextureCube dist : register(t0); SamplerState smp : register(s0);\n"
                        "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1, float4 sh : TEXCOORD2) : SV_Target {\n"
                        "  float3 l = lpos.xyz - w; float d = length(l); float stored = dist.SampleLevel(smp, -l, 0).r; float lit = d - 0.08 <= stored ? 1 : 0;\n"
                        "  float a = saturate(dot(normalize(n), l / d)) / (1 + 0.04 * d * d); return float4(color.rgb * (0.15 + 1.4 * a * lit), 1); }\n");
        s.point = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        s.faces = r.cbuf(6 * 64);
        auto dp = Rig::depth_desc();
        dp.DepthEnable = TRUE;
        s.depth_on = r.depth(dp);
        r.no_cull();
    };
    body.frame = [](Rig& r, CubeShadowState& s, unsigned f) {
        const float t = 0.9f * static_cast<float>(f & ~1u);
        const V3 lp = {1.6f * std::cos(t), 1.8f, 1.6f * std::sin(t)};
        const bool refresh = (f % 2) == 0;
        const V3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const V3 ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
        rr::Mat4 proj = rr::Mat4::perspective_d3d(1.5707963f, 1.0f, 0.1f, 30.0f);
        proj.m[0] = -proj.m[0];
        float mats[6][16];
        for (int i = 0; i < 6; ++i) {
            const rr::Mat4 m = proj * look_at(lp, {lp.x + dirs[i].x, lp.y + dirs[i].y, lp.z + dirs[i].z}, ups[i]);
            std::memcpy(mats[i], m.m, 64);
        }
        r.d->UpdateSubresource(s.faces.get(), 0, nullptr, mats, 0, 0);
        s.pass->set_light(r, rr::Mat4::identity(), {0, 1, 0}, lp);
        s.pass->bind(r);
        r.gs_cb(2, s.faces.get());
        r.gs_cb(1, s.pass->light.get());
        if (refresh) {
        ID3D10ShaderResourceView* none = nullptr;
        r.d->PSSetShaderResources(0, 1, &none);
        ID3D10RenderTargetView* rt = s.rtv.get();
        r.d->OMSetRenderTargets(1, &rt, s.dsv.get());
        const float far_dist[4] = {100, 100, 100, 100};
        r.d->ClearRenderTargetView(rt, far_dist);
        r.d->ClearDepthStencilView(s.dsv.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
        r.viewport(128, 128);
        r.d->OMSetDepthStencilState(s.depth_on.get(), 0);
        r.d->GSSetShader(s.gs.get());
        r.d->PSSetShader(s.dist_ps.get());
        s.pass->draw(r, rr::Mat4::identity(), 0.5f);
        r.d->GSSetShader(nullptr);
        }
        r.bind_rt(r.main, &r.depth_main);
        r.clear(r.main, 0.02f, 0.02f, 0.04f, 1);
        r.clear_depth(r.depth_main);
        r.viewport(256, 256);
        r.d->PSSetShader(s.lit_ps.get());
        r.ps_srv(0, s.srv.get());
        r.ps_smp(0, s.point.get());
        s.pass->draw(r, camera(0.4f + 0.1f * static_cast<float>(f), 0.55f, 7.0f, 1.0f, 0.9f), 0.5f);
    };
    multi<CubeShadowState>(spec("scene", "point_light_cube_shadow_cached_for_two_frames").size(256, 256).frames(4, {1, 2, 3}).tolerance(scene_tol(30)), body);
}};

struct DeferredState {
    std::unique_ptr<WorldPass> pass;
    Target albedo, normal, position;
    Com<ID3D10PixelShader> gbuf_ps, light_ps, ambient_ps;
    Com<ID3D10BlendState> additive, opaque;
    Com<ID3D10DepthStencilState> depth_on, depth_off;
    Com<ID3D10Buffer> light_cb, blend_cb;
    Target scene_rt, history;
    Com<ID3D10PixelShader> temporal_ps;
    Com<ID3D10SamplerState> point;
};

const Registrar kDeferred{[] {
    MultiBody<DeferredState> body;
    body.setup = [](Rig& r, DeferredState& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        s.pass = std::make_unique<WorldPass>(r);
        r.target(s.albedo, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM);
        r.target(s.normal, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT);
        r.target(s.position, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT);
        s.gbuf_ps = r.ps("cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\n"
                         "struct PO { float4 a : SV_Target0; float4 n : SV_Target1; float4 w : SV_Target2; };\n"
                         "PO main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1, float4 sh : TEXCOORD2) { PO o; o.a = float4(color.rgb * (0.8 + 0.2 * sin(w.x * 6) * sin(w.z * 6)), 1); o.n = float4(normalize(n), 0); o.w = float4(w, 1); return o; }\n");
        s.light_ps = r.ps("cbuffer Light : register(b0) { float4 pos; float4 col; };\n"
                          "Texture2D alb : register(t0); Texture2D nrm : register(t1); Texture2D wp : register(t2);\n"
                          "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int3 q = int3(p.xy, 0); float4 w = wp.Load(q); if (w.a < 0.5) discard; float3 n = nrm.Load(q).xyz; float3 l = pos.xyz - w.xyz; float d = length(l);\n"
                          "  float att = saturate(1 - d / pos.w); return float4(alb.Load(q).rgb * col.rgb * saturate(dot(n, l / d)) * att * att, 0); }\n");
        s.ambient_ps = r.ps("Texture2D alb : register(t0);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return float4(alb.Load(int3(p.xy, 0)).rgb * 0.08 + float3(0.02, 0.03, 0.05) * (1 - alb.Load(int3(p.xy, 0)).a), 1); }\n");
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = bd.DestBlend = D3D10_BLEND_ONE;
        bd.SrcBlendAlpha = bd.DestBlendAlpha = D3D10_BLEND_ONE;
        s.additive = r.blend(bd);
        s.opaque = r.blend(Rig::blend_desc());
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        s.depth_on = r.depth(dd);
        s.depth_off = r.depth(Rig::depth_desc());
        s.light_cb = r.cbuf(32);
        s.blend_cb = r.cbuf(16);
        r.target(s.scene_rt, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM);
        r.target(s.history, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM);
        s.temporal_ps = r.ps("cbuffer W : register(b0) { float4 w; };\nTexture2D now : register(t0); Texture2D old : register(t1);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int3 q = int3(p.xy, 0); return lerp(now.Load(q), old.Load(q), w.x); }\n");
        r.no_cull();
    };
    body.frame = [](Rig& r, DeferredState& s, unsigned f) {
        const float t = 0.7f * static_cast<float>(f);
        ID3D10RenderTargetView* views[3] = {s.albedo.view.get(), s.normal.view.get(), s.position.view.get()};
        r.d->OMSetRenderTargets(3, views, r.depth_main.view.get());
        const float zero[4] = {0, 0, 0, 0};
        for (auto* v : views) r.d->ClearRenderTargetView(v, zero);
        r.clear_depth(r.depth_main);
        r.viewport(256, 256);
        s.pass->set_light(r, rr::Mat4::identity(), {0, 1, 0}, {0, 0, 0});
        s.pass->bind(r);
        r.d->OMSetDepthStencilState(s.depth_on.get(), 0);
        r.d->PSSetShader(s.gbuf_ps.get());
        s.pass->draw(r, camera(0.5f, 0.55f, 7.0f, 1.0f, 0.9f), 0.4f);
        r.bind_rt(s.scene_rt);
        r.clear(s.scene_rt, 0.0f, 0.0f, 0.0f, 1);
        r.d->OMSetDepthStencilState(s.depth_off.get(), 0);
        ID3D10ShaderResourceView* srvs[3];
        Com<ID3D10ShaderResourceView> keep[3];
        keep[0] = r.srv(s.albedo.texture.get());
        keep[1] = r.srv(s.normal.texture.get());
        keep[2] = r.srv(s.position.texture.get());
        for (int i = 0; i < 3; ++i) srvs[i] = keep[i].get();
        r.d->PSSetShaderResources(0, 3, srvs);
        const float f4[4] = {1, 1, 1, 1};
        r.d->OMSetBlendState(s.opaque.get(), f4, 0xffffffffu);
        r.fullscreen(s.ambient_ps.get());
        r.d->OMSetBlendState(s.additive.get(), f4, 0xffffffffu);
        for (int i = 0; i < 6; ++i) {
            const float a = t + 1.047f * static_cast<float>(i);
            const float light[8] = {2.6f * std::cos(a), 0.2f + 0.6f * std::sin(a * 1.7f), 2.6f * std::sin(a), 4.5f, palette(static_cast<unsigned>(i))[0] * 1.4f, palette(static_cast<unsigned>(i))[1] * 1.4f, palette(static_cast<unsigned>(i))[2] * 1.4f, 1};
            r.d->UpdateSubresource(s.light_cb.get(), 0, nullptr, light, 0, 0);
            r.ps_cb(0, s.light_cb.get());
            r.fullscreen(s.light_ps.get());
        }
        r.d->OMSetBlendState(s.opaque.get(), f4, 0xffffffffu);
        ID3D10ShaderResourceView* none[3] = {};
        r.d->PSSetShaderResources(0, 3, none);
        Com<ID3D10ShaderResourceView> now = r.srv(s.scene_rt.texture.get());
        Com<ID3D10ShaderResourceView> old = r.srv(s.history.texture.get());
        const float weight[4] = {(f == 0 || r.mutate("scene_history")) ? 0.0f : 0.5f, 0, 0, 0};
        r.d->UpdateSubresource(s.blend_cb.get(), 0, nullptr, weight, 0, 0);
        r.bind_rt(r.main);
        r.ps_cb(0, s.blend_cb.get());
        r.ps_srv(0, now.get());
        r.ps_srv(1, old.get());
        r.fullscreen(s.temporal_ps.get());
        r.d->PSSetShaderResources(0, 2, none);
        r.d->CopyResource(s.history.texture.get(), r.main.texture.get());
    };
    body.capture = [](Rig& r, DeferredState& s, unsigned, rr::CaptureSet& o) {
        r.cap_main(o);
        r.cap(o, "normal", s.normal);
    };
    multi<DeferredState>(spec("scene", "deferred_gbuffer_lights_with_temporal_history").size(256, 256).frames(4, {0, 2, 3}).tolerance(scene_tol(12)).tolerance_for("normal", rr::Tolerance::floats(0.03f, 0.03f).with_edge(1)), body);
}};

struct HdrState {
    std::unique_ptr<WorldPass> pass;
    Target hdr, half_a, half_b, bloom_hist;
    Com<ID3D10ShaderResourceView> hist_view;
    Com<ID3D10PixelShader> hist_ps;
    Com<ID3D10PixelShader> scene_ps, bright_ps, blur_ps, combine_ps;
    Com<ID3D10Buffer> params;
    Com<ID3D10SamplerState> linear_clamp;
    Com<ID3D10DepthStencilState> depth_on;
};

const Registrar kHdr{[] {
    MultiBody<HdrState> body;
    body.setup = [](Rig& r, HdrState& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        s.pass = std::make_unique<WorldPass>(r);
        r.target(s.hdr, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT);
        r.target(s.half_a, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT);
        r.target(s.half_b, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT);
        r.target(s.bloom_hist, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT);
        s.hist_view = r.srv(s.bloom_hist.texture.get());
        r.clear(s.bloom_hist, 0, 0, 0, 1);
        s.hist_ps = r.ps("Texture2D now : register(t0); Texture2D old : register(t1);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int3 q = int3(p.xy, 0); return float4(max(now.Load(q).rgb, old.Load(q).rgb * 0.7), 1); }\n");
        s.scene_ps = r.ps("cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\ncbuffer Light : register(b1) { float4x4 lvp; float4 ldir; float4 lpos; };\n"
                          "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1, float4 sh : TEXCOORD2) : SV_Target {\n"
                          "  float d = saturate(dot(normalize(n), normalize(ldir.xyz))); float spec = pow(saturate(dot(normalize(n), normalize(ldir.xyz + float3(0, 0, 1)))), 24) * 6; return float4(color.rgb * (0.2 + 1.8 * d) + spec, 1); }\n");
        s.bright_ps = r.ps("Texture2D t : register(t0); SamplerState s : register(s0);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float3 c = t.Sample(s, uv).rgb; float l = dot(c, float3(0.3, 0.59, 0.11)); return float4(c * saturate(l - 1.2), 1); }\n");
        s.blur_ps = r.ps("cbuffer P : register(b0) { float4 dir; float4 exposure; };\nTexture2D t : register(t0); SamplerState s : register(s0);\n"
                         "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float w[5] = {0.227, 0.194, 0.121, 0.054, 0.016}; float3 c = t.Sample(s, uv).rgb * w[0];\n"
                         "  [unroll] for (int i = 1; i < 5; ++i) { c += t.Sample(s, uv + dir.xy * i * 1.5).rgb * w[i]; c += t.Sample(s, uv - dir.xy * i * 1.5).rgb * w[i]; } return float4(c, 1); }\n");
        s.combine_ps = r.ps("cbuffer P : register(b0) { float4 dir; float4 exposure; };\nTexture2D scene : register(t0); Texture2D bloom : register(t1); SamplerState s : register(s0);\n"
                            "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float3 c = (scene.Sample(s, uv).rgb + bloom.Sample(s, uv).rgb * 1.5) * exposure.x; c = c / (1 + c); return float4(pow(c, 1 / 2.2), 1); }\n");
        s.params = r.cbuf(32);
        s.linear_clamp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR));
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        s.depth_on = r.depth(dd);
        r.no_cull();
    };
    body.frame = [](Rig& r, HdrState& s, unsigned f) {
        r.bind_rt(s.hdr, &r.depth_main);
        r.clear(s.hdr, 0.02f, 0.03f, 0.06f, 1);
        r.clear_depth(r.depth_main);
        r.viewport(256, 256);
        s.pass->bind(r);
        s.pass->set_light(r, rr::Mat4::identity(), norm3({0.5f, 1.0f, 0.7f}), {0, 0, 0});
        r.d->OMSetDepthStencilState(s.depth_on.get(), 0);
        r.d->PSSetShader(s.scene_ps.get());
        s.pass->draw(r, camera(0.4f, 0.5f, 7.0f, 1.0f, 0.9f), 0.8f * static_cast<float>(f));
        r.d->OMSetDepthStencilState(nullptr, 0);
        Com<ID3D10ShaderResourceView> hdr_view = r.srv(s.hdr.texture.get());
        Com<ID3D10ShaderResourceView> a_view = r.srv(s.half_a.texture.get());
        Com<ID3D10ShaderResourceView> b_view = r.srv(s.half_b.texture.get());
        if (!r.ok()) return;
        r.ps_smp(0, s.linear_clamp.get());
        r.bind_rt(s.half_a);
        r.viewport(128, 128);
        r.ps_srv(0, hdr_view.get());
        r.fullscreen(s.bright_ps.get());
        const float h[8] = {1.0f / 128.0f, 0, 0, 0, 0.8f + 0.35f * static_cast<float>(f), 0, 0, 0};
        const float v[8] = {0, 1.0f / 128.0f, 0, 0, 0.8f + 0.35f * static_cast<float>(f), 0, 0, 0};
        r.ps_cb(0, s.params.get());
        for (int pass = 0; pass < 2; ++pass) {
            ID3D10ShaderResourceView* none = nullptr;
            r.d->PSSetShaderResources(0, 1, &none);
            r.d->UpdateSubresource(s.params.get(), 0, nullptr, h, 0, 0);
            r.bind_rt(s.half_b);
            r.ps_srv(0, a_view.get());
            r.fullscreen(s.blur_ps.get());
            r.d->PSSetShaderResources(0, 1, &none);
            r.d->UpdateSubresource(s.params.get(), 0, nullptr, v, 0, 0);
            r.bind_rt(s.half_a);
            r.ps_srv(0, b_view.get());
            r.fullscreen(s.blur_ps.get());
        }
        ID3D10ShaderResourceView* none2[2] = {};
        r.d->PSSetShaderResources(0, 2, none2);
        {
            r.bind_rt(s.half_b);
            r.ps_srv(0, a_view.get());
            r.ps_srv(1, s.hist_view.get());
            r.fullscreen(s.hist_ps.get());
            r.d->PSSetShaderResources(0, 2, none2);
            if (!r.mutate("scene_history")) r.d->CopyResource(s.bloom_hist.texture.get(), s.half_b.texture.get());
        }
        r.d->UpdateSubresource(s.params.get(), 0, nullptr, h, 0, 0);
        r.bind_rt(r.main);
        r.viewport(256, 256);
        r.ps_srv(0, hdr_view.get());
        r.ps_srv(1, b_view.get());
        r.fullscreen(s.combine_ps.get());
        r.d->PSSetShaderResources(0, 2, none2);
    };
    multi<HdrState>(spec("scene", "hdr_bloom_with_decaying_bloom_history").size(256, 256).frames(3, {0, 1, 2}).tolerance(rr::Tolerance::filter(10, 0.01)), body);
}};

struct SkinVertex {
    float p[3], n[3], uv[2];
    std::uint8_t idx[4];
    float w[4];
};

struct SkinState {
    Com<ID3D10Buffer> vb, ib, bones, cb;
    Com<ID3D10VertexShader> vs;
    Com<ID3D10PixelShader> ps;
    Com<ID3D10InputLayout> layout;
    Com<ID3D10DepthStencilState> depth_on;
    UINT count = 0;
};

const Registrar kSkin{[] {
    MultiBody<SkinState> body;
    body.setup = [](Rig& r, SkinState& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        std::vector<SkinVertex> verts;
        std::vector<std::uint32_t> idx;
        const int rings = 32, segs = 14;
        for (int i = 0; i <= rings; ++i) {
            const float y = -1.6f + 3.2f * static_cast<float>(i) / rings;
            const float radius = 0.28f + 0.1f * std::sin(static_cast<float>(i) * 0.5f);
            const float bone = (y + 1.6f) / 3.2f * 3.0f;
            const int b0 = std::min(2, static_cast<int>(bone));
            const float frac = bone - static_cast<float>(b0);
            for (int j = 0; j <= segs; ++j) {
                const float a = 6.2831853f * static_cast<float>(j) / segs;
                SkinVertex v{};
                v.p[0] = radius * std::cos(a); v.p[1] = y; v.p[2] = radius * std::sin(a);
                v.n[0] = std::cos(a); v.n[1] = 0; v.n[2] = std::sin(a);
                v.uv[0] = static_cast<float>(j) / segs; v.uv[1] = static_cast<float>(i) / rings;
                v.idx[0] = static_cast<std::uint8_t>(b0); v.idx[1] = static_cast<std::uint8_t>(b0 + 1);
                v.w[0] = 1.0f - frac; v.w[1] = frac;
                verts.push_back(v);
            }
        }
        for (int i = 0; i < rings; ++i)
            for (int j = 0; j < segs; ++j) {
                const std::uint32_t a = static_cast<std::uint32_t>(i * (segs + 1) + j), b = a + segs + 1;
                idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        s.count = static_cast<UINT>(idx.size());
        s.vb = r.vbuf(verts.data(), static_cast<UINT>(verts.size() * sizeof(SkinVertex)));
        s.ib = r.ibuf32(idx);
        s.bones = r.cbuf(4 * 64);
        s.cb = r.cbuf(64);
        s.vs = r.vs("cbuffer Bones : register(b0) { float4x4 bones[4]; };\ncbuffer Cam : register(b1) { float4x4 vp; };\n"
                    "struct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; uint4 bi : BLENDINDICES; float4 bw : BLENDWEIGHT; };\n"
                    "struct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
                    "VO main(VI i) { VO o; float4 pos = 0; float3 nrm = 0; [unroll] for (int k = 0; k < 4; ++k) { pos += mul(bones[i.bi[k]], float4(i.p, 1)) * i.bw[k]; nrm += mul((float3x3)bones[i.bi[k]], i.n) * i.bw[k]; }\n"
                    "  o.p = mul(vp, pos); o.n = nrm; o.uv = i.uv; return o; }\n");
        s.ps = r.ps("float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0) : SV_Target { float l = saturate(dot(normalize(n), normalize(float3(0.4, 0.7, 0.6)))) * 0.8 + 0.2; float band = step(0.5, frac(uv.y * 8)); return float4(l * lerp(float3(0.9, 0.5, 0.3), float3(0.4, 0.6, 0.9), band), 1); }\n");
        s.layout = r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                             {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
                             {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D10_INPUT_PER_VERTEX_DATA, 0},
                             {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D10_INPUT_PER_VERTEX_DATA, 0},
                             {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D10_INPUT_PER_VERTEX_DATA, 0}});
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        s.depth_on = r.depth(dd);
        r.no_cull();
    };
    body.frame = [](Rig& r, SkinState& s, unsigned f) {
        const float t = 0.45f * static_cast<float>(f);
        rr::Mat4 bones[4];
        bones[0] = rr::Mat4::rotate_z(0.15f * std::sin(t));
        bones[1] = rr::Mat4::translate(0, -0.55f, 0) * rr::Mat4::rotate_z(0.5f * std::sin(t + 0.8f)) * rr::Mat4::translate(0, 0.55f, 0);
        bones[2] = bones[1] * rr::Mat4::translate(0, 0.5f, 0) * rr::Mat4::rotate_z(0.7f * std::sin(t + 1.6f)) * rr::Mat4::translate(0, -0.5f, 0);
        bones[3] = bones[2] * rr::Mat4::translate(0, 1.1f, 0) * rr::Mat4::rotate_x(0.5f * std::sin(t + 2.4f)) * rr::Mat4::translate(0, -1.1f, 0);
        r.d->UpdateSubresource(s.bones.get(), 0, nullptr, bones, 0, 0);
        const rr::Mat4 vp = camera(0.6f, 0.2f, 6.0f, 1.0f, 0.9f);
        r.d->UpdateSubresource(s.cb.get(), 0, nullptr, vp.m, 0, 0);
        r.clear(r.main, 0.12f, 0.12f, 0.18f, 1);
        r.clear_depth(r.depth_main);
        r.d->IASetInputLayout(s.layout.get());
        r.d->VSSetShader(s.vs.get());
        r.d->PSSetShader(s.ps.get());
        r.vs_cb(0, s.bones.get());
        r.vs_cb(1, s.cb.get());
        r.d->OMSetDepthStencilState(s.depth_on.get(), 0);
        const UINT stride = sizeof(SkinVertex), offset = 0;
        ID3D10Buffer* vb = s.vb.get();
        r.d->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        r.d->IASetIndexBuffer(s.ib.get(), DXGI_FORMAT_R32_UINT, 0);
        r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.d->DrawIndexed(s.count, 0, 0);
    };
    multi_single<SkinState>(spec("scene", "skinned_tube_bone_palette_animation").size(256, 256).tolerance(scene_tol(8)), body, {0, 4, 7});
}};

MeshData cone(unsigned segments, float radius, float height, float y0) {
    MeshData m;
    for (unsigned j = 0; j <= segments; ++j) {
        const float a = 6.2831853f * static_cast<float>(j) / segments;
        const float nx = std::cos(a), nz = std::sin(a);
        m.vertices.push_back({{radius * nx, y0, radius * nz}, {nx * 0.8f, 0.6f, nz * 0.8f}, {static_cast<float>(j) / segments, 0}});
        m.vertices.push_back({{0, y0 + height, 0}, {nx * 0.8f, 0.6f, nz * 0.8f}, {static_cast<float>(j) / segments, 1}});
    }
    for (unsigned j = 0; j < segments; ++j) m.indices.insert(m.indices.end(), {j * 2, j * 2 + 1, j * 2 + 2});
    return m;
}

const Registrar kForest{[] {
    single(spec("scene", "instanced_forest_two_meshes").size(256, 256).tolerance(scene_tol(10)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        struct Inst {
            float m0[4], m1[4], m2[4], color[4];
        };
        std::vector<Inst> trees, trunks;
        std::uint32_t state = 4242;
        for (int gz = 0; gz < 12; ++gz)
            for (int gx = 0; gx < 14; ++gx) {
                const float x = -6.5f + gx * 1.0f + rr::Pattern::unit_random(state) * 0.6f;
                const float z = -9.0f + gz * 1.1f + rr::Pattern::unit_random(state) * 0.6f;
                const float sc = 0.6f + 0.7f * rr::Pattern::unit_random(state);
                const float g = 0.35f + 0.5f * rr::Pattern::unit_random(state);
                trees.push_back({{sc, 0, 0, x}, {0, sc, 0, 0}, {0, 0, sc, z}, {0.15f, g, 0.2f, 1}});
                trunks.push_back({{0.25f * sc, 0, 0, x}, {0, sc, 0, 0}, {0, 0, 0.25f * sc, z}, {0.4f, 0.28f, 0.15f, 1}});
            }
        Com<ID3D10VertexShader> vs = r.vs("cbuffer Cam : register(b0) { float4x4 vp; };\n"
                                          "struct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float4 r0 : ROW0; float4 r1 : ROW1; float4 r2 : ROW2; float4 col : INSTCOLOR; uint iid : SV_InstanceID; };\n"
                                          "struct VO { float4 p : SV_Position; float3 n : NORMAL; float4 col : COLOR; };\n"
                                          "VO main(VI i) { VO o; float4 p4 = float4(i.p, 1); float3 w = float3(dot(i.r0, p4), dot(i.r1, p4), dot(i.r2, p4)); o.p = mul(vp, float4(w, 1));\n"
                                          "  o.n = float3(dot(i.r0.xyz, i.n), dot(i.r1.xyz, i.n), dot(i.r2.xyz, i.n)); o.col = i.col * (0.85 + 0.15 * frac(i.iid * 0.37)); return o; }\n");
        Com<ID3D10PixelShader> ps = r.ps("float4 main(float4 p : SV_Position, float3 n : NORMAL, float4 col : COLOR) : SV_Target { float l = saturate(dot(normalize(n), normalize(float3(0.4, 0.8, 0.5)))) * 0.75 + 0.25; return float4(col.rgb * l, 1); }\n");
        Com<ID3D10InputLayout> layout = r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                                  {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                                  {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                                  {"ROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D10_INPUT_PER_INSTANCE_DATA, 1},
                                                  {"ROW", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D10_INPUT_PER_INSTANCE_DATA, 1},
                                                  {"ROW", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D10_INPUT_PER_INSTANCE_DATA, 1},
                                                  {"INSTCOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D10_INPUT_PER_INSTANCE_DATA, 1}});
        MeshBuffers crown = upload(r, cone(10, 0.5f, 1.4f, 0.25f));
        MeshBuffers stem = upload(r, Geo::box(0.5f, 0.3f, 0.5f));
        Com<ID3D10Buffer> tree_inst = r.vbuf(trees.data(), static_cast<UINT>(trees.size() * sizeof(Inst)));
        Com<ID3D10Buffer> trunk_inst = r.vbuf(trunks.data(), static_cast<UINT>(trunks.size() * sizeof(Inst)));
        Com<ID3D10Buffer> cam = r.cb(camera(0.0f, 0.6f, 1.0f, 1.0f, 1.0f) * rr::Mat4::translate(0, -0.5f, -4.5f));
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        if (!r.ok()) return;
        r.no_cull();
        r.d->OMSetDepthStencilState(ds.get(), 0);
        r.clear(r.main, 0.5f, 0.7f, 0.9f, 1);
        r.d->IASetInputLayout(layout.get());
        r.d->VSSetShader(vs.get());
        r.d->PSSetShader(ps.get());
        r.vs_cb(0, cam.get());
        const MeshBuffers* meshes[2] = {&crown, &stem};
        Com<ID3D10Buffer>* insts[2] = {&tree_inst, &trunk_inst};
        for (int k = 0; k < 2; ++k) {
            ID3D10Buffer* vbs[2] = {meshes[k]->vertices.get(), insts[k]->get()};
            const UINT strides[2] = {sizeof(MeshVertex), sizeof(Inst)};
            const UINT offsets[2] = {0, 0};
            r.d->IASetVertexBuffers(0, 2, vbs, strides, offsets);
            r.d->IASetIndexBuffer(meshes[k]->indices.get(), DXGI_FORMAT_R32_UINT, 0);
            r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            r.d->DrawIndexedInstanced(meshes[k]->count, static_cast<UINT>(trees.size()), 0, 0, 0);
        }
        r.cap_main(o);
    });
}};

struct TerrainState {
    Com<ID3D10Texture2D> heights, detail;
    Com<ID3D10ShaderResourceView> height_view, detail_view;
    Com<ID3D10Buffer> vb, ib, cb;
    Com<ID3D10VertexShader> vs;
    Com<ID3D10PixelShader> ps;
    Com<ID3D10InputLayout> layout;
    Com<ID3D10SamplerState> point, linear;
    Com<ID3D10DepthStencilState> depth_on;
    UINT count = 0;
};

const Registrar kTerrain{[] {
    MultiBody<TerrainState> body;
    body.setup = [](Rig& r, TerrainState& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        const unsigned n = 64;
        std::vector<float> hf = Geo::height_field(n, 77);
        hf.resize(n * n);
        std::vector<float> tex(n * n);
        for (unsigned y = 0; y < n; ++y)
            for (unsigned x = 0; x < n; ++x) tex[y * n + x] = 0.5f + 0.9f * (0.5f * std::sin(x * 0.19f) * std::cos(y * 0.13f) + 0.3f * std::sin(x * 0.51f + y * 0.37f) + 0.2f * std::sin(y * 0.9f));
        s.heights = r.tex2d(n, n, DXGI_FORMAT_R32_FLOAT, D3D10_BIND_SHADER_RESOURCE, 1, 1, {tex.data()}, {n * 4});
        s.detail = r.rgba_tex(32, 32, rr::Pattern::noise(32, 32, 5), true);
        s.height_view = r.srv(s.heights.get());
        s.detail_view = r.srv(s.detail.get());
        std::vector<float> verts;
        for (unsigned z = 0; z < n; ++z)
            for (unsigned x = 0; x < n; ++x) {
                verts.push_back(static_cast<float>(x) / (n - 1));
                verts.push_back(static_cast<float>(z) / (n - 1));
            }
        std::vector<std::uint32_t> idx;
        for (unsigned z = 0; z + 1 < n; ++z)
            for (unsigned x = 0; x + 1 < n; ++x) {
                const std::uint32_t a = z * n + x;
                idx.insert(idx.end(), {a, a + n, a + 1, a + 1, a + n, a + n + 1});
            }
        s.count = static_cast<UINT>(idx.size());
        s.vb = r.vbuf(verts.data(), static_cast<UINT>(verts.size() * 4));
        s.ib = r.ibuf32(idx);
        s.cb = r.cbuf(80);
        s.vs = r.vs("cbuffer Cam : register(b0) { float4x4 vp; float4 params; };\nTexture2D hmap : register(t0); SamplerState hs : register(s0);\n"
                    "struct VI { float2 g : POSITION; };\nstruct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float h : TEXCOORD1; };\n"
                    "VO main(VI i) { VO o; float e = 1.0 / 64; float h = hmap.SampleLevel(hs, i.g, 0).r; float hx = hmap.SampleLevel(hs, i.g + float2(e, 0), 0).r - hmap.SampleLevel(hs, i.g - float2(e, 0), 0).r;\n"
                    "  float hz = hmap.SampleLevel(hs, i.g + float2(0, e), 0).r - hmap.SampleLevel(hs, i.g - float2(0, e), 0).r; float3 w = float3((i.g.x - 0.5) * 10, h * 1.4, (i.g.y - 0.5) * 10);\n"
                    "  o.p = mul(vp, float4(w, 1)); o.n = normalize(float3(-hx * 6, 1, -hz * 6)); o.uv = i.g * 12; o.h = h; return o; }\n");
        s.ps = r.ps("Texture2D detail : register(t1); SamplerState ls : register(s1);\n"
                    "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float h : TEXCOORD1) : SV_Target { float3 d = detail.Sample(ls, uv).rgb; float slope = 1 - n.y;\n"
                    "  float3 grass = float3(0.2, 0.5, 0.15) * (0.6 + 0.6 * d.g), rock = float3(0.45, 0.42, 0.4) * (0.7 + 0.5 * d.r), snow = float3(0.95, 0.95, 1.0);\n"
                    "  float3 c = lerp(grass, rock, smoothstep(0.1, 0.35, slope)); c = lerp(c, snow, smoothstep(1.05, 1.35, h) * (1 - slope)); float l = saturate(dot(n, normalize(float3(0.5, 0.8, 0.3)))) * 0.8 + 0.2; return float4(c * l, 1); }\n");
        s.layout = r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0}});
        s.point = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT, D3D10_TEXTURE_ADDRESS_CLAMP));
        s.linear = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_WRAP));
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        s.depth_on = r.depth(dd);
        r.no_cull();
    };
    body.frame = [](Rig& r, TerrainState& s, unsigned f) {
        const float t = 0.35f * static_cast<float>(f);
        const rr::Mat4 vp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.3f, 60.0f) * rr::Mat4::rotate_x(0.55f) * rr::Mat4::translate(-1.5f + t, -3.2f, -6.5f + t);
        float data[20];
        std::memcpy(data, vp.m, 64);
        r.d->UpdateSubresource(s.cb.get(), 0, nullptr, data, 0, 0);
        r.clear(r.main, 0.55f, 0.7f, 0.9f, 1);
        r.clear_depth(r.depth_main);
        r.d->IASetInputLayout(s.layout.get());
        r.d->VSSetShader(s.vs.get());
        r.d->PSSetShader(s.ps.get());
        r.vs_cb(0, s.cb.get());
        ID3D10ShaderResourceView* vs_views[1] = {s.height_view.get()};
        r.d->VSSetShaderResources(0, 1, vs_views);
        ID3D10SamplerState* vs_smp = s.point.get();
        r.d->VSSetSamplers(0, 1, &vs_smp);
        r.ps_srv(1, s.detail_view.get());
        r.ps_smp(1, s.linear.get());
        r.d->OMSetDepthStencilState(s.depth_on.get(), 0);
        const UINT stride = 8, offset = 0;
        ID3D10Buffer* vb = s.vb.get();
        r.d->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        r.d->IASetIndexBuffer(s.ib.get(), DXGI_FORMAT_R32_UINT, 0);
        r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.d->DrawIndexed(s.count, 0, 0);
    };
    multi_single<TerrainState>(spec("scene", "terrain_vertex_texture_displacement_flyover").size(256, 256).tolerance(scene_tol(14)), body, {0, 3});
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
