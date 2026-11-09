#include "render/d3d10/util.hpp"

#include "support/test_window.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::render::d3d10 {
namespace {

const char* kObjVs =
    "cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\n"
    "struct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; };\n"
    "VO main(VI i) { VO o; float4 w = mul(model, float4(i.p, 1)); o.p = mul(vp, w); o.n = mul((float3x3)model, i.n); o.uv = i.uv; o.w = w.xyz; return o; }\n";

const char* kObjPsHead =
    "cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\n"
    "struct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; };\n";

const char* kLitPs =
    "float4 main(VO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.5)))) * 0.75 + 0.25; return float4(color.rgb * l, 1); }\n";

struct Obj {
    const MeshBuffers* mesh;
    rr::Mat4 model;
    float color[4];
};

struct ObjPipe {
    explicit ObjPipe(Rig& r, const std::string& ps_body = std::string(kLitPs)) {
        vs = r.vs(kObjVs);
        layout = r.layout(mesh_elements());
        ps = r.ps(std::string(kObjPsHead) + ps_body);
        cb = r.cbuf(sizeof(ObjectConstants));
    }
    void bind(Rig& r) {
        r.d->IASetInputLayout(layout.get());
        r.d->VSSetShader(vs.get());
        r.d->PSSetShader(ps.get());
        r.vs_cb(0, cb.get());
        r.ps_cb(0, cb.get());
    }
    void draw(Rig& r, const MeshBuffers& mesh, const rr::Mat4& vp, const rr::Mat4& model, const float color[4], const float extra[4] = nullptr) {
        ObjectConstants c{};
        std::memcpy(c.vp, vp.m, 64);
        std::memcpy(c.model, model.m, 64);
        std::memcpy(c.color, color, 16);
        if (extra != nullptr) std::memcpy(c.extra, extra, 16);
        r.d->UpdateSubresource(cb.get(), 0, nullptr, &c, 0, 0);
        draw_mesh(r, mesh);
    }
    Com<ID3D10VertexShader> vs;
    Com<ID3D10PixelShader> ps;
    Com<ID3D10InputLayout> layout;
    Com<ID3D10Buffer> cb;
};

struct Props {
    explicit Props(Rig& r) : ground(upload(r, Geo::box(5.0f, 0.1f, 5.0f))), ball(upload(r, Geo::sphere(16, 24, 0.8f))), ring(upload(r, Geo::torus(24, 12, 0.8f, 0.28f))),
                             cube(upload(r, Geo::box(0.55f, 0.55f, 0.55f))) {}
    [[nodiscard]] std::vector<Obj> items(float t) const {
        return {{&ball, rr::Mat4::translate(-1.4f, 0.2f + 0.2f * std::sin(t), 0.2f), {0.9f, 0.35f, 0.25f, 1}},
                {&ring, rr::Mat4::translate(1.3f, 0.35f, -0.5f) * rr::Mat4::rotate_x(1.0f + 0.4f * t) * rr::Mat4::rotate_y(0.6f * t), {0.3f, 0.75f, 0.4f, 1}},
                {&cube, rr::Mat4::translate(0.1f, 0.3f, 1.3f) * rr::Mat4::rotate_y(0.5f + 0.5f * t), {0.3f, 0.45f, 0.9f, 1}}};
    }
    MeshBuffers ground, ball, ring, cube;
};

Com<ID3D10DepthStencilState> depth_state(Rig& r, bool test, bool write, D3D10_COMPARISON_FUNC func = D3D10_COMPARISON_LESS_EQUAL) {
    auto dd = Rig::depth_desc();
    dd.DepthEnable = test ? TRUE : FALSE;
    dd.DepthWriteMask = write ? D3D10_DEPTH_WRITE_MASK_ALL : D3D10_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = func;
    return r.depth(dd);
}

const Registrar kSwap{[] {
    struct S {
        TestWindow window;
        Com<IDXGISwapChain> chain;
        Com<ID3D10Texture2D> snapshot;
        std::unique_ptr<ObjPipe> pipe;
        std::unique_ptr<Props> props;
        Com<ID3D10DepthStencilState> ds;
        Depth depth;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        TestWindowConfig config;
        TestWindow::fit_config(config, 192, 192);
        std::string e;
        if (!s.window.create(config, e)) {
            r.fail(e);
            return;
        }
        Com<IDXGIDevice> dxgi;
        Com<IDXGIAdapter> adapter;
        Com<IDXGIFactory> factory;
        r.check(r.d->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void()), "IDXGIDevice");
        if (!r.ok()) return;
        r.check(dxgi->GetAdapter(adapter.put()), "GetAdapter");
        if (!r.ok()) return;
        r.check(adapter->GetParent(__uuidof(IDXGIFactory), factory.put_void()), "GetParent");
        if (!r.ok()) return;
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = 192;
        desc.BufferDesc.Height = 192;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = s.window.hwnd();
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        r.check(factory->CreateSwapChain(r.d, &desc, s.chain.put()), "CreateSwapChain");
        factory->MakeWindowAssociation(s.window.hwnd(), DXGI_MWA_NO_ALT_ENTER);
        s.snapshot = r.tex2d(192, 192, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE);
        r.depth_target(s.depth, 192, 192);
        s.pipe = std::make_unique<ObjPipe>(r);
        s.props = std::make_unique<Props>(r);
        s.ds = depth_state(r, true, true);
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        Com<ID3D10Texture2D> back;
        r.check(s.chain->GetBuffer(0, __uuidof(ID3D10Texture2D), back.put_void()), "GetBuffer");
        if (!r.ok()) return;
        Com<ID3D10RenderTargetView> rtv;
        r.check(r.d->CreateRenderTargetView(back.get(), nullptr, rtv.put()), "CreateRenderTargetView");
        if (!r.ok()) return;
        ID3D10RenderTargetView* rt = rtv.get();
        r.d->OMSetRenderTargets(1, &rt, s.depth.view.get());
        const float sky[4] = {0.45f, 0.6f, 0.8f, 1};
        r.d->ClearRenderTargetView(rt, sky);
        r.clear_depth(s.depth);
        r.viewport(192, 192);
        r.d->OMSetDepthStencilState(s.ds.get(), 0);
        s.pipe->bind(r);
        const float t = 0.5f * static_cast<float>(f);
        const rr::Mat4 vp = camera(0.4f + 0.1f * t, 0.5f, 6.5f, 1.0f, 0.9f);
        const float ground_color[4] = {0.6f, 0.6f, 0.65f, 1};
        s.pipe->draw(r, s.props->ground, vp, rr::Mat4::translate(0, -0.6f, 0), ground_color);
        for (const Obj& o : s.props->items(t)) s.pipe->draw(r, *o.mesh, vp, o.model, o.color);
        r.d->CopyResource(s.snapshot.get(), back.get());
        r.d->OMSetRenderTargets(0, nullptr, nullptr);
        if (f == 3) {
            rtv.reset();
            back.reset();
            r.check(s.chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
            if (!r.ok()) return;
        }
        r.check(s.chain->Present(0, 0), "Present");
    };
    body.capture = [](Rig& r, S& s, unsigned, rr::CaptureSet& o) { r.cap_tex(o, "backbuffer", s.snapshot.get()); };
    multi<S>(spec("scene", "swap_chain_present_loop_backbuffer_snapshots").size(192, 192).frames(6, {0, 3, 5}).tolerance(scene_tol(8)), body);
}};

const Registrar kWater{[] {
    struct S {
        std::unique_ptr<ObjPipe> pipe;
        std::unique_ptr<Props> props;
        Target reflection;
        Depth reflection_depth;
        Com<ID3D10ShaderResourceView> reflection_view;
        Com<ID3D10PixelShader> water_ps;
        Com<ID3D10DepthStencilState> ds, ds_read;
        Com<ID3D10BlendState> alpha;
        Com<ID3D10SamplerState> smp;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        s.pipe = std::make_unique<ObjPipe>(r);
        s.props = std::make_unique<Props>(r);
        r.target(s.reflection, 256, 256);
        r.depth_target(s.reflection_depth, 256, 256);
        s.reflection_view = r.srv(s.reflection.texture.get());
        s.water_ps = r.ps(std::string(kObjPsHead) + "Texture2D refl : register(t0); SamplerState s : register(s0);\n"
                          "float4 main(VO i) : SV_Target { float2 uv = i.p.xy / 256; float2 wob = float2(sin(i.w.x * 5 + extra.x) + sin(i.w.z * 7 - extra.x * 1.3), cos(i.w.z * 6 + extra.x) + cos(i.w.x * 4 + extra.x * 0.7)) * 0.01;\n"
                          "  float3 rc = refl.Sample(s, uv + wob).rgb; float fres = 0.35 + 0.35 * pow(1 - saturate(dot(normalize(float3(0, 1, 0.5)), float3(0, 1, 0))), 2); float3 deep = float3(0.05, 0.25, 0.35); return float4(lerp(deep, rc, 0.55), 0.8); }\n");
        s.ds = depth_state(r, true, true);
        s.ds_read = depth_state(r, true, false);
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        bd.SrcBlendAlpha = D3D10_BLEND_ONE;
        bd.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
        s.alpha = r.blend(bd);
        s.smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        const float t = 0.6f * static_cast<float>(f);
        const rr::Mat4 vp = camera(0.5f, 0.55f, 7.0f, 1.0f, 0.9f);
        const float water_y = -0.2f;
        const rr::Mat4 mirror = rr::Mat4::translate(0, water_y, 0) * rr::Mat4::scale(1, -1, 1) * rr::Mat4::translate(0, -water_y, 0);
        ID3D10ShaderResourceView* none = nullptr;
        r.d->PSSetShaderResources(0, 1, &none);
        r.bind_rt(s.reflection, &s.reflection_depth);
        r.clear(s.reflection, 0.5f, 0.65f, 0.85f, 1);
        r.clear_depth(s.reflection_depth);
        r.d->OMSetDepthStencilState(s.ds.get(), 0);
        s.pipe->bind(r);
        for (const Obj& o : s.props->items(t)) s.pipe->draw(r, *o.mesh, vp, mirror * o.model, o.color);
        r.bind_rt(r.main, &r.depth_main);
        r.clear(r.main, 0.5f, 0.65f, 0.85f, 1);
        r.clear_depth(r.depth_main);
        r.viewport(256, 256);
        const float bed[4] = {0.55f, 0.5f, 0.35f, 1};
        s.pipe->draw(r, s.props->ground, vp, rr::Mat4::translate(0, -1.4f, 0), bed);
        for (const Obj& o : s.props->items(t)) s.pipe->draw(r, *o.mesh, vp, o.model, o.color);
        r.d->OMSetDepthStencilState(s.ds_read.get(), 0);
        const float bf[4] = {1, 1, 1, 1};
        r.d->OMSetBlendState(s.alpha.get(), bf, 0xffffffffu);
        r.d->PSSetShader(s.water_ps.get());
        r.ps_srv(0, s.reflection_view.get());
        r.ps_smp(0, s.smp.get());
        const float wc[4] = {0, 0, 0, 1};
        const float extra[4] = {t * 2.0f, 0, 0, 0};
        s.pipe->draw(r, s.props->ground, vp, rr::Mat4::translate(0, water_y, 0) * rr::Mat4::scale(1, 0.02f, 1), wc, extra);
        r.d->OMSetBlendState(nullptr, bf, 0xffffffffu);
        r.d->PSSetShaderResources(0, 1, &none);
    };
    multi<S>(spec("scene", "water_planar_reflection_with_ripples").size(256, 256).frames(3, {0, 1, 2}).tolerance(scene_tol(24, 1e-3, 64)), body);
}};

const Registrar kDecals{[] {
    single(spec("scene", "projected_decals_from_world_position_target").size(256, 256).tolerance(scene_tol(10)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        Target wpos;
        r.target(wpos, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT);
        ObjPipe pipe(r, "struct PO { float4 c : SV_Target0; float4 w : SV_Target1; };\n"
                        "PO main(VO i) { PO o; float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.5)))) * 0.75 + 0.25; o.c = float4(color.rgb * l, 1); o.w = float4(i.w, 1); return o; }\n");
        Props props(r);
        std::vector<rr::Pattern::Bytes> unused;
        rr::Pattern::Bytes px(32 * 32 * 4);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const float dx = (x - 15.5f) / 15.5f, dy = (y - 15.5f) / 15.5f, d = std::sqrt(dx * dx + dy * dy);
                const bool ring = d > 0.55f && d < 0.8f, cross = std::fabs(dx) < 0.12f || std::fabs(dy) < 0.12f;
                px[(y * 32 + x) * 4] = ring ? 255 : 40;
                px[(y * 32 + x) * 4 + 1] = cross ? 230 : 30;
                px[(y * 32 + x) * 4 + 2] = 30;
                px[(y * 32 + x) * 4 + 3] = (ring || (cross && d < 0.8f)) ? 255 : 0;
            }
        Com<ID3D10Texture2D> decal_tex = r.rgba_tex(32, 32, px);
        Com<ID3D10ShaderResourceView> decal_view = r.srv(decal_tex.get());
        Com<ID3D10ShaderResourceView> wpos_view = r.srv(wpos.texture.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        Com<ID3D10PixelShader> decal_ps = r.ps("cbuffer D : register(b0) { float4x4 inv; float4 tint; };\nTexture2D wp : register(t0); Texture2D dec : register(t1); SamplerState s : register(s0);\n"
                                               "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { float4 w = wp.Load(int3(p.xy, 0)); if (w.a < 0.5) discard; float3 q = mul(inv, float4(w.xyz, 1)).xyz; if (any(abs(q) > 1)) discard;\n"
                                               "  float4 c = dec.Sample(s, q.xz * 0.5 + 0.5); return float4(c.rgb * tint.rgb, c.a * tint.a); }\n");
        Com<ID3D10Buffer> decal_cb = r.cbuf(80);
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        bd.SrcBlendAlpha = D3D10_BLEND_ZERO;
        bd.DestBlendAlpha = D3D10_BLEND_ONE;
        Com<ID3D10BlendState> alpha = r.blend(bd);
        Com<ID3D10DepthStencilState> ds = depth_state(r, true, true);
        Com<ID3D10DepthStencilState> off = depth_state(r, false, false);
        if (!r.ok()) return;
        r.no_cull();
        ID3D10RenderTargetView* views[2] = {r.main.view.get(), wpos.view.get()};
        r.d->OMSetRenderTargets(2, views, r.depth_main.view.get());
        const float sky[4] = {0.5f, 0.65f, 0.85f, 1}, zero[4] = {0, 0, 0, 0};
        r.d->ClearRenderTargetView(views[0], sky);
        r.d->ClearRenderTargetView(views[1], zero);
        r.d->OMSetDepthStencilState(ds.get(), 0);
        pipe.bind(r);
        const rr::Mat4 vp = camera(0.5f, 0.6f, 7.0f, 1.0f, 0.9f);
        const float ground[4] = {0.6f, 0.6f, 0.65f, 1};
        pipe.draw(r, props.ground, vp, rr::Mat4::translate(0, -0.6f, 0), ground);
        for (const Obj& ob : props.items(0.4f)) pipe.draw(r, *ob.mesh, vp, ob.model, ob.color);
        r.bind_rt(r.main);
        r.d->OMSetDepthStencilState(off.get(), 0);
        const float bf[4] = {1, 1, 1, 1};
        r.d->OMSetBlendState(alpha.get(), bf, 0xffffffffu);
        r.ps_srv(0, wpos_view.get());
        r.ps_srv(1, decal_view.get());
        r.ps_smp(0, smp.get());
        r.ps_cb(0, decal_cb.get());
        struct Decal { float x, y, z, size, angle; float tint[4]; };
        const Decal decals[3] = {{-0.2f, -0.6f, 0.6f, 1.3f, 0.4f, {1, 1, 1, 0.9f}}, {-1.4f, 0.5f, 0.2f, 1.0f, 1.1f, {0.9f, 0.9f, 0.3f, 0.8f}}, {1.3f, 0.3f, -0.5f, 1.2f, 0.0f, {0.6f, 1, 1, 0.85f}}};
        for (const Decal& d : decals) {
            const rr::Mat4 inv = rr::Mat4::scale(1 / d.size, 1 / d.size, 1 / d.size) * rr::Mat4::rotate_y(-d.angle) * rr::Mat4::translate(-d.x, -d.y, -d.z);
            float data[20];
            std::memcpy(data, inv.m, 64);
            std::memcpy(data + 16, d.tint, 16);
            r.d->UpdateSubresource(decal_cb.get(), 0, nullptr, data, 0, 0);
            r.fullscreen(decal_ps.get());
        }
        r.d->OMSetBlendState(nullptr, bf, 0xffffffffu);
        r.cap_main(o);
    });
}};

const Registrar kBlur{[] {
    struct S {
        std::unique_ptr<ObjPipe> pipe;
        std::unique_ptr<Props> props;
        Target temp, accum;
        Depth temp_depth;
        Com<ID3D10ShaderResourceView> temp_view, accum_view;
        Com<ID3D10PixelShader> add_ps, out_ps;
        Com<ID3D10BlendState> additive;
        Com<ID3D10DepthStencilState> ds;
        Com<ID3D10SamplerState> point;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256);
        s.pipe = std::make_unique<ObjPipe>(r);
        s.props = std::make_unique<Props>(r);
        r.target(s.temp, 256, 256);
        r.target(s.accum, 256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT);
        r.depth_target(s.temp_depth, 256, 256);
        s.temp_view = r.srv(s.temp.texture.get());
        s.accum_view = r.srv(s.accum.texture.get());
        s.add_ps = r.ps("Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return t.Load(int3(p.xy, 0)) * 0.25; }\n");
        s.out_ps = r.ps("Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return float4(t.Load(int3(p.xy, 0)).rgb, 1); }\n");
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = bd.DestBlend = D3D10_BLEND_ONE;
        bd.SrcBlendAlpha = bd.DestBlendAlpha = D3D10_BLEND_ONE;
        s.additive = r.blend(bd);
        s.ds = depth_state(r, true, true);
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        r.clear(s.accum, 0, 0, 0, 0);
        const rr::Mat4 vp = camera(0.5f, 0.55f, 6.5f, 1.0f, 0.9f);
        const float bf[4] = {1, 1, 1, 1};
        ID3D10ShaderResourceView* none = nullptr;
        for (int k = 0; k < 4; ++k) {
            const float t = 0.35f * (static_cast<float>(f) + 0.25f * static_cast<float>(k));
            r.d->PSSetShaderResources(0, 1, &none);
            r.d->OMSetBlendState(nullptr, bf, 0xffffffffu);
            r.bind_rt(s.temp, &s.temp_depth);
            r.clear(s.temp, 0.45f, 0.6f, 0.8f, 1);
            r.clear_depth(s.temp_depth);
            r.viewport(256, 256);
            r.d->OMSetDepthStencilState(s.ds.get(), 0);
            s.pipe->bind(r);
            const float ground[4] = {0.6f, 0.6f, 0.65f, 1};
            s.pipe->draw(r, s.props->ground, vp, rr::Mat4::translate(0, -0.6f, 0), ground);
            for (const Obj& o : s.props->items(t * 3.0f)) s.pipe->draw(r, *o.mesh, vp, o.model, o.color);
            r.bind_rt(s.accum);
            r.d->OMSetDepthStencilState(nullptr, 0);
            r.d->OMSetBlendState(s.additive.get(), bf, 0xffffffffu);
            r.ps_srv(0, s.temp_view.get());
            r.fullscreen(s.add_ps.get());
        }
        r.d->PSSetShaderResources(0, 1, &none);
        r.d->OMSetBlendState(nullptr, bf, 0xffffffffu);
        r.bind_rt(r.main);
        r.ps_srv(0, s.accum_view.get());
        r.fullscreen(s.out_ps.get());
        r.d->PSSetShaderResources(0, 1, &none);
    };
    multi<S>(spec("scene", "motion_blur_four_subframe_accumulation").size(256, 256).frames(6, {1, 3, 5}).tolerance(scene_tol(12, 5e-4, 40)), body);
}};

