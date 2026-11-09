#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

struct SV {
    float x, y, z;
    DWORD color;
    float u, v;
};
constexpr DWORD kSvFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

const char* const kPassSource =
    "struct I { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\n"
    "struct O { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\n"
    "O main(I i) { O o; o.p = i.p; o.c = i.c; o.t = i.t; return o; }\n";

void columns(Ctx& c, UINT reg, const Xf& m) {
    float t[16];
    for (int r = 0; r < 4; ++r) {
        for (int k = 0; k < 4; ++k) {
            t[k * 4 + r] = m.m[r][k];
        }
    }
    c.chk(c.d->SetVertexShaderConstantF(reg, t, 4), "SetVertexShaderConstantF");
}

void pass_quad(Ctx& c, const std::string& ps_source, const char* ps_profile, const char* vs_profile, float uv = 1.0f,
               DWORD tl = 0xFFFFFFFF, DWORD tr = 0xFFC0FF80, DWORD bl = 0xFF80C0FF, DWORD br = 0xFFFFC080) {
    Com<IDirect3DVertexShader9> vs = c.vs(kPassSource, vs_profile);
    Com<IDirect3DPixelShader9> ps = c.ps(ps_source, ps_profile);
    if (!vs || !ps) {
        return;
    }
    c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
    c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
    const float s = 0.9f;
    const SV q[4] = {{-s, s, 0.5f, tl, 0, 0}, {s, s, 0.5f, tr, uv, 0}, {-s, -s, 0.5f, bl, 0, uv}, {s, -s, 0.5f, br, uv, uv}};
    c.fvf(kSvFvf);
    c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(SV));
    c.chk(c.d->SetVertexShader(nullptr), "SetVertexShader");
    c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
}

Need ps_need(unsigned major, unsigned minor = 0) {
    return Needs::caps([=](const D3DCAPS9& caps) { return caps.PixelShaderVersion >= D3DPS_VERSION(major, minor); }, "pixel shader " + std::to_string(major) + "." + std::to_string(minor));
}

Need vs_need(unsigned major, unsigned minor = 0) {
    return Needs::caps([=](const D3DCAPS9& caps) { return caps.VertexShaderVersion >= D3DVS_VERSION(major, minor); }, "vertex shader " + std::to_string(major) + "." + std::to_string(minor));
}

Com<IDirect3DTexture9> detail_texture(Ctx& c) {
    Com<IDirect3DTexture9> t = c.texture(64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
        const std::uint32_t base = Texels::rainbow()(x, y, w, h);
        return ((x / 4 + y / 4) & 1u) ? base : ((base >> 1) & 0x007F7F7Fu) | 0xFF000000u;
    });
    c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    return t;
}

const char* const kPsHead = "sampler2D tex : register(s0);\n";

