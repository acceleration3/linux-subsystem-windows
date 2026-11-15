#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

const Layout kScene{D3DFMT_A8R8G8B8, D3DFMT_D24S8, 256, true};

rr::CaseSpec scene(const char* name, unsigned frames, std::vector<unsigned> captured, rr::Tolerance tol = rr::Tolerance::blend(3)) {
    return rr::CaseSpec("scene", name).size(256, 256).frames(frames, std::move(captured)).tolerance(std::move(tol));
}

void look(Ctx& c, const float eye[3], const float at[3], float fov = 1.0f) {
    const float up[3] = {0, 1, 0};
    c.xf(D3DTS_VIEW, Xf::look_at(eye, at, up));
    c.xf(D3DTS_PROJECTION, Xf::perspective(fov, 1.0f, 0.3f, 80.0f));
}

void upload(Ctx& c, UINT reg, const Xf& m) {
    float t[16];
    for (int r = 0; r < 4; ++r) {
        for (int k = 0; k < 4; ++k) {
            t[k * 4 + r] = m.m[r][k];
        }
    }
    c.chk(c.d->SetVertexShaderConstantF(reg, t, 4), "SetVertexShaderConstantF");
}

void fullscreen(Ctx& c, float w, float h, DWORD color = 0xFFFFFFFF) {
    const std::vector<TlVtx> q = Geo::tl_quad(0, 0, w, h, color);
    c.fvf(kTlFvf);
    c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
}