const Registrar kBillboards{[] {
    struct S {
        Com<ID3D10VertexShader> vs;
        Com<ID3D10PixelShader> ps;
        Com<ID3D10InputLayout> layout;
        Com<ID3D10Buffer> cb;
        Com<ID3D10ShaderResourceView> view;
        Com<ID3D10SamplerState> smp;
        Com<ID3D10BlendState> alpha;
        Com<ID3D10DepthStencilState> ds_test, ds_read;
        std::unique_ptr<ObjPipe> pipe;
        std::unique_ptr<Props> props;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        s.pipe = std::make_unique<ObjPipe>(r);
        s.props = std::make_unique<Props>(r);
        s.vs = r.vs("cbuffer C : register(b0) { float4x4 vp; };\nstruct VI { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\nstruct VO { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
                    "VO main(VI i) { VO o; o.p = mul(vp, float4(i.p, 1)); o.c = i.c; o.uv = i.uv; return o; }\n");
        s.layout = r.std_layout();
        s.ps = r.ps("Texture2D t : register(t0); SamplerState s : register(s0);\nfloat4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { float4 v = t.Sample(s, uv); return float4(c.rgb * v.rgb, c.a * v.a); }\n");
        rr::Pattern::Bytes px(64 * 64 * 4);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const float dx = (x - 31.5f) / 32.0f, dy = (y - 31.5f) / 32.0f, d = std::sqrt(dx * dx + dy * dy);
                const float a = std::max(0.0f, 1.0f - d);
                px[(y * 64 + x) * 4] = 255;
                px[(y * 64 + x) * 4 + 1] = static_cast<std::uint8_t>(200 - 120 * d);
                px[(y * 64 + x) * 4 + 2] = static_cast<std::uint8_t>(120 * (1 - d));
                px[(y * 64 + x) * 4 + 3] = static_cast<std::uint8_t>(255 * a * a);
            }
        Com<ID3D10Texture2D> tex = r.rgba_tex(64, 64, px, true);
        s.view = r.srv(tex.get());
        s.smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        s.cb = r.cbuf(64);
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        bd.SrcBlendAlpha = D3D10_BLEND_ONE;
        bd.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
        s.alpha = r.blend(bd);
        s.ds_test = depth_state(r, true, true);
        s.ds_read = depth_state(r, true, false);
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        const float yaw = 0.35f + 0.25f * static_cast<float>(f);
        const rr::Mat4 vp = camera(yaw, 0.45f, 7.0f, 1.0f, 0.9f);
        r.clear(r.main, 0.1f, 0.12f, 0.2f, 1);
        r.clear_depth(r.depth_main);
        r.d->OMSetDepthStencilState(s.ds_test.get(), 0);
        s.pipe->bind(r);
        const float ground[4] = {0.5f, 0.5f, 0.55f, 1};
        s.pipe->draw(r, s.props->ground, vp, rr::Mat4::translate(0, -0.6f, 0), ground);
        for (const Obj& o : s.props->items(0.5f)) s.pipe->draw(r, *o.mesh, vp, o.model, o.color);
        struct B { V3 pos; float depth; float size; unsigned color; };
        std::vector<B> list;
        std::uint32_t state = 777;
        const V3 right = {std::cos(yaw), 0, std::sin(yaw) * 1.0f}, up = {0, 1, 0};
        for (int i = 0; i < 70; ++i) {
            const V3 p = {-3.0f + 6.0f * rr::Pattern::unit_random(state), -0.2f + 2.0f * rr::Pattern::unit_random(state), -3.0f + 6.0f * rr::Pattern::unit_random(state)};
            const float d = -(p.x * std::sin(yaw) * 0 + dot3(p, {-std::sin(yaw), 0, std::cos(yaw)}));
            list.push_back({p, d, 0.25f + 0.35f * rr::Pattern::unit_random(state), static_cast<unsigned>(i)});
        }
        std::sort(list.begin(), list.end(), [](const B& a, const B& b) { return a.depth > b.depth; });
        std::vector<Vtx> verts;
        for (const B& b : list) {
            const float* c = palette(b.color);
            const V3 rr_ = {right.x * b.size, 0, right.z * b.size};
            const V3 uu = {0, up.y * b.size, 0};
            auto corner = [&](float sx, float sy, float u, float v) { return Vtx{b.pos.x + rr_.x * sx + uu.x * sy, b.pos.y + uu.y * sy, b.pos.z + rr_.z * sx, 0.6f + 0.4f * c[0], 0.6f + 0.4f * c[1], 0.6f + 0.4f * c[2], 0.9f, u, v}; };
            const Vtx a = corner(-1, 1, 0, 0), bb = corner(1, 1, 1, 0), cc = corner(1, -1, 1, 1), dd = corner(-1, -1, 0, 1);
            verts.insert(verts.end(), {a, bb, cc, a, cc, dd});
        }
        r.d->UpdateSubresource(s.cb.get(), 0, nullptr, vp.m, 0, 0);
        r.d->IASetInputLayout(s.layout.get());
        r.d->VSSetShader(s.vs.get());
        r.d->PSSetShader(s.ps.get());
        r.vs_cb(0, s.cb.get());
        r.ps_srv(0, s.view.get());
        r.ps_smp(0, s.smp.get());
        const float bf[4] = {1, 1, 1, 1};
        r.d->OMSetBlendState(s.alpha.get(), bf, 0xffffffffu);
        r.d->OMSetDepthStencilState(s.ds_read.get(), 0);
        r.draw_std(verts);
        r.d->OMSetBlendState(nullptr, bf, 0xffffffffu);
    };
    multi<S>(spec("scene", "sorted_alpha_blended_billboards_over_opaque").size(256, 256).frames(3, {0, 1, 2}).tolerance(scene_tol(16, 5e-4, 40)), body);
}};