void stage_cases() {
    const rr::Tolerance tol = rr::Tolerance::blend(3);
    Cases::single(rr::CaseSpec("shader", "vs11_lit_transform").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_CULLMODE, D3DCULL_CCW);
        Com<IDirect3DVertexShader9> vs = c.vs(
            "float4 cm[4] : register(c0);\nfloat4 light : register(c4);\nfloat4 amb : register(c5);\n"
            "struct I { float4 p : POSITION; float3 n : NORMAL; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\n"
            "O main(I i) { O o; o.p = float4(dot(i.p, cm[0]), dot(i.p, cm[1]), dot(i.p, cm[2]), dot(i.p, cm[3]));\n"
            "float d = max(dot(i.n, light.xyz), 0.0); o.c = amb + float4(d, d * 0.9, d * 0.7, 1.0); return o; }\n", "vs_1_1");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_2_0");
        if (!vs || !ps) {
            return;
        }
        const Mesh m = Geo::sphere(24, 16, 1.2f, 0xFFFFFFFF);
        const float eye[3] = {0, 0.6f, -4.0f};
        const float at[3] = {0, 0, 0};
        const float up[3] = {0, 1, 0};
        columns(c, 0, Xf::rot_y(0.5f) * Xf::look_at(eye, at, up) * Xf::perspective(1.0f, 1.0f, 0.5f, 20.0f));
        const float light[4] = {0.3f, 0.6f, -0.7f, 0};
        const float amb[4] = {0.1f, 0.1f, 0.18f, 0};
        c.chk(c.d->SetVertexShaderConstantF(4, light, 1), "const");
        c.chk(c.d->SetVertexShaderConstantF(5, amb, 1), "const");
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        c.fvf(kVtxFvf);
        c.draw_mesh(m);
    }, {}, Needs::all({vs_need(1, 1), ps_need(2, 0)}));

    Cases::single(rr::CaseSpec("shader", "vs20_wave_static_loop").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DVertexShader9> vs = c.vs(
            "float4 cm[4] : register(c0);\nfloat4 wave : register(c4);\n"
            "struct I { float4 p : POSITION; float2 t : TEXCOORD0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\n"
            "O main(I i) { O o; float4 p = i.p; float h = 0;\n"
            "for (int k = 1; k <= 3; k++) { h += sin(p.x * k * 2.0 + wave.x * k) * cos(p.z * k + wave.y) / k; }\n"
            "p.y += h * 0.25; o.p = float4(dot(p, cm[0]), dot(p, cm[1]), dot(p, cm[2]), dot(p, cm[3]));\n"
            "o.c = float4(0.5 + h * 0.5, 0.7 - h * 0.3, 0.9, 1.0); return o; }\n", "vs_2_0");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_2_0");
        if (!vs || !ps) {
            return;
        }
        const float eye[3] = {0, 3.0f, -5.0f};
        const float at[3] = {0, 0, 0};
        const float up[3] = {0, 1, 0};
        columns(c, 0, Xf::look_at(eye, at, up) * Xf::perspective(1.0f, 1.0f, 0.5f, 30.0f));
        const float wave[4] = {0.7f, 1.3f, 0, 0};
        c.chk(c.d->SetVertexShaderConstantF(4, wave, 1), "const");
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        c.fvf(kVtxFvf);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.draw_mesh(Geo::grid(22, 3.0f, 0.0f, 0xFFFFFFFF));
    }, {}, Needs::all({vs_need(2, 0), ps_need(2, 0)}));

    Cases::single(rr::CaseSpec("shader", "vs20_palette_skinning").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DVertexShader9> vs = c.vs(
            "float4 cm[4] : register(c0);\nfloat4 pal[12] : register(c8);\n"
            "struct I { float3 p : POSITION; float w : BLENDWEIGHT; float idx : BLENDINDICES; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\n"
            "float4 xf(float4 p, int b) { return float4(dot(p, pal[b * 4]), dot(p, pal[b * 4 + 1]), dot(p, pal[b * 4 + 2]), 1.0); }\n"
            "O main(I i) { O o; float4 p = float4(i.p, 1.0); int b = (int)i.idx;\n"
            "float4 a = xf(p, 0); float4 s = xf(p, 1); float4 m = lerp(a, s, i.w);\n"
            "o.p = float4(dot(m, cm[0]), dot(m, cm[1]), dot(m, cm[2]), dot(m, cm[3])); o.c = float4(i.w, 0.6, 1.0 - i.w, 1.0); return o; }\n", "vs_2_0");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_2_0");
        if (!vs || !ps) {
            return;
        }
        columns(c, 0, Xf::translate(0, 0, 5.0f) * Xf::perspective(1.0f, 1.0f, 0.5f, 30.0f));
        columns(c, 8, Xf::rot_z(0.6f));
        columns(c, 12, Xf::rot_z(-0.6f) * Xf::translate(0.5f, 0, 0));
        struct V { float x, y, z, w, idx; };
        std::vector<V> v;
        for (int k = 0; k < 16; ++k) {
            const float t = static_cast<float>(k) / 15.0f;
            v.push_back({-0.35f, -1.5f + 3.0f * t, 0, t, 0});
            v.push_back({0.35f, -1.5f + 3.0f * t, 0, t, 0});
        }
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        const D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0}, {0, 12, D3DDECLTYPE_FLOAT1, 0, D3DDECLUSAGE_BLENDWEIGHT, 0},
                                        {0, 16, D3DDECLTYPE_FLOAT1, 0, D3DDECLUSAGE_BLENDINDICES, 0}, D3DDECL_END()};
        Com<IDirect3DVertexDeclaration9> d = c.decl(el);
        c.chk(c.d->SetVertexDeclaration(d.get()), "SetVertexDeclaration");
        c.draw(D3DPT_TRIANGLESTRIP, 30, v.data(), sizeof(V));
    }, {}, Needs::all({vs_need(2, 0), ps_need(2, 0)}));

    Cases::single(rr::CaseSpec("shader", "vs2a_dynamic_branch").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DVertexShader9> vs = c.vs(
            "float4 sel : register(c0);\nstruct I { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\n"
            "O main(I i) { O o; o.p = i.p; o.t = i.t; if (i.t.x + sel.x > 1.0) { o.c = float4(1, 0.3, 0.2, 1); } else { o.c = i.c * float4(0.4, 0.8, 1, 1); } return o; }\n", "vs_2_a");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_2_0");
        if (!vs || !ps) {
            return;
        }
        const float sel[4] = {0.5f, 0, 0, 0};
        c.chk(c.d->SetVertexShaderConstantF(0, sel, 1), "const");
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        SV v[12];
        for (int k = 0; k < 6; ++k) {
            const float x0 = -0.9f + 0.3f * static_cast<float>(k);
            const SV a{x0, 0.8f, 0.5f, 0xFFFFFFFF, static_cast<float>(k) / 5.0f, 0};
            const SV b{x0 + 0.25f, 0.8f, 0.5f, 0xFFFFFFFF, static_cast<float>(k) / 5.0f, 0};
            v[k * 2] = a;
            v[k * 2 + 1] = b;
        }
        std::vector<SV> tris;
        for (int k = 0; k < 6; ++k) {
            const float x0 = -0.9f + 0.3f * static_cast<float>(k);
            const float u = static_cast<float>(k) / 5.0f;
            tris.push_back({x0, 0.8f, 0.5f, 0xFFE0E0E0, u, 0});
            tris.push_back({x0 + 0.25f, 0.8f, 0.5f, 0xFFE0E0E0, u, 0});
            tris.push_back({x0, -0.8f, 0.5f, 0xFFE0E0E0, u, 0});
        }
        c.fvf(kSvFvf);
        c.draw(D3DPT_TRIANGLELIST, 6, tris.data(), sizeof(SV));
    }, {}, Needs::all({vs_need(2, 0), ps_need(2, 0)}));

    Cases::single(rr::CaseSpec("shader", "vs30_int_loop_and_bool").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DVertexShader9> vs = c.vs(
            "int count : register(i0);\nbool flip : register(b0);\nstruct I { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\n"
            "struct O { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\n"
            "O main(I i) { O o; float4 p = i.p; float acc = 0; for (int k = 0; k < count; k++) { acc += 0.1 * (k + 1); }\n"
            "if (flip) { p.y = -p.y; } p.x += acc * 0.1 - 0.1; o.p = p; o.c = i.c * float4(1, acc, 1 - acc * 0.5, 1); o.t = i.t; return o; }\n", "vs_3_0");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_3_0");
        if (!vs || !ps) {
            return;
        }
        const int count[4] = {4, 0, 0, 0};
        const BOOL flip = TRUE;
        c.chk(c.d->SetVertexShaderConstantI(0, count, 1), "constI");
        c.chk(c.d->SetVertexShaderConstantB(0, &flip, 1), "constB");
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        const SV q[4] = {{-0.8f, 0.8f, 0.5f, 0xFFFF8080, 0, 0}, {0.8f, 0.8f, 0.5f, 0xFF80FF80, 1, 0}, {-0.8f, -0.8f, 0.5f, 0xFF8080FF, 0, 1}, {0.8f, -0.8f, 0.5f, 0xFFFFFF80, 1, 1}};
        c.fvf(kSvFvf);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(SV));
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
}