void linear(Ctx& c, DWORD s, bool wrap = false) {
    c.ss(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    c.ss(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    c.ss(s, D3DSAMP_ADDRESSU, wrap ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
    c.ss(s, D3DSAMP_ADDRESSV, wrap ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
}

const char* const kMeshVs =
    "float4 cm[4] : register(c0);\nfloat4 wm[4] : register(c4);\nfloat4 lightdir : register(c8);\nfloat4 gain : register(c9);\n"
    "struct I { float3 p : POSITION; float3 n : NORMAL; float4 c : COLOR0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; float4 wp : TEXCOORD0; float3 n : TEXCOORD1; };\n"
    "O main(I i) { O o; float4 p = float4(i.p, 1.0); float4 w = float4(dot(p, wm[0]), dot(p, wm[1]), dot(p, wm[2]), 1.0);\n"
    "o.p = float4(dot(w, cm[0]), dot(w, cm[1]), dot(w, cm[2]), dot(w, cm[3])); o.wp = o.p;\n"
    "float3 n = float3(dot(float4(i.n, 0), wm[0]), dot(float4(i.n, 0), wm[1]), dot(float4(i.n, 0), wm[2])); o.n = n;\n"
    "float d = max(dot(normalize(n), -lightdir.xyz), 0.0); o.c = i.c * (0.15 + d) * gain.x; return o; }\n";

void mesh_constants(Ctx& c, const Xf& world, const Xf& view_proj, float gain, const float light[3]) {
    upload(c, 0, world * view_proj);
    upload(c, 4, world);
    const float l[4] = {light[0], light[1], light[2], 0};
    const float g[4] = {gain, 0, 0, 0};
    c.chk(c.d->SetVertexShaderConstantF(8, l, 1), "const");
    c.chk(c.d->SetVertexShaderConstantF(9, g, 1), "const");
}

void glow_scene(Ctx& c, unsigned f, bool shader_hdr, const Com<IDirect3DVertexShader9>& vs, const Com<IDirect3DPixelShader9>& ps) {
    c.clear(Dx::kColor(12, 14, 28));
    c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
    c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
    const float eye[3] = {0, 1.5f, -6.0f};
    const float at[3] = {0, 0, 0};
    const float up[3] = {0, 1, 0};
    const Xf vp = Xf::look_at(eye, at, up) * Xf::perspective(0.9f, 1.0f, 0.3f, 50.0f);
    const float light[3] = {0.3f, -0.6f, 0.7f};
    c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
    c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
    c.fvf(kVtxFvf);
    const float a = 0.5f * static_cast<float>(f);
    const DWORD colors[3] = {0xFFFF5030, 0xFF40FF70, 0xFF5080FF};
    for (int i = 0; i < 3; ++i) {
        mesh_constants(c, Xf::translate(-2.2f + 2.2f * static_cast<float>(i), 0.4f * std::sin(a + static_cast<float>(i)), 0), vp, shader_hdr ? 5.0f : 3.0f, light);
        c.draw_mesh(Geo::sphere(24, 16, 0.9f, colors[i]));
    }
    mesh_constants(c, Xf::translate(0, -1.2f, 0), vp, 1.0f, light);
    c.draw_mesh(Geo::grid(1, 4.0f, 0.0f, 0xFF506070));
}

void post_scenes() {
    Cases::frames(scene("bloom_postprocess_ps20_chain", 3, {0, 2}, rr::Tolerance::blend(5)), [] {
        struct State {
            Rtt scene_rt, half_a, half_b;
            Com<IDirect3DVertexShader9> vs;
            Com<IDirect3DPixelShader9> ps, bright, blur;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                if (!RttOps::make(c, st->scene_rt, 256, 256, D3DFMT_A8R8G8B8) || !RttOps::make(c, st->half_a, 128, 128, D3DFMT_A8R8G8B8, false) ||
                    !RttOps::make(c, st->half_b, 128, 128, D3DFMT_A8R8G8B8, false)) {
                    return;
                }
                st->vs = c.vs(kMeshVs, "vs_2_0");
                st->ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return float4(c.rgb, 1.0); }\n", "ps_2_0");
                st->bright = c.ps("sampler2D s : register(s0);\nfloat4 main(float2 uv : TEXCOORD0) : COLOR { float4 t = tex2D(s, uv); float l = dot(t.rgb, float3(0.3, 0.59, 0.11)); return float4(t.rgb * saturate((l - 0.55) * 3.0), 1.0); }\n", "ps_2_0");
                st->blur = c.ps("sampler2D s : register(s0);\nfloat4 dir : register(c0);\nfloat4 main(float2 uv : TEXCOORD0) : COLOR { float4 a = tex2D(s, uv) * 0.30; a += (tex2D(s, uv + dir.xy) + tex2D(s, uv - dir.xy)) * 0.22; a += (tex2D(s, uv + dir.xy * 2) + tex2D(s, uv - dir.xy * 2)) * 0.13; return a; }\n", "ps_2_0");
            }
            if (!st->scene_rt.tex || !st->vs || !st->ps || !st->bright || !st->blur) {
                return;
            }
            RttOps::push(c, st->scene_rt);
            glow_scene(c, f, false, st->vs, st->ps);
            c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
            RttOps::push(c, st->half_a);
            c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
            c.tex(0, st->scene_rt.tex.get());
            linear(c, 0);
            c.chk(c.d->SetPixelShader(st->bright.get()), "SetPixelShader");
            fullscreen(c, 128, 128);
            const float h[4] = {1.5f / 128.0f, 0, 0, 0};
            const float v[4] = {0, 1.5f / 128.0f, 0, 0};
            c.chk(c.d->SetPixelShader(st->blur.get()), "SetPixelShader");
            RttOps::push(c, st->half_b);
            c.tex(0, st->half_a.tex.get());
            c.chk(c.d->SetPixelShaderConstantF(0, h, 1), "const");
            fullscreen(c, 128, 128);
            RttOps::push(c, st->half_a);
            c.tex(0, st->half_b.tex.get());
            c.chk(c.d->SetPixelShaderConstantF(0, v, 1), "const");
            fullscreen(c, 128, 128);
            RttOps::pop(c);
            c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
            c.tex(0, st->scene_rt.tex.get());
            fullscreen(c, 256, 256);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_ONE);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_ONE);
            c.tex(0, st->half_a.tex.get());
            fullscreen(c, 256, 256);
            c.rs(D3DRS_ALPHABLENDENABLE, FALSE);
            c.tex(0, nullptr);
        });
    }, kScene, Needs::all({Needs::shader(2, 2), Needs::format(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8, "render target textures")}));

    Cases::frames(scene("hdr_fp16_tonemap_ps30", 3, {0, 2}, rr::Tolerance::blend(4)), [] {
        struct State {
            Rtt hdr;
            Com<IDirect3DVertexShader9> vs;
            Com<IDirect3DPixelShader9> ps, tone;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                if (!RttOps::make(c, st->hdr, 256, 256, D3DFMT_A16B16G16R16F)) {
                    return;
                }
                st->vs = c.vs(kMeshVs, "vs_3_0");
                st->ps = c.ps("float4 main(float4 c : COLOR0, float4 wp : TEXCOORD0, float3 n : TEXCOORD1) : COLOR { return float4(c.rgb, 1.0); }\n", "ps_3_0");
                st->tone = c.ps("sampler2D s : register(s0);\nfloat4 e : register(c0);\nfloat4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR { float3 x = tex2D(s, uv).rgb * e.x; return float4(pow(x / (1.0 + x), 1.0 / 2.2), 1.0); }\n", "ps_3_0");
            }
            if (!st->hdr.tex || !st->vs || !st->ps || !st->tone) {
                return;
            }
            RttOps::push(c, st->hdr);
            glow_scene(c, f, true, st->vs, st->ps);
            RttOps::pop(c);
            Com<IDirect3DVertexShader9> pass = c.vs("struct I { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\nO main(I i) { O o; o.p = i.p; o.c = i.c; o.t = i.t; return o; }\n", "vs_3_0");
            if (!pass) {
                return;
            }
            c.clear(Dx::kColor(0, 0, 0));
            c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
            c.tex(0, st->hdr.tex.get());
            c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            const float exposure[4] = {0.5f + 0.4f * static_cast<float>(f), 0, 0, 0};
            c.chk(c.d->SetPixelShaderConstantF(0, exposure, 1), "const");
            c.chk(c.d->SetVertexShader(pass.get()), "SetVertexShader");
            c.chk(c.d->SetPixelShader(st->tone.get()), "SetPixelShader");
            struct V { float x, y, z; DWORD color; float u, v; };
            const V q[4] = {{-1, 1, 0.5f, 0xFFFFFFFF, 0, 0}, {1, 1, 0.5f, 0xFFFFFFFF, 1, 0}, {-1, -1, 0.5f, 0xFFFFFFFF, 0, 1}, {1, -1, 0.5f, 0xFFFFFFFF, 1, 1}};
            c.fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
            c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
            c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
            c.tex(0, nullptr);
        });
    }, kScene, Needs::all({Needs::shader(3, 3), Needs::format(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F, "FP16 render target textures")}));

    Cases::frames(scene("shadow_map_intz_depth_texture", 3, {0, 2}, rr::Tolerance::blend(6).with_edge(1)), [] {
        struct State {
            Com<IDirect3DTexture9> depth;
            Com<IDirect3DSurface9> depth_surface;
            Rtt dummy;
            Com<IDirect3DVertexShader9> vs;
            Com<IDirect3DPixelShader9> ps;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            constexpr D3DFORMAT kIntz = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));
            if (f == 0) {
                c.chk(c.d->CreateTexture(256, 256, 1, D3DUSAGE_DEPTHSTENCIL, kIntz, D3DPOOL_DEFAULT, st->depth.put(), nullptr), "CreateTexture INTZ");
                if (!st->depth || !RttOps::make(c, st->dummy, 256, 256, D3DFMT_A8R8G8B8, false)) {
                    return;
                }
                st->depth->GetSurfaceLevel(0, st->depth_surface.put());
                st->vs = c.vs(
                    "float4 cm[4] : register(c0);\nfloat4 lm[4] : register(c4);\nfloat4 wm[4] : register(c8);\nstruct I { float3 p : POSITION; float4 c : COLOR0; };\n"
                    "struct O { float4 p : POSITION; float4 c : COLOR0; float4 lp : TEXCOORD0; };\nO main(I i) { O o; float4 p = float4(i.p, 1.0); float4 w = float4(dot(p, wm[0]), dot(p, wm[1]), dot(p, wm[2]), 1.0);\n"
                    "o.p = float4(dot(w, cm[0]), dot(w, cm[1]), dot(w, cm[2]), dot(w, cm[3])); o.lp = float4(dot(w, lm[0]), dot(w, lm[1]), dot(w, lm[2]), dot(w, lm[3])); o.c = i.c; return o; }\n", "vs_3_0");
                st->ps = c.ps(
                    "sampler2D sm : register(s0);\nfloat4 main(float4 c : COLOR0, float4 lp : TEXCOORD0) : COLOR { float2 uv = lp.xy / lp.w * float2(0.5, -0.5) + 0.5;\n"
                    "float d = tex2D(sm, uv).r; float z = lp.z / lp.w; float lit = (z - 0.003 > d) ? 0.35 : 1.0; return float4(c.rgb * lit, 1.0); }\n", "ps_3_0");
            }
            if (!st->depth || !st->vs || !st->ps) {
                return;
            }
            const float leye[3] = {3.0f * std::sin(0.5f * static_cast<float>(f)), 7.0f, -3.0f};
            const float lat[3] = {0, 0, 0};
            const float lup[3] = {0, 0, 1};
            const Xf light_vp = Xf::look_at(leye, lat, lup) * Xf::perspective(0.9f, 1.0f, 1.0f, 20.0f);
            const float eye[3] = {0, 5.0f, -8.0f};
            const float at[3] = {0, 0, 0};
            const float up[3] = {0, 1, 0};
            const Xf cam_vp = Xf::look_at(eye, at, up) * Xf::perspective(0.9f, 1.0f, 0.5f, 40.0f);
            IDirect3DVertexShader9* vs = st->vs.get();
            c.chk(c.d->SetRenderTarget(0, st->dummy.color.get()), "SetRenderTarget");
            c.chk(c.d->SetDepthStencilSurface(st->depth_surface.get()), "SetDepthStencilSurface");
            c.clear(Dx::kColor(0, 0, 0), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 1.0f);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_NONE);
            c.rs(D3DRS_COLORWRITEENABLE, 0);
            c.fvf(kVtxFvf);
            Com<IDirect3DPixelShader9> depth_only = c.ps("float4 main() : COLOR { return 0; }\n", "ps_3_0");
            c.chk(c.d->SetPixelShader(depth_only.get()), "SetPixelShader");
            c.chk(c.d->SetVertexShader(vs), "SetVertexShader");
            const auto draw_all = [&](const Xf& vp, const Xf& lvp) {
                upload(c, 0, vp);
                upload(c, 4, lvp);
                upload(c, 8, Xf());
                c.draw_mesh(Geo::grid(1, 5.0f, 0.0f, 0xFFB0B8A8));
                for (int i = 0; i < 3; ++i) {
                    const Xf w = Xf::translate(-1.8f + 1.8f * static_cast<float>(i), 0.8f + 0.4f * static_cast<float>(i), 0.5f * static_cast<float>(i) - 0.5f);
                    upload(c, 8, w);
                    c.draw_mesh(Geo::cube(0.6f, 0xFFD07050));
                }
            };
            draw_all(light_vp, light_vp);
            c.rs(D3DRS_COLORWRITEENABLE, 15);
            RttOps::pop(c);
            c.clear(Dx::kColor(100, 120, 160));
            c.rs(D3DRS_CULLMODE, D3DCULL_NONE);
            c.tex(0, st->depth.get());
            c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            c.chk(c.d->SetPixelShader(st->ps.get()), "SetPixelShader");
            draw_all(cam_vp, light_vp);
            c.tex(0, nullptr);
            c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
            c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
        });
    }, kScene, Needs::all({Needs::shader(3, 3), Needs::format(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z')), "INTZ depth textures")}));

    Cases::frames(scene("deferred_lighting_mrt_ps30", 3, {0, 2}, rr::Tolerance::blend(6)), [] {
        struct State {
            Rtt albedo, normal, depth;
            Com<IDirect3DSurface9> zbuf;
            Com<IDirect3DVertexShader9> gvs, pass;
            Com<IDirect3DPixelShader9> gps, lps;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                if (!RttOps::make(c, st->albedo, 256, 256, D3DFMT_A8R8G8B8, false) || !RttOps::make(c, st->normal, 256, 256, D3DFMT_A8R8G8B8, false) ||
                    !RttOps::make(c, st->depth, 256, 256, D3DFMT_R32F, false)) {
                    return;
                }
                c.chk(c.d->CreateDepthStencilSurface(256, 256, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, st->zbuf.put(), nullptr), "CreateDepthStencilSurface");
                st->gvs = c.vs(
                    "float4 cm[4] : register(c0);\nfloat4 wm[4] : register(c4);\nstruct I { float3 p : POSITION; float3 n : NORMAL; float4 c : COLOR0; };\n"
                    "struct O { float4 p : POSITION; float4 c : COLOR0; float3 n : TEXCOORD0; float z : TEXCOORD1; };\nO main(I i) { O o; float4 p = float4(i.p, 1.0); float4 w = float4(dot(p, wm[0]), dot(p, wm[1]), dot(p, wm[2]), 1.0);\n"
                    "o.p = float4(dot(w, cm[0]), dot(w, cm[1]), dot(w, cm[2]), dot(w, cm[3])); o.c = i.c; o.n = float3(dot(float4(i.n, 0), wm[0]), dot(float4(i.n, 0), wm[1]), dot(float4(i.n, 0), wm[2])); o.z = o.p.w; return o; }\n", "vs_3_0");
                st->gps = c.ps(
                    "struct O { float4 a : COLOR0; float4 n : COLOR1; float4 d : COLOR2; };\nO main(float4 c : COLOR0, float3 n : TEXCOORD0, float z : TEXCOORD1) { O o; o.a = c; o.n = float4(normalize(n) * 0.5 + 0.5, 1); o.d = float4(z, 0, 0, 1); return o; }\n", "ps_3_0");
                st->pass = c.vs("struct I { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\nO main(I i) { O o; o.p = i.p; o.c = i.c; o.t = i.t; return o; }\n", "vs_3_0");
                st->lps = c.ps(
                    "sampler2D ga : register(s0);\nsampler2D gn : register(s1);\nsampler2D gd : register(s2);\nfloat4 lpos : register(c0);\nfloat4 lcol : register(c1);\n"
                    "float4 main(float4 c : COLOR0, float2 uv : TEXCOORD0) : COLOR { float4 a = tex2D(ga, uv); float3 n = tex2D(gn, uv).xyz * 2 - 1; float z = tex2D(gd, uv).x;\n"
                    "float3 vp = float3((uv.x * 2 - 1) * z * 0.5463, (1 - uv.y * 2) * z * 0.5463, z); float3 l = lpos.xyz - vp; float d = length(l); float att = saturate(1 - d / lpos.w);\n"
                    "float ndl = saturate(dot(n, l / d)); return float4(a.rgb * lcol.rgb * ndl * att * att * 2.0, 1); }\n", "ps_3_0");
            }
            if (!st->albedo.tex || !st->gvs || !st->gps || !st->lps || !st->pass) {
                return;
            }
            const float eye[3] = {0, 1.0f, -6.0f};
            const float at[3] = {0, 0, 0};
            const float up[3] = {0, 1, 0};
            const Xf view = Xf::look_at(eye, at, up);
            const Xf vp = view * Xf::perspective(1.0f, 1.0f, 0.5f, 40.0f);
            c.chk(c.d->SetRenderTarget(0, st->albedo.color.get()), "SetRenderTarget0");
            c.chk(c.d->SetRenderTarget(1, st->normal.color.get()), "SetRenderTarget1");
            c.chk(c.d->SetRenderTarget(2, st->depth.color.get()), "SetRenderTarget2");
            c.chk(c.d->SetDepthStencilSurface(st->zbuf.get()), "SetDepthStencilSurface");
            c.clear(Dx::kColor(0, 0, 0, 255), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 1.0f);
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
            c.chk(c.d->SetVertexShader(st->gvs.get()), "SetVertexShader");
            c.chk(c.d->SetPixelShader(st->gps.get()), "SetPixelShader");
            c.fvf(kVtxFvf);
            upload(c, 0, vp);
            upload(c, 4, Xf());
            c.draw_mesh(Geo::grid(1, 5.0f, 0.0f, 0xFFB0B0B0));
            for (int i = 0; i < 3; ++i) {
                upload(c, 0, Xf::translate(-2.0f + 2.0f * static_cast<float>(i), 0.9f, 0.5f * static_cast<float>(i)) * vp);
                upload(c, 4, Xf::translate(-2.0f + 2.0f * static_cast<float>(i), 0.9f, 0.5f * static_cast<float>(i)));
                c.draw_mesh(Geo::sphere(20, 14, 0.8f, i == 0 ? 0xFFE06050 : i == 1 ? 0xFF50E060 : 0xFF5070E0));
            }
            c.chk(c.d->SetRenderTarget(1, nullptr), "SetRenderTarget1");
            c.chk(c.d->SetRenderTarget(2, nullptr), "SetRenderTarget2");
            RttOps::pop(c);
            c.clear(Dx::kColor(0, 0, 0, 255), D3DCLEAR_TARGET);
            c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_ONE);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_ONE);
            c.tex(0, st->albedo.tex.get());
            c.tex(1, st->normal.tex.get());
            c.tex(2, st->depth.tex.get());
            for (DWORD s = 0; s < 3; ++s) {
                c.ss(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                c.ss(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            }
            c.chk(c.d->SetVertexShader(st->pass.get()), "SetVertexShader");
            c.chk(c.d->SetPixelShader(st->lps.get()), "SetPixelShader");
            struct V { float x, y, z; DWORD color; float u, v; };
            const V q[4] = {{-1, 1, 0.5f, 0xFFFFFFFF, 0, 0}, {1, 1, 0.5f, 0xFFFFFFFF, 1, 0}, {-1, -1, 0.5f, 0xFFFFFFFF, 0, 1}, {1, -1, 0.5f, 0xFFFFFFFF, 1, 1}};
            c.fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            const float a = 0.8f * static_cast<float>(f);
            const float lights[3][8] = {{-2.5f + std::sin(a), 1.5f, 1.0f, 5.0f, 1.0f, 0.6f, 0.4f, 0}, {2.5f, 2.0f + std::cos(a), 0.5f, 5.0f, 0.4f, 0.8f, 1.0f, 0}, {0.0f, 3.0f, -1.0f, 6.0f, 0.9f, 0.9f, 0.7f, 0}};
            for (const auto& l : lights) {
                float pos[4];
                pos[0] = l[0] * view.m[0][0] + l[1] * view.m[1][0] + l[2] * view.m[2][0] + view.m[3][0];
                pos[1] = l[0] * view.m[0][1] + l[1] * view.m[1][1] + l[2] * view.m[2][1] + view.m[3][1];
                pos[2] = l[0] * view.m[0][2] + l[1] * view.m[1][2] + l[2] * view.m[2][2] + view.m[3][2];
                pos[3] = l[3];
                const float col[4] = {l[4], l[5], l[6], 1};
                c.chk(c.d->SetPixelShaderConstantF(0, pos, 1), "const");
                c.chk(c.d->SetPixelShaderConstantF(1, col, 1), "const");
                c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
            }
            c.rs(D3DRS_ALPHABLENDENABLE, FALSE);
            c.tex(0, nullptr);
            c.tex(1, nullptr);
            c.tex(2, nullptr);
            c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
            c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
        });
    }, kScene, Needs::all({Needs::shader(3, 3), Needs::caps([](const D3DCAPS9& caps) { return caps.NumSimultaneousRTs >= 3; }, "three render targets"),
                           Needs::format(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F, "R32F render targets")}));

    Cases::frames(scene("water_reflection_clip_plane", 3, {0, 2}, rr::Tolerance::filter(8, 0.006)), [] {
        struct State {
            Rtt reflect;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0 && !RttOps::make(c, st->reflect, 128, 128, D3DFMT_A8R8G8B8)) {
                return;
            }
            if (!st->reflect.tex) {
                return;
            }
            const float eye[3] = {0, 2.5f, -7.0f};
            const float at[3] = {0, 0.5f, 0};
            const float up[3] = {0, 1, 0};
            const Xf view = Xf::look_at(eye, at, up);
            const Xf proj = Xf::perspective(0.9f, 1.0f, 0.5f, 60.0f);
            const auto world = [&](Ctx& k, bool mirror) {
                k.fvf(kVtxFvf);
                k.rs(D3DRS_CULLMODE, mirror ? D3DCULL_CW : D3DCULL_CCW);
                k.rs(D3DRS_ZENABLE, D3DZB_TRUE);
                for (int i = 0; i < 3; ++i) {
                    k.xf(D3DTS_WORLD, Xf::rot_y(0.4f * static_cast<float>(i) + 0.3f * static_cast<float>(f)) * Xf::translate(-2.0f + 2.0f * static_cast<float>(i), 1.0f, 1.0f + static_cast<float>(i % 2)));
                    Mesh cube = Geo::cube(0.8f, 0xFFFFFFFF);
                    const DWORD cols[6] = {0xFFE04040, 0xFF40E040, 0xFF4040E0, 0xFFE0E040, 0xFFE040E0, 0xFF40E0E0};
                    for (std::size_t q = 0; q < cube.vertices.size(); ++q) {
                        cube.vertices[q].diffuse = cols[q / 4];
                    }
                    k.draw_mesh(cube);
                }
            };
            RttOps::push(c, st->reflect);
            c.clear(Dx::kColor(120, 150, 210));
            c.xf(D3DTS_VIEW, view.mirror_y());
            c.xf(D3DTS_PROJECTION, proj);
            const float plane[4] = {0, 1, 0, 0};
            c.chk(c.d->SetClipPlane(0, plane), "SetClipPlane");
            c.rs(D3DRS_CLIPPLANEENABLE, 1);
            world(c, true);
            c.rs(D3DRS_CLIPPLANEENABLE, 0);
            RttOps::pop(c);
            c.clear(Dx::kColor(120, 150, 210));
            c.xf(D3DTS_VIEW, view);
            world(c, false);
            c.xf(D3DTS_WORLD, Xf());
            Xf bias;
            bias.m[0][0] = 0.5f;
            bias.m[1][1] = -0.5f;
            bias.m[3][0] = 0.5f;
            bias.m[3][1] = 0.5f;
            c.tex(0, st->reflect.tex.get());
            linear(c, 0);
            c.tss(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);
            c.xf(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0), view.rigid_inverse() * view.mirror_y() * proj * bias);
            c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
            c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            c.rs(D3DRS_CULLMODE, D3DCULL_NONE);
            c.draw_mesh(Geo::grid(1, 5.0f, 0.0f, 0xB0FFFFFF));
            c.rs(D3DRS_ALPHABLENDENABLE, FALSE);
            c.tss(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            c.tss(0, D3DTSS_TEXCOORDINDEX, 0);
            c.tex(0, nullptr);
        });
    }, kScene, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxUserClipPlanes >= 1; }, "user clip planes"));

    Cases::frames(scene("render_to_texture_minimap", 6, {0, 3, 5}, rr::Tolerance::blend(3)), [] {
        struct State {
            Rtt mini;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0 && !RttOps::make(c, st->mini, 64, 64, D3DFMT_A8R8G8B8)) {
                return;
            }
            if (!st->mini.tex) {
                return;
            }
            const float px = -3.0f + 1.2f * static_cast<float>(f);
            const float pz = 2.0f * std::sin(0.8f * static_cast<float>(f));
            const auto world = [&](Ctx& k) {
                k.fvf(kVtxFvf);
                k.rs(D3DRS_ZENABLE, D3DZB_TRUE);
                k.rs(D3DRS_CULLMODE, D3DCULL_CCW);
                k.xf(D3DTS_WORLD, Xf());
                k.draw_mesh(Geo::grid(10, 5.0f, 0.4f, 0xFF78A068));
                for (int i = 0; i < 5; ++i) {
                    k.xf(D3DTS_WORLD, Xf::translate(-3.0f + 1.5f * static_cast<float>(i), 0.9f, 2.0f - 1.2f * static_cast<float>(i % 3)));
                    k.draw_mesh(Geo::cube(0.5f, 0xFFB08060));
                }
                k.xf(D3DTS_WORLD, Xf::translate(px, 1.0f, pz));
                k.draw_mesh(Geo::sphere(12, 8, 0.4f, 0xFFFF4040));
            };
            RttOps::push(c, st->mini);
            c.clear(Dx::kColor(30, 40, 30));
            const float meye[3] = {0, 12.0f, 0.01f};
            const float mat[3] = {0, 0, 0};
            const float mup[3] = {0, 0, 1};
            c.xf(D3DTS_VIEW, Xf::look_at(meye, mat, mup));
            c.xf(D3DTS_PROJECTION, Xf::ortho(12.0f, 12.0f, 1.0f, 30.0f));
            world(c);
            RttOps::pop(c);
            c.clear(Dx::kColor(110, 140, 200));
            const float eye[3] = {px - 2.0f, 3.0f, pz - 6.0f};
            const float at[3] = {px, 0.5f, pz};
            look(c, eye, at);
            world(c);
            c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
            c.tex(0, st->mini.tex.get());
            linear(c, 0);
            const std::vector<TlVtx> q = Geo::tl_quad(176, 12, 244, 80, 0xFFFFFFFF);
            c.fvf(kTlFvf);
            c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
            c.tex(0, nullptr);
            c.quad(174, 10, 246, 12, 0.1f, 0xFFFFFFFF);
            c.quad(174, 80, 246, 82, 0.1f, 0xFFFFFFFF);
        });
    }, kScene, Needs::format(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8, "render target textures"));

    Cases::frames(scene("feedback_ping_pong_trails", 8, {0, 4, 7}, rr::Tolerance::blend(3)), [] {
        struct State {
            Rtt a, b;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            if (f == 0) {
                if (!RttOps::make(c, st->a, 256, 256, D3DFMT_A8R8G8B8, false) || !RttOps::make(c, st->b, 256, 256, D3DFMT_A8R8G8B8, false)) {
                    return;
                }
                RttOps::push(c, st->a);
                c.clear(Dx::kColor(0, 0, 0));
                RttOps::push(c, st->b);
                c.clear(Dx::kColor(0, 0, 0));
                RttOps::pop(c);
            }
            if (!st->a.tex) {
                return;
            }
            Rtt& src = (f % 2 == 0) ? st->a : st->b;
            Rtt& dst = (f % 2 == 0) ? st->b : st->a;
            RttOps::push(c, dst);
            c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
            c.tex(0, src.tex.get());
            linear(c, 0);
            const std::vector<TlVtx> q = Geo::tl_quad(3, 2, 259, 258, 0xFFE8E8E8);
            c.fvf(kTlFvf);
            c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
            c.tex(0, nullptr);
            const float a = 0.8f * static_cast<float>(f);
            const float x = 128 + 90 * std::cos(a);
            const float y = 128 + 90 * std::sin(a);
            c.quad(x - 10, y - 10, x + 10, y + 10, 0.5f, 0xFFFF9030, 0xFFFFE030, 0xFF30A0FF, 0xFFFF30A0);
            RttOps::pop(c);
            c.tex(0, dst.tex.get());
            linear(c, 0);
            c.clear(Dx::kColor(0, 0, 0));
            fullscreen(c, 256, 256);
            c.tex(0, nullptr);
        });
    }, kScene, Needs::format(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8, "render target textures"));
}

const Registrar kPost(post_scenes);

}  // namespace

}  // namespace lsw::suites::render::d3d9