const Registrar kMirror{[] {
    single(spec("scene", "planar_mirror_through_stencil_mask").size(256, 256).tolerance(scene_tol(12)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        r.depth_target(r.depth_main, 256, 256, DXGI_FORMAT_D24_UNORM_S8_UINT);
        r.bind_rt(r.main, &r.depth_main);
        r.clear(r.main, 0.3f, 0.35f, 0.45f, 1);
        r.clear_depth(r.depth_main, 1.0f, 0);
        ObjPipe pipe(r);
        Props props(r);
        MeshBuffers wall = upload(r, Geo::box(2.0f, 1.5f, 0.05f));
        Com<ID3D10VertexShader> far_vs = r.vs("cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\nstruct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
                                              "float4 main(VI i) : SV_Position { float4 p = mul(vp, mul(model, float4(i.p, 1))); p.z = p.w * 0.99999; return p; }\n");
        Com<ID3D10PixelShader> flat = r.ps("float4 main(float4 p : SV_Position) : SV_Target { return float4(0.7, 0.8, 0.9, 1); }\n");
        auto base = Rig::depth_desc();
        base.DepthEnable = TRUE;
        base.DepthFunc = D3D10_COMPARISON_LESS_EQUAL;
        Com<ID3D10DepthStencilState> normal = r.depth(base);
        auto mark = base;
        mark.StencilEnable = TRUE;
        mark.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_REPLACE, D3D10_COMPARISON_ALWAYS};
        mark.BackFace = mark.FrontFace;
        Com<ID3D10DepthStencilState> mark_state = r.depth(mark);
        auto reset = mark;
        reset.DepthFunc = D3D10_COMPARISON_ALWAYS;
        reset.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_EQUAL};
        reset.BackFace = reset.FrontFace;
        Com<ID3D10DepthStencilState> reset_state = r.depth(reset);
        auto inside = base;
        inside.StencilEnable = TRUE;
        inside.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_EQUAL};
        inside.BackFace = inside.FrontFace;
        Com<ID3D10DepthStencilState> inside_state = r.depth(inside);
        auto no_color = Rig::blend_desc();
        for (int i = 0; i < 8; ++i) no_color.RenderTargetWriteMask[i] = 0;
        Com<ID3D10BlendState> masked = r.blend(no_color);
        auto bd = Rig::blend_desc();
        bd.BlendEnable[0] = TRUE;
        bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        bd.SrcBlendAlpha = D3D10_BLEND_ONE;
        bd.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
        Com<ID3D10BlendState> glass = r.blend(bd);
        if (!r.ok()) return;
        r.no_cull();
        const rr::Mat4 vp = camera(0.35f, 0.25f, 8.0f, 1.0f, 0.8f) * rr::Mat4::translate(0, 0, 1.0f);
        const float bf[4] = {1, 1, 1, 1};
        const rr::Mat4 wall_model = rr::Mat4::translate(0, 0.9f, -2.5f);
        const rr::Mat4 reflect = rr::Mat4::translate(0, 0, -2.5f) * rr::Mat4::scale(1, 1, -1) * rr::Mat4::translate(0, 0, 2.5f);
        pipe.bind(r);
        r.d->OMSetDepthStencilState(normal.get(), 0);
        const float floor_color[4] = {0.55f, 0.55f, 0.6f, 1}, frame_color[4] = {0.35f, 0.25f, 0.15f, 1}, glass_color[4] = {0.7f, 0.85f, 1.0f, 0.25f};
        pipe.draw(r, props.ground, vp, rr::Mat4::translate(0, -0.6f, 0), floor_color);
        pipe.draw(r, wall, vp, wall_model * rr::Mat4::translate(0, 0, -0.15f) * rr::Mat4::scale(1.08f, 1.08f, 1), frame_color);
        for (const Obj& ob : props.items(0.9f)) pipe.draw(r, *ob.mesh, vp, ob.model, ob.color);
        r.d->OMSetBlendState(masked.get(), bf, 0xffffffffu);
        r.d->OMSetDepthStencilState(mark_state.get(), 1);
        pipe.draw(r, wall, vp, wall_model, glass_color);
        r.d->OMSetDepthStencilState(reset_state.get(), 1);
        r.d->VSSetShader(far_vs.get());
        r.d->PSSetShader(flat.get());
        ObjectConstants c{};
        std::memcpy(c.vp, vp.m, 64);
        std::memcpy(c.model, wall_model.m, 64);
        r.d->UpdateSubresource(pipe.cb.get(), 0, nullptr, &c, 0, 0);
        draw_mesh(r, wall);
        r.d->OMSetBlendState(nullptr, bf, 0xffffffffu);
        pipe.bind(r);
        r.d->OMSetDepthStencilState(inside_state.get(), 1);
        for (const Obj& ob : props.items(0.9f)) pipe.draw(r, *ob.mesh, vp, reflect * ob.model, ob.color);
        r.d->OMSetBlendState(glass.get(), bf, 0xffffffffu);
        r.d->OMSetDepthStencilState(normal.get(), 0);
        pipe.draw(r, wall, vp, wall_model, glass_color);
        r.d->OMSetBlendState(nullptr, bf, 0xffffffffu);
        r.cap_main(o);
        r.cap_stencil(o, "stencil", r.depth_main);
    });
}};