void pixel_cases() {
    const rr::Tolerance tol = rr::Tolerance::blend(3);
    const struct {
        const char* name;
        const char* vs;
        const char* ps;
    } profiles[] = {{"ps20_procedural", "vs_2_0", "ps_2_0"}, {"ps2a_procedural", "vs_2_a", "ps_2_a"}, {"ps2b_procedural", "vs_2_0", "ps_2_b"}, {"ps30_procedural", "vs_3_0", "ps_3_0"}};
    for (const auto& p : profiles) {
        Cases::single(rr::CaseSpec("shader", p.name).tolerance(rr::Tolerance::blend(4)), [p](Ctx& c) {
            c.clear(Dx::kColor(15, 15, 40));
            Com<IDirect3DTexture9> t = detail_texture(c);
            c.tex(0, t.get());
            pass_quad(c, std::string(kPsHead) +
                          "float4 k : register(c0);\n"
                          "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR {\n"
                          "float4 s = tex2D(tex, uv * 2.0);\n"
                          "float r = length(uv - 0.5); float ring = 0.5 + 0.5 * sin(r * 40.0);\n"
                          "float3 rgb = lerp(s.rgb, col.rgb, 0.35) * (0.6 + 0.4 * ring);\n"
                          "rgb = saturate(rgb * k.x + k.y); return float4(rgb, 1.0); }\n",
                      p.ps, p.vs);
        }, {}, Needs::all({ps_need(p.ps[3] - '0'), Needs::caps([p](const D3DCAPS9& caps) { return p.ps[6] != 'a' || (caps.PS20Caps.NumInstructionSlots >= 512 && caps.PS20Caps.NumTemps >= 22); }, "ps_2_a capabilities"), vs_need(p.vs[3] - '0')}));
    }
    Cases::single(rr::CaseSpec("shader", "ps30_loop_break").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        pass_quad(c,
                  "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR {\n"
                  "float2 z = 0; float2 cc = (uv - float2(0.7, 0.5)) * 2.6; int n = 0;\n"
                  "for (int i = 0; i < 24; i++) { z = float2(z.x * z.x - z.y * z.y, 2 * z.x * z.y) + cc; if (dot(z, z) > 4.0) break; n++; }\n"
                  "float f = n / 24.0; return float4(f, f * f, 1.0 - f, 1.0); }\n", "ps_3_0", "vs_3_0");
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    Cases::single(rr::CaseSpec("shader", "ps30_dynamic_branch").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        pass_quad(c,
                  "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR {\n"
                  "float4 o; [flatten] if (uv.x > uv.y) { o = float4(col.rgb, 1); } else { o = float4(1 - col.rgb, 1); }\n"
                  "if (frac(uv.x * 8.0) < 0.5) { o.rgb *= 0.6; } return o; }\n", "ps_3_0", "vs_3_0");
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    Cases::single(rr::CaseSpec("shader", "ps30_int_bool_constants").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DPixelShader9> ps = c.ps(
            "uniform int n;\nuniform bool inv;\n"
            "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR {\nfloat acc = 0; for (int k = 0; k < n; k++) { acc += step(0.5, frac(uv.x * (k + 1))) / n; }\n"
            "if (inv) acc = 1 - acc; return float4(acc, col.g * acc, col.b, 1); }\n", "ps_3_0");
        Com<IDirect3DVertexShader9> vs = c.vs(kPassSource, "vs_3_0");
        if (!ps || !vs) {
            return;
        }
        const int n[4] = {5, 0, 0, 0};
        const BOOL inv = TRUE;
        c.chk(c.d->SetPixelShaderConstantI(0, n, 1), "constI");
        c.chk(c.d->SetPixelShaderConstantB(0, &inv, 1), "constB");
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        const SV q[4] = {{-0.9f, 0.9f, 0.5f, 0xFFFFFFFF, 0, 0}, {0.9f, 0.9f, 0.5f, 0xFFC0FF80, 1, 0}, {-0.9f, -0.9f, 0.5f, 0xFF80C0FF, 0, 1}, {0.9f, -0.9f, 0.5f, 0xFFFFC080, 1, 1}};
        c.fvf(kSvFvf);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(SV));
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    Cases::single(rr::CaseSpec("shader", "ps30_derivatives_ddx_ddy").tolerance(rr::Tolerance::blend(6)), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        pass_quad(c,
                  "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR {\n"
                  "float2 p = uv * uv * 6.0; float2 dx = ddx(p); float2 dy = ddy(p);\n"
                  "return float4(abs(dx) * 4.0, abs(dy.y) * 4.0, 1.0); }\n", "ps_3_0", "vs_3_0");
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    Cases::single(rr::CaseSpec("shader", "ps30_texldl_explicit_lod").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DTexture9> t = c.texture(64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT) {
            unsigned level = 0;
            while ((64u >> level) > w) {
                ++level;
            }
            const DWORD colors[7] = {0xFFFF4040, 0xFF40FF40, 0xFF4040FF, 0xFFFFFF40, 0xFFFF40FF, 0xFF40FFFF, 0xFF808080};
            return ((x + y) & 1u) && level == 0 ? 0xFFFFFFFFu : colors[level % 7];
        });
        c.tex(0, t.get());
        c.ss(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        c.ss(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        pass_quad(c, std::string(kPsHead) + "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR { return tex2Dlod(tex, float4(uv, 0, floor(uv.x * 6.0))); }\n", "ps_3_0", "vs_3_0");
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    Cases::single(rr::CaseSpec("shader", "ps30_texldd_explicit_gradients").tolerance(rr::Tolerance::blend(4)), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DTexture9> t = detail_texture(c);
        c.tex(0, t.get());
        pass_quad(c, std::string(kPsHead) + "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR { float g = 0.002 + uv.x * 0.05; return tex2Dgrad(tex, uv * 3.0, float2(g, 0), float2(0, g)); }\n", "ps_3_0", "vs_3_0");
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    Cases::single(rr::CaseSpec("shader", "ps30_vpos").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        pass_quad(c, "float4 main(float4 col : COLOR0, float4 vp : VPOS) : COLOR { float2 q = floor(vp.xy / 8.0); float k = fmod(q.x + q.y, 2.0); return float4(k, vp.x / 128.0, vp.y / 128.0, 1.0); }\n", "ps_3_0", "vs_3_0");
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    Cases::single(rr::CaseSpec("shader", "ps30_vface").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DVertexShader9> vs = c.vs(kPassSource, "vs_3_0");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 col : COLOR0, float f : VFACE) : COLOR { return f > 0 ? float4(1, 0.3, 0.2, 1) : float4(0.2, 0.4, 1, 1); }\n", "ps_3_0");
        if (!vs || !ps) {
            return;
        }
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        const SV cw[3] = {{-0.9f, 0.8f, 0.5f, 0xFFFFFFFF, 0, 0}, {-0.1f, 0.8f, 0.5f, 0xFFFFFFFF, 0, 0}, {-0.9f, -0.8f, 0.5f, 0xFFFFFFFF, 0, 0}};
        const SV ccw[3] = {{0.1f, 0.8f, 0.5f, 0xFFFFFFFF, 0, 0}, {0.1f, -0.8f, 0.5f, 0xFFFFFFFF, 0, 0}, {0.9f, 0.8f, 0.5f, 0xFFFFFFFF, 0, 0}};
        c.fvf(kSvFvf);
        c.draw(D3DPT_TRIANGLELIST, 1, cw, sizeof(SV));
        c.draw(D3DPT_TRIANGLELIST, 1, ccw, sizeof(SV));
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    Cases::single(rr::CaseSpec("shader", "ps20_texkill_clip").tolerance(tol), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        pass_quad(c, "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR { clip(sin(uv.x * 30.0) * cos(uv.y * 20.0)); return float4(col.rgb, 1); }\n", "ps_2_0", "vs_2_0");
    }, {}, Needs::all({vs_need(2, 0), ps_need(2, 0)}));
    for (int ver = 2; ver <= 3; ++ver) {
        Cases::single(rr::CaseSpec("shader", ver == 2 ? "ps20_depth_output" : "ps30_depth_output").tolerance(tol), [ver](Ctx& c) {
            c.clear(Dx::kColor(15, 15, 40));
            c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
            c.rs(D3DRS_ZFUNC, D3DCMP_LESS);
            c.quad(0, 0, 128, 128, 0.5f, 0xFF3060C0, 0xFF60C030, 0xFFC03060, 0xFF30C0C0);
            pass_quad(c,
                      "struct O { float4 c : COLOR0; float d : DEPTH; };\n"
                      "O main(float4 col : COLOR0, float2 uv : TEXCOORD0) { O o; o.c = float4(col.rgb, 1); o.d = uv.x; return o; }\n",
                      ver == 2 ? "ps_2_0" : "ps_3_0", ver == 2 ? "vs_2_0" : "vs_3_0");
        }, {}, Needs::all({vs_need(static_cast<unsigned>(ver), 0), ps_need(static_cast<unsigned>(ver), 0)}));
    }
    Cases::single(rr::CaseSpec("shader", "ps30_mrt_four_targets").tolerance(tol), [](Ctx& c) {
        Surface t1, t2, t3;
        std::string error;
        if (!c.kit.make_target(t1, 128, 128, D3DFMT_A8R8G8B8, error) || !c.kit.make_target(t2, 128, 128, D3DFMT_A8R8G8B8, error) ||
            !c.kit.make_target(t3, 128, 128, D3DFMT_A8R8G8B8, error)) {
            c.fail(error);
            return;
        }
        c.chk(c.d->SetRenderTarget(1, t1.surface.get()), "SetRenderTarget1");
        c.chk(c.d->SetRenderTarget(2, t2.surface.get()), "SetRenderTarget2");
        c.chk(c.d->SetRenderTarget(3, t3.surface.get()), "SetRenderTarget3");
        c.clear(Dx::kColor(0, 0, 0, 255));
        pass_quad(c,
                  "struct O { float4 a : COLOR0; float4 b : COLOR1; float4 c : COLOR2; float4 d : COLOR3; };\n"
                  "O main(float4 col : COLOR0, float2 uv : TEXCOORD0) { O o; o.a = float4(col.rgb, 1); o.b = float4(uv, 0, 1); o.c = float4(1 - col.rgb, 1); o.d = float4(uv.x * uv.y, 0.5, uv.y, 1); return o; }\n",
                  "ps_3_0", "vs_3_0");
        c.snap("rt0");
        c.snap_surface(t1, "rt1");
        c.snap_surface(t2, "rt2");
        c.snap_surface(t3, "rt3");
        c.chk(c.d->SetRenderTarget(1, nullptr), "SetRenderTarget1");
        c.chk(c.d->SetRenderTarget(2, nullptr), "SetRenderTarget2");
        c.chk(c.d->SetRenderTarget(3, nullptr), "SetRenderTarget3");
    }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0), Needs::caps([](const D3DCAPS9& caps) { return caps.NumSimultaneousRTs >= 4; }, "four simultaneous render targets")}));
}

struct InstVertex {
    float x, y, u, v;
};

void instancing_cases() {
    for (int indexed = 0; indexed < 2; ++indexed) {
        Cases::single(rr::CaseSpec("shader", indexed ? "instancing_indexed_geometry" : "instancing_array_geometry").tolerance(rr::Tolerance::blend(3)), [indexed](Ctx& c) {
            c.clear(Dx::kColor(15, 15, 40));
            Com<IDirect3DVertexShader9> vs = c.vs(
                "struct I { float2 p : POSITION; float2 t : TEXCOORD0; float4 inst : TEXCOORD1; float4 col : COLOR0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\n"
                "O main(I i) { O o; o.p = float4(i.p * i.inst.z + i.inst.xy, 0.5, 1.0); o.c = i.col; o.t = i.t; return o; }\n", "vs_3_0");
            Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR { return col * (0.6 + 0.4 * uv.x); }\n", "ps_3_0");
            if (!vs || !ps) {
                return;
            }
            const InstVertex quad[4] = {{-1, 1, 0, 0}, {1, 1, 1, 0}, {-1, -1, 0, 1}, {1, -1, 1, 1}};
            struct Inst { float x, y, s, pad; DWORD color; };
            std::vector<Inst> inst;
            for (int i = 0; i < 12; ++i) {
                inst.push_back({-0.75f + 0.5f * static_cast<float>(i % 4), 0.65f - 0.55f * static_cast<float>(i / 4), 0.16f + 0.015f * static_cast<float>(i), 0, Dx::kColor(60 + 15 * static_cast<unsigned>(i), 255 - 18 * static_cast<unsigned>(i), 120 + 8 * static_cast<unsigned>(i))});
            }
            const std::uint16_t idx[6] = {0, 1, 2, 2, 1, 3};
            Com<IDirect3DVertexBuffer9> geom = c.vbuffer(quad, sizeof quad);
            Com<IDirect3DVertexBuffer9> per = c.vbuffer(inst.data(), static_cast<UINT>(inst.size() * sizeof(Inst)));
            Com<IDirect3DIndexBuffer9> ib = c.ibuffer(idx, sizeof idx, D3DFMT_INDEX16);
            const D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_POSITION, 0}, {0, 8, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0},
                                            {1, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_TEXCOORD, 1}, {1, 16, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
            Com<IDirect3DVertexDeclaration9> d = c.decl(el);
            if (!geom || !per || !ib || !d) {
                return;
            }
            c.chk(c.d->SetVertexDeclaration(d.get()), "SetVertexDeclaration");
            c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
            c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
            c.chk(c.d->SetStreamSource(0, geom.get(), 0, sizeof(InstVertex)), "SetStreamSource");
            c.chk(c.d->SetStreamSource(1, per.get(), 0, sizeof(Inst)), "SetStreamSource");
            c.chk(c.d->SetIndices(ib.get()), "SetIndices");
            c.chk(c.d->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 12u), "SetStreamSourceFreq");
            c.chk(c.d->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1u), "SetStreamSourceFreq");
            c.chk(c.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2), "DrawIndexedPrimitive");
            c.chk(c.d->SetStreamSourceFreq(0, 1), "SetStreamSourceFreq");
            c.chk(c.d->SetStreamSourceFreq(1, 1), "SetStreamSourceFreq");
            c.chk(c.d->SetStreamSource(1, nullptr, 0, 0), "SetStreamSource");
            if (indexed) {
                c.chk(c.d->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 5u), "SetStreamSourceFreq");
                c.chk(c.d->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1u), "SetStreamSourceFreq");
                c.chk(c.d->SetStreamSource(1, per.get(), 0, sizeof(Inst)), "SetStreamSource");
                c.rs(D3DRS_ALPHABLENDENABLE, TRUE);
                c.rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                c.rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                c.chk(c.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2), "DrawIndexedPrimitive");
                c.chk(c.d->SetStreamSourceFreq(0, 1), "SetStreamSourceFreq");
                c.chk(c.d->SetStreamSourceFreq(1, 1), "SetStreamSourceFreq");
            }
        }, {}, Needs::all({vs_need(3, 0), ps_need(3, 0)}));
    }
}

