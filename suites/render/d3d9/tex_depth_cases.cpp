#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

constexpr D3DFORMAT fourcc(char a, char b, char c, char d) { return static_cast<D3DFORMAT>(MAKEFOURCC(a, b, c, d)); }

void depth_quads(Ctx& c) {
    c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
    c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
    c.quad(0, 0, 128, 128, 0.9f, 0xFF404040);
    c.quad(10, 10, 80, 80, 0.2f, 0xFFFFFFFF);
    c.quad(50, 40, 120, 120, 0.5f, 0xFFFFFFFF);
    c.quad(20, 90, 100, 124, 0.7f, 0xFFFFFFFF);
}

void depth_texture_rows() {
    struct Row {
        const char* name;
        D3DFORMAT format;
        bool null_target;
    };
    const Row rows[] = {{"depth_texture_intz", fourcc('I', 'N', 'T', 'Z'), false}, {"depth_texture_df24", fourcc('D', 'F', '2', '4'), false}, {"depth_texture_rawz", fourcc('R', 'A', 'W', 'Z'), false},
                        {"depth_only_null_target_intz", fourcc('I', 'N', 'T', 'Z'), true}, {"depth_only_null_target_df24", fourcc('D', 'F', '2', '4'), true}};
    for (const Row& r : rows) {
        Need need = Needs::all({Needs::format(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, r.format, std::string("depth texture format ") + r.name),
                                r.null_target ? Needs::format(D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, fourcc('N', 'U', 'L', 'L'), "NULL render target format") : Need(), Needs::shader(0, 2)});
        Cases::single(rr::CaseSpec("tex", r.name).tolerance(rr::Tolerance::blend(4)), [r](Ctx& c) {
            Com<IDirect3DTexture9> depth;
            c.chk(c.d->CreateTexture(128, 128, 1, D3DUSAGE_DEPTHSTENCIL, r.format, D3DPOOL_DEFAULT, depth.put(), nullptr), "CreateTexture depth");
            Com<IDirect3DSurface9> color;
            c.chk(c.d->CreateRenderTarget(128, 128, r.null_target ? fourcc('N', 'U', 'L', 'L') : D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, color.put(), nullptr), "CreateRenderTarget");
            if (!depth || !color) {
                return;
            }
            Com<IDirect3DSurface9> ds;
            depth->GetSurfaceLevel(0, ds.put());
            c.chk(c.d->SetRenderTarget(0, color.get()), "SetRenderTarget");
            c.chk(c.d->SetDepthStencilSurface(ds.get()), "SetDepthStencilSurface");
            c.clear(Dx::kColor(0, 0, 0), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 1.0f);
            depth_quads(c);
            RttOps::pop(c);
            c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
            c.clear(Dx::kColor(0, 0, 0), D3DCLEAR_TARGET);
            Com<IDirect3DPixelShader9> ps = c.ps("sampler2D s : register(s0);\nfloat4 main(float2 uv : TEXCOORD0) : COLOR { float4 t = tex2D(s, uv); return float4(t.rgb, 1.0); }\n", "ps_2_0");
            if (!ps) {
                return;
            }
            c.tex(0, depth.get());
            c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
            const std::vector<TlVtx> q = Geo::tl_quad(0, 0, 128, 128, 0xFFFFFFFF);
            c.fvf(kTlFvf);
            c.draw(D3DPT_TRIANGLELIST, 2, q.data(), sizeof(TlVtx));
            c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
            c.tex(0, nullptr);
        }, {}, need);
    }

    Cases::single(rr::CaseSpec("tex", "shadow_compare_pcf_d24x8").tolerance(rr::Tolerance::blend(8).with_outliers(0.01)), [](Ctx& c) {
        Com<IDirect3DTexture9> depth;
        c.chk(c.d->CreateTexture(128, 128, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24X8, D3DPOOL_DEFAULT, depth.put(), nullptr), "CreateTexture depth");
        Com<IDirect3DSurface9> color;
        c.chk(c.d->CreateRenderTarget(128, 128, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, color.put(), nullptr), "CreateRenderTarget");
        if (!depth || !color) {
            return;
        }
        Com<IDirect3DSurface9> ds;
        depth->GetSurfaceLevel(0, ds.put());
        c.chk(c.d->SetRenderTarget(0, color.get()), "SetRenderTarget");
        c.chk(c.d->SetDepthStencilSurface(ds.get()), "SetDepthStencilSurface");
        c.clear(Dx::kColor(0, 0, 0), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 1.0f);
        depth_quads(c);
        RttOps::pop(c);
        c.rs(D3DRS_ZENABLE, D3DZB_FALSE);
        c.clear(Dx::kColor(0, 0, 0), D3DCLEAR_TARGET);
        Com<IDirect3DPixelShader9> ps = c.ps("sampler2D s : register(s0);\nfloat4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR { float lit = tex2Dproj(s, float4(uv, uv.x * 0.9 + 0.05, 1.0)).r; return float4(lit, lit * 0.8, 1.0 - lit, 1.0); }\n", "ps_3_0");
        Com<IDirect3DVertexShader9> vs = c.vs("struct I { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\nO main(I i) { O o; o.p = i.p; o.c = i.c; o.t = i.t; return o; }\n", "vs_3_0");
        if (!ps || !vs) {
            return;
        }
        c.tex(0, depth.get());
        c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        c.ss(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        c.ss(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        struct V { float x, y, z; DWORD color; float u, v; };
        const V q[4] = {{-1, 1, 0.5f, 0xFFFFFFFF, 0, 0}, {1, 1, 0.5f, 0xFFFFFFFF, 1, 0}, {-1, -1, 0.5f, 0xFFFFFFFF, 0, 1}, {1, -1, 0.5f, 0xFFFFFFFF, 1, 1}};
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        c.fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
        c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
        c.tex(0, nullptr);
    }, {}, Needs::all({Needs::format(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_D24X8, "D24X8 depth textures"), Needs::shader(3, 3)}));

    Cases::single(rr::CaseSpec("tex", "vertex_texture_fetch_displacement").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DTexture9> height = c.texture(32, 32, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
            const float fx = static_cast<float>(x) / static_cast<float>(w);
            const float fy = static_cast<float>(y) / static_cast<float>(h);
            const unsigned v = static_cast<unsigned>(127.5f + 127.5f * std::sin(fx * 12.0f) * std::cos(fy * 9.0f));
            return Texels::argb(255, v, v, v);
        });
        Com<IDirect3DVertexShader9> vs = c.vs(
            "sampler2D disp : register(s0);\nfloat4 cm[4] : register(c0);\nstruct I { float3 p : POSITION; float2 t : TEXCOORD0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\n"
            "O main(I i) { O o; float h = tex2Dlod(disp, float4(i.t, 0, 0)).r; float4 p = float4(i.p.x, h * 1.2 - 0.3, i.p.z, 1.0);\n"
            "o.p = float4(dot(p, cm[0]), dot(p, cm[1]), dot(p, cm[2]), dot(p, cm[3])); o.c = float4(h, 0.5 + h * 0.3, 1.0 - h, 1.0); return o; }\n", "vs_3_0");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_3_0");
        if (!height || !vs || !ps) {
            return;
        }
        const float eye[3] = {0, 3.0f, -4.5f};
        const float at[3] = {0, 0, 0};
        const float up[3] = {0, 1, 0};
        const Xf m = Xf::look_at(eye, at, up) * Xf::perspective(1.0f, 1.0f, 0.5f, 30.0f);
        float t[16];
        for (int r = 0; r < 4; ++r) {
            for (int k = 0; k < 4; ++k) {
                t[k * 4 + r] = m.m[r][k];
            }
        }
        c.chk(c.d->SetVertexShaderConstantF(0, t, 4), "const");
        c.chk(c.d->SetTexture(D3DVERTEXTEXTURESAMPLER0, height.get()), "SetTexture vertex sampler");
        c.chk(c.d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MINFILTER, D3DTEXF_POINT), "SetSamplerState");
        c.chk(c.d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, D3DTEXF_POINT), "SetSamplerState");
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.fvf(kVtxFvf);
        Mesh grid = Geo::grid(30, 2.5f, 0.0f, 0xFFFFFFFF);
        for (Vtx& v : grid.vertices) {
            v.u = (v.x / 2.5f) * 0.5f + 0.5f;
            v.v = (v.z / 2.5f) * 0.5f + 0.5f;
        }
        c.draw_mesh(grid);
        c.chk(c.d->SetTexture(D3DVERTEXTEXTURESAMPLER0, nullptr), "SetTexture");
        c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
        c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
    }, {}, Needs::all({Needs::shader(3, 3), Needs::format(D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE, D3DFMT_A32B32G32R32F, "vertex texture fetch from A32B32G32R32F")}));
}

const Registrar kDepthTextures(depth_texture_rows);

}  // namespace

}  // namespace lsw::suites::render::d3d9