const Registrar kPick{[] {
    struct S {
        std::unique_ptr<ObjPipe> id_pipe, lit_pipe;
        std::unique_ptr<Props> props;
        Target ids;
        Depth depth;
        Com<ID3D10DepthStencilState> ds;
        unsigned selected = 0;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        s.id_pipe = std::make_unique<ObjPipe>(r, "uint main(VO i) : SV_Target { return asuint(extra.x); }\n");
        s.lit_pipe = std::make_unique<ObjPipe>(r, "float4 main(VO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.5)))) * 0.75 + 0.25; float3 c = color.rgb * l; if (asuint(extra.x) == asuint(extra.y)) c = c * 0.4 + float3(0.6, 0.55, 0.1); return float4(c, 1); }\n");
        s.props = std::make_unique<Props>(r);
        r.target(s.ids, 256, 256, DXGI_FORMAT_R32_UINT);
        s.ds = depth_state(r, true, true);
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        const rr::Mat4 vp = camera(0.5f, 0.5f, 6.5f, 1.0f, 0.9f);
        const float t = 0.3f * static_cast<float>(f);
        const auto objects = s.props->items(t);
        const unsigned ids[4] = {1, 2, 3, 4};
        r.bind_rt(s.ids, &r.depth_main);
        const UINT zero[4] = {0, 0, 0, 0};
        r.d->ClearRenderTargetView(s.ids.view.get(), reinterpret_cast<const float*>(zero));
        r.clear_depth(r.depth_main);
        r.viewport(256, 256);
        r.d->OMSetDepthStencilState(s.ds.get(), 0);
        s.id_pipe->bind(r);
        auto id_extra = [&](unsigned id, float out[4]) { std::memcpy(&out[0], &id, 4); out[1] = out[2] = out[3] = 0; };
        float extra[4];
        const float dummy[4] = {};
        id_extra(ids[0], extra);
        s.id_pipe->draw(r, s.props->ground, vp, rr::Mat4::translate(0, -0.6f, 0), dummy, extra);
        for (std::size_t i = 0; i < objects.size(); ++i) {
            id_extra(ids[i + 1], extra);
            s.id_pipe->draw(r, *objects[i].mesh, vp, objects[i].model, dummy, extra);
        }
        rr::Image id_image;
        r.read(s.ids, id_image);
        if (!r.ok()) return;
        const unsigned cursor_x[4] = {70, 128, 190, 128}, cursor_y[4] = {128, 110, 150, 200};
        const unsigned previous = s.selected;
        s.selected = static_cast<unsigned>(id_image.channel(cursor_x[f % 4], cursor_y[f % 4], 0));
        r.bind_rt(r.main, &r.depth_main);
        r.clear(r.main, 0.45f, 0.6f, 0.8f, 1);
        r.clear_depth(r.depth_main);
        s.lit_pipe->bind(r);
        auto lit_extra = [&](unsigned id, float out[4]) { std::memcpy(&out[0], &id, 4); std::memcpy(&out[1], &previous, 4); out[2] = out[3] = 0; };
        const float ground[4] = {0.6f, 0.6f, 0.65f, 1};
        lit_extra(ids[0], extra);
        s.lit_pipe->draw(r, s.props->ground, vp, rr::Mat4::translate(0, -0.6f, 0), ground, extra);
        for (std::size_t i = 0; i < objects.size(); ++i) {
            lit_extra(ids[i + 1], extra);
            s.lit_pipe->draw(r, *objects[i].mesh, vp, objects[i].model, objects[i].color, extra);
        }
    };
    body.capture = [](Rig& r, S& s, unsigned, rr::CaptureSet& o) {
        r.cap_main(o);
        r.cap(o, "ids", s.ids);
    };
    multi<S>(spec("scene", "object_id_target_pick_readback_drives_highlight").size(256, 256).frames(4, {0, 1, 2, 3}).tolerance(scene_tol(12)).tolerance_for("ids", rr::Tolerance::floats(0.5f, 0.0f).with_edge(1)), body);
}};