struct DeclType {
    const char* name;
    D3DDECLTYPE type;
    DWORD cap;
    UINT bytes;
};

void write_attr(D3DDECLTYPE type, int corner, std::uint8_t* dst, float& scale) {
    const float a = 0.15f + 0.25f * static_cast<float>(corner);
    const float b = 0.9f - 0.2f * static_cast<float>(corner);
    const float c = 0.3f + 0.15f * static_cast<float>(corner);
    const float d = 1.0f - 0.1f * static_cast<float>(corner);
    const auto f4 = [&](int n) {
        const float v[4] = {a, b, c, d};
        std::memcpy(dst, v, static_cast<std::size_t>(n) * 4);
    };
    const auto s16 = [&](int n, float k) {
        const float v[4] = {a, b, c, d};
        for (int i = 0; i < n; ++i) {
            const std::int16_t s = static_cast<std::int16_t>(v[i] * k);
            std::memcpy(dst + i * 2, &s, 2);
        }
    };
    const auto u16 = [&](int n) {
        const float v[4] = {a, b, c, d};
        for (int i = 0; i < n; ++i) {
            const std::uint16_t s = static_cast<std::uint16_t>(v[i] * 65535.0f);
            std::memcpy(dst + i * 2, &s, 2);
        }
    };
    const auto u8 = [&](bool norm) {
        const float v[4] = {a, b, c, d};
        for (int i = 0; i < 4; ++i) {
            dst[i] = static_cast<std::uint8_t>(v[i] * 255.0f);
        }
        (void)norm;
    };
    scale = 1.0f;
    switch (type) {
        case D3DDECLTYPE_FLOAT1: f4(1); break;
        case D3DDECLTYPE_FLOAT2: f4(2); break;
        case D3DDECLTYPE_FLOAT3: f4(3); break;
        case D3DDECLTYPE_FLOAT4: f4(4); break;
        case D3DDECLTYPE_D3DCOLOR: {
            const DWORD col = Dx::kColor(static_cast<unsigned>(a * 255), static_cast<unsigned>(b * 255), static_cast<unsigned>(c * 255), static_cast<unsigned>(d * 255));
            std::memcpy(dst, &col, 4);
            break;
        }
        case D3DDECLTYPE_UBYTE4: u8(false); scale = 1.0f / 255.0f; break;
        case D3DDECLTYPE_UBYTE4N: u8(true); break;
        case D3DDECLTYPE_SHORT2: s16(2, 20000.0f); scale = 1.0f / 20000.0f; break;
        case D3DDECLTYPE_SHORT4: s16(4, 20000.0f); scale = 1.0f / 20000.0f; break;
        case D3DDECLTYPE_SHORT2N: s16(2, 32767.0f); break;
        case D3DDECLTYPE_SHORT4N: s16(4, 32767.0f); break;
        case D3DDECLTYPE_USHORT2N: u16(2); break;
        case D3DDECLTYPE_USHORT4N: u16(4); break;
        case D3DDECLTYPE_UDEC3: {
            const DWORD v = (static_cast<DWORD>(a * 1023) & 1023) | ((static_cast<DWORD>(b * 1023) & 1023) << 10) | ((static_cast<DWORD>(c * 1023) & 1023) << 20);
            std::memcpy(dst, &v, 4);
            scale = 1.0f / 1023.0f;
            break;
        }
        case D3DDECLTYPE_DEC3N: {
            const auto q = [](float x) { return static_cast<DWORD>(static_cast<std::int32_t>(x * 511.0f)) & 1023u; };
            const DWORD v = q(a) | (q(b) << 10) | (q(c) << 20);
            std::memcpy(dst, &v, 4);
            break;
        }
        case D3DDECLTYPE_FLOAT16_2:
        case D3DDECLTYPE_FLOAT16_4: {
            const float v[4] = {a, b, c, d};
            const int n = type == D3DDECLTYPE_FLOAT16_2 ? 2 : 4;
            for (int i = 0; i < n; ++i) {
                const std::uint16_t h = Texels::half(v[i]);
                std::memcpy(dst + i * 2, &h, 2);
            }
            break;
        }
        default: break;
    }
}

void decl_cases() {
    const DeclType types[] = {
        {"float1", D3DDECLTYPE_FLOAT1, 0, 4}, {"float2", D3DDECLTYPE_FLOAT2, 0, 8}, {"float3", D3DDECLTYPE_FLOAT3, 0, 12}, {"float4", D3DDECLTYPE_FLOAT4, 0, 16},
        {"d3dcolor", D3DDECLTYPE_D3DCOLOR, 0, 4}, {"ubyte4", D3DDECLTYPE_UBYTE4, D3DDTCAPS_UBYTE4, 4}, {"short2", D3DDECLTYPE_SHORT2, 0, 4}, {"short4", D3DDECLTYPE_SHORT4, 0, 8},
        {"ubyte4n", D3DDECLTYPE_UBYTE4N, D3DDTCAPS_UBYTE4N, 4}, {"short2n", D3DDECLTYPE_SHORT2N, D3DDTCAPS_SHORT2N, 4}, {"short4n", D3DDECLTYPE_SHORT4N, D3DDTCAPS_SHORT4N, 8},
        {"ushort2n", D3DDECLTYPE_USHORT2N, D3DDTCAPS_USHORT2N, 4}, {"ushort4n", D3DDECLTYPE_USHORT4N, D3DDTCAPS_USHORT4N, 8}, {"udec3", D3DDECLTYPE_UDEC3, D3DDTCAPS_UDEC3, 4},
        {"dec3n", D3DDECLTYPE_DEC3N, D3DDTCAPS_DEC3N, 4}, {"float16_2", D3DDECLTYPE_FLOAT16_2, D3DDTCAPS_FLOAT16_2, 4}, {"float16_4", D3DDECLTYPE_FLOAT16_4, D3DDTCAPS_FLOAT16_4, 8},
    };
    for (const DeclType& t : types) {
        Cases::single(rr::CaseSpec("shader", std::string("decl_type_") + t.name).tolerance(rr::Tolerance::blend(3)), [t](Ctx& c) {
            c.clear(Dx::kColor(15, 15, 40));
            Com<IDirect3DVertexShader9> vs = c.vs(
                "float4 k : register(c0);\nstruct I { float2 p : POSITION; float4 a : TEXCOORD0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\n"
                "O main(I i) { O o; o.p = float4(i.p, 0.5, 1.0); o.c = float4(saturate(i.a.xyz * k.x), 1.0); return o; }\n", "vs_2_0");
            Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_2_0");
            if (!vs || !ps) {
                return;
            }
            const UINT stride = 8 + t.bytes;
            std::vector<std::uint8_t> data(static_cast<std::size_t>(stride) * 4, 0);
            const float pos[4][2] = {{-0.85f, 0.85f}, {0.85f, 0.85f}, {-0.85f, -0.85f}, {0.85f, -0.85f}};
            float scale = 1.0f;
            for (int k = 0; k < 4; ++k) {
                std::memcpy(&data[static_cast<std::size_t>(k) * stride], pos[k], 8);
                write_attr(t.type, k, &data[static_cast<std::size_t>(k) * stride + 8], scale);
            }
            const float kconst[4] = {scale, 0, 0, 0};
            c.chk(c.d->SetVertexShaderConstantF(0, kconst, 1), "const");
            const D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_POSITION, 0}, {0, 8, static_cast<BYTE>(t.type), 0, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
            Com<IDirect3DVertexDeclaration9> d = c.decl(el);
            c.chk(c.d->SetVertexDeclaration(d.get()), "SetVertexDeclaration");
            c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
            c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
            c.draw(D3DPT_TRIANGLESTRIP, 2, data.data(), stride);
        }, {}, Needs::all({vs_need(2, 0), ps_need(2, 0), Needs::caps([t](const D3DCAPS9& caps) { return t.cap == 0 || (caps.DeclTypes & t.cap) != 0; }, std::string("declaration type ") + t.name)}));
    }
    Cases::single(rr::CaseSpec("shader", "decl_multiple_streams_and_offsets").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DVertexShader9> vs = c.vs(
            "struct I { float3 p : POSITION; float3 n : NORMAL; float4 col : COLOR0; float2 t : TEXCOORD0; };\nstruct O { float4 p : POSITION; float4 c : COLOR0; };\n"
            "O main(I i) { O o; o.p = float4(i.p.xy, 0.5, 1.0); o.c = float4(i.col.rgb * (0.5 + 0.5 * i.n.z) + float3(i.t, 0) * 0.3, 1.0); return o; }\n", "vs_2_0");
        Com<IDirect3DPixelShader9> ps = c.ps("float4 main(float4 c : COLOR0) : COLOR { return c; }\n", "ps_2_0");
        if (!vs || !ps) {
            return;
        }
        struct S0 { float x, y, z; };
        struct S1 { float pad[2]; DWORD col; float nx, ny, nz; float u, v; };
        const S0 s0[4] = {{-0.8f, 0.8f, 0}, {0.8f, 0.8f, 0}, {-0.8f, -0.8f, 0}, {0.8f, -0.8f, 0}};
        const S1 s1[4] = {{{0, 0}, 0xFFFF6060, 0, 0, 1, 0, 0}, {{0, 0}, 0xFF60FF60, 0, 0, 0.5f, 1, 0}, {{0, 0}, 0xFF6060FF, 0, 0, 0.5f, 0, 1}, {{0, 0}, 0xFFFFFF60, 0, 0, 0, 1, 1}};
        Com<IDirect3DVertexBuffer9> a = c.vbuffer(s0, sizeof s0);
        Com<IDirect3DVertexBuffer9> b = c.vbuffer(s1, sizeof s1);
        const D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0}, {1, 8, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0},
                                        {1, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0}, {1, 24, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
        Com<IDirect3DVertexDeclaration9> d = c.decl(el);
        if (!a || !b || !d) {
            return;
        }
        c.chk(c.d->SetVertexDeclaration(d.get()), "SetVertexDeclaration");
        c.chk(c.d->SetVertexShader(vs.get()), "SetVertexShader");
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        c.chk(c.d->SetStreamSource(0, a.get(), 0, sizeof(S0)), "SetStreamSource");
        c.chk(c.d->SetStreamSource(1, b.get(), 0, sizeof(S1)), "SetStreamSource");
        c.chk(c.d->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2), "DrawPrimitive");
        c.chk(c.d->SetStreamSource(1, nullptr, 0, 0), "SetStreamSource");
    }, {}, Needs::all({vs_need(2, 0), ps_need(2, 0)}));
}