const Registrar kPost{[] {
    struct S {
        std::unique_ptr<ObjPipe> pipe;
        std::unique_ptr<Props> props;
        Target scene;
        Depth depth;
        Com<ID3D10Texture3D> lut;
        Com<ID3D10ShaderResourceView> scene_view, lut_view;
        Com<ID3D10PixelShader> post_ps;
        Com<ID3D10SamplerState> linear;
        Com<ID3D10Buffer> cb;
        Com<ID3D10DepthStencilState> ds;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin(256, 256);
        s.pipe = std::make_unique<ObjPipe>(r);
        s.props = std::make_unique<Props>(r);
        r.target(s.scene, 256, 256);
        r.depth_target(s.depth, 256, 256);
        s.scene_view = r.srv(s.scene.texture.get());
        std::vector<std::uint8_t> lut(16 * 16 * 16 * 4);
        for (int b = 0; b < 16; ++b)
            for (int g = 0; g < 16; ++g)
                for (int x = 0; x < 16; ++x) {
                    const float rr_ = x / 15.0f, gg = g / 15.0f, bb = b / 15.0f;
                    const float l = 0.3f * rr_ + 0.59f * gg + 0.11f * bb;
                    const float c = 1.25f;
                    auto curve = [&](float v) { return std::min(1.0f, std::max(0.0f, (v - 0.5f) * c + 0.5f)); };
                    std::uint8_t* p = &lut[((b * 16 + g) * 16 + x) * 4];
                    p[0] = static_cast<std::uint8_t>(255 * curve(rr_ * 0.85f + l * 0.25f + 0.04f));
                    p[1] = static_cast<std::uint8_t>(255 * curve(gg * 0.9f + l * 0.1f));
                    p[2] = static_cast<std::uint8_t>(255 * curve(bb * 0.8f + (1 - l) * 0.12f));
                    p[3] = 255;
                }
        D3D10_TEXTURE3D_DESC desc{};
        desc.Width = desc.Height = desc.Depth = 16;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D10_USAGE_DEFAULT;
        desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
        D3D10_SUBRESOURCE_DATA init{lut.data(), 16 * 4, 16 * 16 * 4};
        r.check(r.d->CreateTexture3D(&desc, &init, s.lut.put()), "LUT");
        s.lut_view = r.srv(s.lut.get());
        s.post_ps = r.ps("cbuffer P : register(b0) { float4 params; };\nTexture2D scene : register(t0); Texture3D lut : register(t1); SamplerState s : register(s0);\n"
                         "float lum(float3 c) { return dot(c, float3(0.3, 0.59, 0.11)); }\n"
                         "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int3 q = int3(p.xy, 0);\n"
                         "  float tl = lum(scene.Load(q + int3(-1, -1, 0)).rgb), t = lum(scene.Load(q + int3(0, -1, 0)).rgb), tr = lum(scene.Load(q + int3(1, -1, 0)).rgb), l = lum(scene.Load(q + int3(-1, 0, 0)).rgb);\n"
                         "  float r = lum(scene.Load(q + int3(1, 0, 0)).rgb), bl = lum(scene.Load(q + int3(-1, 1, 0)).rgb), b = lum(scene.Load(q + int3(0, 1, 0)).rgb), br = lum(scene.Load(q + int3(1, 1, 0)).rgb);\n"
                         "  float gx = -tl - 2 * l - bl + tr + 2 * r + br, gy = -tl - 2 * t - tr + bl + 2 * b + br; float edge = saturate(sqrt(gx * gx + gy * gy) * 1.5);\n"
                         "  float3 c = scene.Load(q).rgb; float3 graded = lut.SampleLevel(s, c * (15.0 / 16.0) + 0.5 / 16.0, 0).rgb; c = lerp(c, graded, params.x); c = lerp(c, float3(0.02, 0.02, 0.05), edge * params.y);\n"
                         "  float2 d = uv - 0.5; c *= 1 - dot(d, d) * params.z; return float4(c, 1); }\n");
        s.linear = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        s.cb = r.cbuf(16);
        s.ds = depth_state(r, true, true);
        r.no_cull();
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        const rr::Mat4 vp = camera(0.5f, 0.5f, 6.5f, 1.0f, 0.9f);
        r.bind_rt(s.scene, &s.depth);
        r.clear(s.scene, 0.45f, 0.6f, 0.8f, 1);
        r.clear_depth(s.depth);
        r.viewport(256, 256);
        r.d->OMSetDepthStencilState(s.ds.get(), 0);
        s.pipe->bind(r);
        const float ground[4] = {0.6f, 0.6f, 0.65f, 1};
        s.pipe->draw(r, s.props->ground, vp, rr::Mat4::translate(0, -0.6f, 0), ground);
        for (const Obj& o : s.props->items(0.5f * static_cast<float>(f))) s.pipe->draw(r, *o.mesh, vp, o.model, o.color);
        r.d->OMSetDepthStencilState(nullptr, 0);
        const float params[4] = {0.5f * static_cast<float>(f), 0.8f, 1.4f, 0};
        r.d->UpdateSubresource(s.cb.get(), 0, nullptr, params, 0, 0);
        r.bind_rt(r.main);
        r.ps_cb(0, s.cb.get());
        r.ps_srv(0, s.scene_view.get());
        r.ps_srv(1, s.lut_view.get());
        r.ps_smp(0, s.linear.get());
        r.fullscreen(s.post_ps.get());
        ID3D10ShaderResourceView* none[2] = {};
        r.d->PSSetShaderResources(0, 2, none);
    };
    multi<S>(spec("scene", "post_chain_sobel_edges_and_3d_lut_grade").size(256, 256).frames(3, {0, 1, 2}).tolerance(scene_tol(12, 5e-4, 40)), body);
}};

const Registrar kStencilShadow{[] {
    single(spec("scene", "stencil_shadow_volumes_zpass_two_sided").size(256, 256).tolerance(scene_tol(24, 1e-3, 64)).tolerance_for("stencil", [] { rr::Tolerance t = rr::Tolerance::exact(); t.outlier_fraction = 2e-4; t.max_outliers = 16; t.name = "exact+16"; return t; }()), [](Rig& r, rr::CaptureSet& o) {
        r.begin(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        r.depth_target(r.depth_main, 256, 256, DXGI_FORMAT_D24_UNORM_S8_UINT);
        r.bind_rt(r.main, &r.depth_main);
        r.clear(r.main, 0.0f, 0.0f, 0.0f, 1);
        r.clear_depth(r.depth_main, 1.0f, 0);
        ObjPipe lit(r, "float4 main(VO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(extra.xyz))); return float4(color.rgb * l * 1.2, 1); }\n");
        ObjPipe ambient(r, "float4 main(VO i) : SV_Target { return float4(color.rgb * 0.15, 1); }\n");
        Com<ID3D10VertexShader> volume_vs = r.vs("cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\nstruct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
                                                 "struct VO { float4 p : SV_Position; float3 w : TEXCOORD1; };\nVO main(VI i) { VO o; float4 w = mul(model, float4(i.p, 1)); o.p = w; o.w = w.xyz; return o; }\n");
        Com<ID3D10InputLayout> volume_layout = r.layout(mesh_elements());
        Com<ID3D10GeometryShader> volume_gs = r.gs(
            "cbuffer Obj : register(b0) { float4x4 vp; float4x4 model; float4 color; float4 extra; };\n"
            "struct VO { float4 p : SV_Position; float3 w : TEXCOORD1; };\nstruct GO { float4 p : SV_Position; };\n"
            "bool toward(float3 a, float3 b, float3 c) { return dot(cross(b - a, c - a), extra.xyz) > 0; }\n"
            "[maxvertexcount(12)] void main(triangleadj VO v[6], inout TriangleStream<GO> s) { if (!toward(v[0].w, v[2].w, v[4].w)) return; GO o;\n"
            "  [unroll] for (int e = 0; e < 3; ++e) { int a = e * 2; int b = ((e + 1) % 3) * 2; int x = e * 2 + 1; if (!toward(v[a].w, v[x].w, v[b].w)) {\n"
            "    float3 pa = v[a].w, pb = v[b].w; o.p = mul(vp, float4(pa, 1)); s.Append(o); o.p = mul(vp, float4(pa - extra.xyz * 12, 1)); s.Append(o); o.p = mul(vp, float4(pb, 1)); s.Append(o); o.p = mul(vp, float4(pb - extra.xyz * 12, 1)); s.Append(o); s.RestartStrip(); } } }\n");
        Com<ID3D10PixelShader> none_ps;
        MeshBuffers ground = upload(r, Geo::box(5.0f, 0.1f, 5.0f));
        MeshBuffers ball = upload(r, Geo::sphere(14, 20, 0.7f));
        const MeshData torus_data = Geo::torus(16, 8, 0.8f, 0.3f);
        MeshBuffers torus;
        torus.vertices = r.vbuf(torus_data.vertices.data(), static_cast<UINT>(torus_data.vertices.size() * sizeof(MeshVertex)));
        const std::vector<std::uint32_t> adj = adjacency(torus_data);
        torus.indices = r.ibuf32(adj);
        torus.count = static_cast<UINT>(adj.size());
        MeshBuffers torus_plain = upload(r, torus_data);
        auto base = Rig::depth_desc();
        base.DepthEnable = TRUE;
        base.DepthFunc = D3D10_COMPARISON_LESS_EQUAL;
        Com<ID3D10DepthStencilState> normal = r.depth(base);
        auto vol = base;
        vol.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ZERO;
        vol.StencilEnable = TRUE;
        vol.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_INCR, D3D10_COMPARISON_ALWAYS};
        vol.BackFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_DECR, D3D10_COMPARISON_ALWAYS};
        Com<ID3D10DepthStencilState> volume_state = r.depth(vol);
        auto lit_state_desc = base;
        lit_state_desc.StencilEnable = TRUE;
        lit_state_desc.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_EQUAL};
        lit_state_desc.BackFace = lit_state_desc.FrontFace;
        Com<ID3D10DepthStencilState> lit_state = r.depth(lit_state_desc);
        auto no_color = Rig::blend_desc();
        for (int i = 0; i < 8; ++i) no_color.RenderTargetWriteMask[i] = 0;
        Com<ID3D10BlendState> masked = r.blend(no_color);
        auto add = Rig::blend_desc();
        add.BlendEnable[0] = TRUE;
        add.SrcBlend = add.DestBlend = D3D10_BLEND_ONE;
        add.SrcBlendAlpha = add.DestBlendAlpha = D3D10_BLEND_ONE;
        Com<ID3D10BlendState> additive = r.blend(add);
        if (!r.ok()) return;
        r.no_cull();
        const rr::Mat4 vp = camera(0.5f, 0.7f, 8.0f, 1.0f, 0.8f);
        const V3 light = norm3({0.6f, 1.0f, 0.4f});
        const float ldir[4] = {light.x, light.y, light.z, 0};
        const float bf[4] = {1, 1, 1, 1};
        const float ground_color[4] = {0.7f, 0.7f, 0.75f, 1}, ball_color[4] = {0.9f, 0.4f, 0.3f, 1}, torus_color[4] = {0.3f, 0.8f, 0.5f, 1};
        const rr::Mat4 ground_model = rr::Mat4::translate(0, -0.9f, 0);
        const rr::Mat4 ball_model = rr::Mat4::translate(-1.6f, -0.1f, 0.8f);
        const rr::Mat4 torus_model = rr::Mat4::translate(0.8f, 0.5f, -0.3f) * rr::Mat4::rotate_x(0.8f) * rr::Mat4::rotate_y(0.5f);
        r.d->OMSetDepthStencilState(normal.get(), 0);
        ambient.bind(r);
        ambient.draw(r, ground, vp, ground_model, ground_color);
        ambient.draw(r, ball, vp, ball_model, ball_color);
        ambient.draw(r, torus_plain, vp, torus_model, torus_color);
        r.d->OMSetBlendState(masked.get(), bf, 0xffffffffu);
        r.d->OMSetDepthStencilState(volume_state.get(), 0);
        r.d->IASetInputLayout(volume_layout.get());
        r.d->VSSetShader(volume_vs.get());
        r.d->GSSetShader(volume_gs.get());
        r.d->PSSetShader(nullptr);
        r.gs_cb(0, ambient.cb.get());
        r.vs_cb(0, ambient.cb.get());
        ObjectConstants c{};
        std::memcpy(c.vp, vp.m, 64);
        std::memcpy(c.model, torus_model.m, 64);
        std::memcpy(c.extra, ldir, 16);
        r.d->UpdateSubresource(ambient.cb.get(), 0, nullptr, &c, 0, 0);
        const UINT stride = sizeof(MeshVertex), offset = 0;
        ID3D10Buffer* vb = torus.vertices.get();
        r.d->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        r.d->IASetIndexBuffer(torus.indices.get(), DXGI_FORMAT_R32_UINT, 0);
        r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ);
        r.d->DrawIndexed(torus.count, 0, 0);
        r.d->GSSetShader(nullptr);
        r.d->OMSetBlendState(additive.get(), bf, 0xffffffffu);
        r.d->OMSetDepthStencilState(lit_state.get(), 0);
        lit.bind(r);
        lit.draw(r, ground, vp, ground_model, ground_color, ldir);
        lit.draw(r, ball, vp, ball_model, ball_color, ldir);
        r.d->OMSetBlendState(nullptr, bf, 0xffffffffu);
        r.cap_main(o);
        r.cap_stencil(o, "stencil", r.depth_main);
    });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