DWORD tok(DWORD reg_type, DWORD number, DWORD mask_or_swizzle, bool dest) {
    const DWORD type_bits = ((reg_type & 7u) << 28) | (((reg_type >> 3) & 3u) << 11);
    return 0x80000000u | type_bits | number | (mask_or_swizzle << 16) | (dest ? 0u : 0u);
}

constexpr DWORD kTemp = 0, kInput = 1, kConst = 2, kTexture = 3;

DWORD dst(DWORD type, DWORD n) { return tok(type, n, 0xF, true); }
DWORD src(DWORD type, DWORD n, DWORD swizzle = 0xE4) { return tok(type, n, swizzle, false); }

using Tokens = std::vector<DWORD>;

void ps1_case(const std::string& name, DWORD version, const Tokens& program, unsigned stages, bool bump = false) {
    Cases::single(rr::CaseSpec("shader", name).tolerance(rr::Tolerance::blend(4)), [version, program, stages, bump](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        Com<IDirect3DTexture9> t0;
        if (bump) {
            t0 = c.texture(32, 32, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
                return Texels::argb(255, 128 + static_cast<unsigned>(60.0f * std::sin(static_cast<float>(x) * 6.0f / static_cast<float>(w))), 128 + static_cast<unsigned>(60.0f * std::cos(static_cast<float>(y) * 6.0f / static_cast<float>(h))), 0);
            });
        } else {
            t0 = c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, [](UINT x, UINT y, UINT w, UINT h) {
                const std::uint32_t g = Texels::rainbow()(x, y, w, h);
                return ((x / 4 + y / 4) & 1u) ? g : (g & 0x00FFFFFFu) | 0x90000000u;
            });
        }
        Com<IDirect3DTexture9> t1 = c.texture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, Texels::checker(4, 0xFFF0E0A0, 0xFF3060A0));
        c.tex(0, t0.get());
        c.tex(1, t1.get());
        for (DWORD s = 0; s < stages; ++s) {
            c.ss(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            c.ss(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        }
        Tokens code = program;
        code.insert(code.begin(), version);
        code.push_back(0x0000FFFFu);
        Com<IDirect3DPixelShader9> ps;
        c.chk(c.d->CreatePixelShader(code.data(), ps.put()), "CreatePixelShader ps_1_x");
        if (!ps) {
            return;
        }
        if (bump) {
            c.tss(0, D3DTSS_BUMPENVMAT00, Dx::fbits(0.2f));
            c.tss(0, D3DTSS_BUMPENVMAT01, 0);
            c.tss(0, D3DTSS_BUMPENVMAT10, 0);
            c.tss(0, D3DTSS_BUMPENVMAT11, Dx::fbits(0.2f));
        }
        c.chk(c.d->SetPixelShader(ps.get()), "SetPixelShader");
        struct V { float x, y, z; DWORD color; float u0, v0, u1, v1; };
        const float s = 0.9f;
        const V q[4] = {{-s, s, 0.5f, 0xFFFFC0C0, -1, 0, 0, 0}, {s, s, 0.5f, 0xFFC0FFC0, 1, 0, 1, 0}, {-s, -s, 0.5f, 0xFFC0C0FF, -1, 1, 0, 1}, {s, -s, 0.5f, 0xFFFFFFC0, 1, 1, 1, 1}};
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        c.fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2);
        c.draw(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        c.chk(c.d->SetPixelShader(nullptr), "SetPixelShader");
    }, {}, Needs::caps([version, bump](const D3DCAPS9& caps) { return caps.PixelShaderVersion >= version && (!bump || (caps.TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAP) != 0); }, "pixel shader " + name));
}

void ps1_cases() {
    ps1_case("ps11_tex_mul_diffuse", 0xFFFF0101u, {0x42, dst(kTexture, 0), 0x5, dst(kTemp, 0), src(kTexture, 0), src(kInput, 0)}, 1);
    ps1_case("ps11_two_textures_mad", 0xFFFF0101u,
             {0x42, dst(kTexture, 0), 0x42, dst(kTexture, 1), 0x4, dst(kTemp, 0), src(kTexture, 0), src(kTexture, 1), src(kInput, 0)}, 2);
    ps1_case("ps11_texkill", 0xFFFF0101u,
             {0x41, dst(kTexture, 0), 0x42, dst(kTexture, 1), 0x1, dst(kTemp, 0), src(kTexture, 1)}, 2);
    ps1_case("ps11_dp3_constant_gray", 0xFFFF0101u,
             {0x51, dst(kConst, 1), Dx::fbits(0.299f), Dx::fbits(0.587f), Dx::fbits(0.114f), 0, 0x42, dst(kTexture, 0), 0x8, dst(kTemp, 0), src(kTexture, 0), src(kConst, 1)}, 1);
    ps1_case("ps11_cnd_select", 0xFFFF0101u,
             {0x42, dst(kTexture, 0), 0x42, dst(kTexture, 1), 0x1, dst(kTemp, 0), src(kTexture, 0), 0x50, dst(kTemp, 0), src(kTemp, 0, 0xFF), src(kTexture, 1), src(kInput, 0)}, 2);
    ps1_case("ps13_texbem", 0xFFFF0103u, {0x42, dst(kTexture, 0), 0x43, dst(kTexture, 1), src(kTexture, 0), 0x1, dst(kTemp, 0), src(kTexture, 1)}, 2, true);
    ps1_case("ps14_texld_two_stage", 0xFFFF0104u,
             {0x42, dst(kTemp, 0), src(kTexture, 0), 0x42, dst(kTemp, 1), src(kTexture, 1), 0x5, dst(kTemp, 0), src(kTemp, 0), src(kTemp, 1)}, 2);
}

const Registrar kStages(stage_cases);
const Registrar kPixel(pixel_cases);
const Registrar kInstancing(instancing_cases);
const Registrar kDecl(decl_cases);
const Registrar kPs1(ps1_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
