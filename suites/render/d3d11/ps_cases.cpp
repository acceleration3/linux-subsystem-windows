#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kPsIn = "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n";

bool fs_pass(Rig& r, Cap& out, const std::string& ps_source, UINT samples = 1, bool per_pixel_resolve = true) {
    (void)per_pixel_resolve;
    Target target;
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    if (!ps || !r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, samples)) return false;
    r.bind(target);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}

const Add kSvPosition(spec("ps", "sv_position_xy").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) + "float4 main(VO i) : SV_Target { return float4(frac(i.p.x / 32.0), frac(i.p.y / 16.0), (uint(i.p.x) ^ uint(i.p.y)) & 1, 1); }\n");
});

const char* const kPerspVs =
    "cbuffer F : register(b0) { column_major float4x4 m; };\n"
    "struct VI { float3 p : POSITION; float4 c : COLOR; };\n"
    "struct VO { float4 p : SV_Position; float4 a : A; centroid float4 b : B; nointerpolation float4 c : C; noperspective float4 d : D; sample float4 e : E; };\n"
    "VO main(VI i) { VO o; o.p = mul(m, float4(i.p, 1)); o.a = o.b = o.c = o.d = o.e = i.c; return o; }\n";

bool interpolation(Rig& r, Cap& out, const char* pick, UINT samples) {
    Target target;
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(kPerspVs, &code);
    const std::string ps_source = std::string("struct VO { float4 p : SV_Position; float4 a : A; centroid float4 b : B; nointerpolation float4 c : C; noperspective float4 d : D; sample float4 e : E; };\n") +
                                  "float4 main(VO i) : SV_Target { return " + pick + "; }\n";
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    if (!vs || !ps || !r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, samples)) return false;
    struct V {
        float p[3];
        float c[4];
    };
    const std::vector<V> verts = {{{-1.6f, -0.9f, -1.0f}, {1, 0, 0, 1}}, {{1.6f, -0.9f, -1.0f}, {0, 1, 0, 1}}, {{-1.6f, -0.9f, -9.0f}, {0, 0, 1, 1}},
                                  {{1.6f, -0.9f, -9.0f}, {1, 1, 0, 1}}, {{-0.4f, 0.8f, -3.0f}, {1, 0, 1, 1}}, {{0.8f, 0.2f, -2.2f}, {0, 1, 1, 1}}, {{0.3f, 0.9f, -5.0f}, {1, 1, 1, 1}}};
    const std::vector<std::uint16_t> idx = {0, 1, 2, 2, 1, 3, 4, 5, 6};
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    Com<ID3D11Buffer> ib = r.ibuf(idx);
    const rr::Mat4 m = rr::Mat4::perspective_d3d(1.3f, 1.0f, 0.5f, 20.0f);
    Com<ID3D11Buffer> cb = r.cbuf(m.m, 64);
    Com<ID3D11InputLayout> il = r.layout({elem("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0), elem("COLOR", DXGI_FORMAT_R32G32B32A32_FLOAT, 12)}, code);
    D3D11_RASTERIZER_DESC d = rdesc();
    d.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(d);
    if (!vb || !ib || !cb || !il || !rs) return false;
    r.bind(target);
    r.ctx->RSSetState(rs.get());
    const UINT stride = sizeof(V), offset = 0;
    ID3D11Buffer* b = vb.get();
    ID3D11Buffer* c = cb.get();
    r.ctx->IASetInputLayout(il.get());
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->VSSetConstantBuffers(0, 1, &c);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->DrawIndexed(9, 0, 0);
    return r.shot(out, target);
}

struct InterpRegistrar {
    InterpRegistrar() {
        struct Row {
            const char* name;
            const char* pick;
            UINT samples;
        };
        static const Row rows[] = {{"linear", "i.a", 1}, {"noperspective", "i.d", 1}, {"nointerpolation", "i.c", 1}, {"centroid_4x", "i.b", 4},
                                   {"linear_4x", "i.a", 4}, {"sample_4x", "i.e", 4}};
        for (const Row& row : rows) {
            auto cs = spec("ps", (std::string("interpolation_") + row.name).c_str()).tolerance(row.samples > 1 ? rr::Tolerance::psnr(26) : rr::Tolerance::blend(3).with_edge(1));
            Add(std::move(cs), [&row](Rig& r, Cap& out) { return interpolation(r, out, row.pick, row.samples); },
                [&row](Rig& r) { return row.samples > 1 ? need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, row.samples) : std::string(); });
        }
    }
};
const InterpRegistrar kInterpRegistrar;

bool msaa_ps(Rig& r, Cap& out, const char* ps_body, bool coverage_out = false) {
    (void)coverage_out;
    Target target;
    const std::string src = std::string(kPsIn) + ps_body;
    Com<ID3D11PixelShader> ps = r.ps(src);
    Com<ID3D11VertexShader> vs = r.vs("float4 main(uint id : SV_VertexID) : SV_Position { float2 p[6] = {float2(-0.9, -0.8), float2(0.9, -0.7), float2(-0.1, 0.9), float2(0.2, 0.7), float2(0.95, -0.2), float2(0.0, -0.9)}; return float4(p[id], 0, 1); }\n");
    D3D11_RASTERIZER_DESC d = rdesc();
    d.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(d);
    if (!ps || !vs || !rs || !r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 4)) return false;
    r.bind(target);
    r.ctx->RSSetState(rs.get());
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->Draw(6, 0);
    return r.shot(out, target);
}

const Add kSampleIndex(spec("ps", "sv_sampleindex_per_sample_shading").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    return msaa_ps(r, out, "float4 main(VO i, uint s : SV_SampleIndex) : SV_Target { return float4(s == 0, s == 1, s == 2, 1) * 0.8 + 0.1; }\n");
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

const Add kCoverageIn(spec("ps", "sv_coverage_input").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    return msaa_ps(r, out, "float4 main(VO i, uint cov : SV_Coverage) : SV_Target { return float4(countbits(cov) / 4.0, ((cov >> 1) & 1) * 0.6, 0.3, 1); }\n");
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

const Add kCoverageOut(spec("ps", "sv_coverage_output").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    return msaa_ps(r, out,
                   "struct PO { float4 c : SV_Target; uint cov : SV_Coverage; };\n"
                   "PO main(VO i) { PO o; o.c = float4(0.9, 0.5, 0.2, 1); o.cov = ((uint(i.p.x) + uint(i.p.y)) & 1) ? 0x5 : 0xA; return o; }\n");
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

const Add kDiscard(spec("ps", "discard_checker").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) + "float4 main(VO i) : SV_Target { if (((uint(i.p.x) >> 3) + (uint(i.p.y) >> 3)) & 1) discard; return float4(i.uv, 0.5, 1); }\n");
});
const Add kClip(spec("ps", "clip_intrinsic").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) + "float4 main(VO i) : SV_Target { clip(sin(i.uv.x * 20.0) * cos(i.uv.y * 14.0) - 0.2); return float4(i.uv.yx, 0.6, 1); }\n");
});

const Add kDerivFine(spec("ps", "derivatives_fine").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) + "float4 main(VO i) : SV_Target { float v = sin(i.p.x * 0.35) * cos(i.p.y * 0.27) * 40.0; return float4(ddx_fine(v) * 0.05 + 0.5, ddy_fine(v) * 0.05 + 0.5, abs(ddx_fine(v) - ddy_fine(v)) * 0.05, 1); }\n");
});
const Add kDerivCoarse(spec("ps", "derivatives_coarse").tolerance(rr::Tolerance::psnr(20)), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) + "float4 main(VO i) : SV_Target { float v = sin(i.p.x * 0.35) * cos(i.p.y * 0.27) * 40.0; return float4(ddx_coarse(v) * 0.05 + 0.5, ddy_coarse(v) * 0.05 + 0.5, fwidth(v) * 0.03, 1); }\n");
});

const Add kBitOps(spec("ps", "integer_and_bit_operations").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) +
        "float4 main(VO i) : SV_Target { uint x = uint(i.p.x), y = uint(i.p.y); uint v = x * 2654435761u + y * 40503u;\n"
        "  uint a = countbits(v) * 8; uint b = (firstbithigh(v | 1) * 8) & 255; uint c = (reversebits(v) >> 24) & 255; uint d = ((v >> (x & 7)) & 0xFFu) ^ ((v << 3) >> 24);\n"
        "  int s = int(v) >> 28; uint e = (uint(abs(s)) * 17 + firstbitlow(v | 0x100)) & 255; return float4((a & 255) / 255.0, b / 255.0, c / 255.0, 1) * 0.5 + float4(d / 510.0, e / 510.0, ((x ^ y) & 255) / 510.0, 0); }\n");
});

const Add kMinPrecision(spec("ps", "min_precision_types").variant(1).tolerance(rr::Tolerance::blend(4)), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) + "float4 main(VO i) : SV_Target { min16float a = (min16float)i.uv.x; min16float b = a * a + (min16float)0.25 * (min16float)i.uv.y; min10float c = (min10float)(i.uv.y * 0.9); return float4(a, b, c, 1); }\n");
}, [](Rig& r) -> std::string {
    D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT support{};
    return SUCCEEDED(r.dev->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &support, sizeof support)) ? std::string() : std::string("min precision shader support");
});

const Add kDouble(spec("ps", "double_precision_math").variant(1).tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) +
        "float4 main(VO i) : SV_Target { double x = i.uv.x; double y = i.uv.y; double q = x * x * 1.0000001 + y * 0.3333333333333333 - x * y; double z = q * q + 0.125 * sqrt(float(abs(q))); return float4(float(q), float(z), float(x * y), 1); }\n");
}, [](Rig& r) -> std::string {
    D3D11_FEATURE_DATA_DOUBLES doubles{};
    return SUCCEEDED(r.dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof doubles)) && doubles.DoublePrecisionFloatShaderOps ? std::string() : std::string("double precision shader ops");
});

const Add kLoops(spec("ps", "flow_control_loops_switch").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    return fs_pass(r, out, std::string(kPsIn) +
        "float4 main(VO i) : SV_Target { float2 c = (i.uv - float2(0.65, 0.5)) * 2.6; float2 z = 0; int n = 0;\n"
        "  [loop] for (int k = 0; k < 24; ++k) { z = float2(z.x * z.x - z.y * z.y, 2 * z.x * z.y) + c; if (dot(z, z) > 4.0) break; n++; }\n"
        "  float3 col; switch (n % 4) { case 0: col = float3(1, 0.3, 0.1); break; case 1: col = float3(0.2, 0.9, 0.3); break; case 2: col = float3(0.2, 0.3, 1); break; default: col = float3(0.9, 0.9, 0.2); }\n"
        "  [branch] if (n >= 24) col = 0.05; return float4(col * (0.3 + 0.7 * n / 24.0), 1); }\n");
});

const Add kLargeCbuffer(spec("ps", "constant_buffer_64k_and_many_slots").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    std::vector<float> big(4096 * 4);
    for (int i = 0; i < 4096; ++i) {
        big[i * 4 + 0] = (i % 17) / 17.0f;
        big[i * 4 + 1] = (i % 29) / 29.0f;
        big[i * 4 + 2] = (i % 13) / 13.0f;
        big[i * 4 + 3] = 1;
    }
    Com<ID3D11Buffer> large = r.buf(4096 * 16, D3D11_BIND_CONSTANT_BUFFER, big.data());
    Com<ID3D11Buffer> small[13];
    ID3D11Buffer* raw[14];
    raw[0] = large.get();
    std::string src = "cbuffer Big : register(b0) { float4 big[4096]; };\n";
    std::string sum = "float4 acc = big[(uint(i.p.x) + 128 * uint(i.p.y)) % 4096];";
    for (int k = 0; k < 13; ++k) {
        const float v[4] = {k * 0.01f, (13 - k) * 0.01f, 0.02f, 0.0f};
        small[k] = r.cbuf(v, 16);
        if (!small[k]) return false;
        raw[k + 1] = small[k].get();
        src += "cbuffer C" + std::to_string(k) + " : register(b" + std::to_string(k + 1) + ") { float4 c" + std::to_string(k) + "; };\n";
        sum += " acc += c" + std::to_string(k) + ";";
    }
    src += kPsIn;
    src += "float4 main(VO i) : SV_Target { " + sum + " return float4(acc.rgb * 0.5, 1); }\n";
    Target target;
    Com<ID3D11PixelShader> ps = r.ps(src);
    if (!large || !ps || !r.color(target)) return false;
    r.bind(target);
    r.ctx->PSSetConstantBuffers(0, 14, raw);
    r.draw_fs(ps.get());
    return r.shot(out, target);
});

bool uav_scene(Rig& r, Cap& out, DXGI_FORMAT format, const char* ps_source, bool early) {
    (void)early;
    Target target;
    Com<ID3D11Texture2D> tex = r.tex(128, 128, format, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    if (!tex || !ps || !r.color(target)) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    if (!uav) return false;
    const UINT zero[4] = {0, 0, 0, 0};
    r.ctx->ClearUnorderedAccessViewUint(uav.get(), zero);
    r.bind(target);
    ID3D11RenderTargetView* rtv = target.view.get();
    ID3D11UnorderedAccessView* u = uav.get();
    const UINT initial = 0;
    r.ctx->OMSetRenderTargetsAndUnorderedAccessViews(1, &rtv, nullptr, 1, 1, &u, &initial);
    r.draw_fs(ps.get());
    rr::Image image;
    if (!r.read_image(tex.get(), format, 0, image)) return false;
    out.add("uav", std::move(image));
    return r.shot(out, target);
}

const Add kUavRgba8(spec("ps", "uav_write_rwtexture2d_rgba8").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    return uav_scene(r, out, DXGI_FORMAT_R8G8B8A8_UNORM,
                     "RWTexture2D<float4> u : register(u1);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
                     "float4 main(VO i) : SV_Target { u[uint2(i.p.xy)] = float4(i.uv.x, frac(i.uv.y * 4), 0.5, 1); return float4(i.uv.yx, 0.2, 1); }\n", false);
});
const Add kUavR32f(spec("ps", "uav_write_and_load_r32_float").tolerance(rr::Tolerance::floats(1e-5f)), [](Rig& r, Cap& out) {
    return uav_scene(r, out, DXGI_FORMAT_R32_FLOAT,
                     "RWTexture2D<float> u : register(u1);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
                     "float4 main(VO i) : SV_Target { uint2 c = uint2(i.p.xy); float old = u[c]; u[c] = old + i.uv.x * 2.0 + i.uv.y; u[c] = u[c] * 0.5 + 0.25; return float4(u[c], 0, 0.3, 1); }\n", false);
});

bool early_scene(Rig& r, Cap& out, bool early) {
    Target target;
    Depth depth;
    Com<ID3D11Texture2D> tex = r.tex(128, 128, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS);
    const std::string head = "RWTexture2D<uint> u : register(u1);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n";
    const std::string tail = "float4 main(VO i) : SV_Target { uint2 c = uint2(i.p.xy); InterlockedAdd(u[c], 1); return float4(i.uv, 0.4, 1); }\n";
    Com<ID3D11PixelShader> ps = r.ps(head + (early ? "[earlydepthstencil]\n" : "") + tail);
    Com<ID3D11VertexShader> vs = r.vs(Hlsl3d::kFsVsZ);
    Com<ID3D11Buffer> zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!tex || !ps || !vs || !zb || !ds || !r.color(target) || !r.depth(depth)) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    if (!uav) return false;
    const UINT zero[4] = {0, 0, 0, 0};
    r.ctx->ClearUnorderedAccessViewUint(uav.get(), zero);
    r.bind(target, &depth);
    ID3D11RenderTargetView* rtv = target.view.get();
    ID3D11UnorderedAccessView* u = uav.get();
    const UINT initial = 0;
    r.ctx->OMSetRenderTargetsAndUnorderedAccessViews(1, &rtv, depth.view.get(), 1, 1, &u, &initial);
    ID3D11Buffer* cb = zb.get();
    r.ctx->VSSetConstantBuffers(0, 1, &cb);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    D3D11_RASTERIZER_DESC d = rdesc();
    d.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(d);
    r.ctx->RSSetState(rs.get());
    const D3D11_RECT all{0, 0, 128, 128};
    const D3D11_RECT half{0, 0, 64, 128};
    const float near_plane[4] = {0.2f, 0, 0, 0};
    const float far_plane[4] = {0.7f, 0, 0, 0};
    r.ctx->UpdateSubresource(zb.get(), 0, nullptr, near_plane, 0, 0);
    r.ctx->RSSetScissorRects(1, &half);
    r.ctx->Draw(3, 0);
    r.ctx->UpdateSubresource(zb.get(), 0, nullptr, far_plane, 0, 0);
    r.ctx->RSSetScissorRects(1, &all);
    r.ctx->Draw(3, 0);
    rr::Image image;
    Bytes bytes;
    Com<ID3D11Texture2D> staging;
    D3D11_TEXTURE2D_DESC sd = D3d11Tools::desc2d(128, 128, DXGI_FORMAT_R32_UINT, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
    if (!r.ok(r.dev->CreateTexture2D(&sd, nullptr, staging.put()), "staging")) return false;
    r.ctx->CopyResource(staging.get(), tex.get());
    TexelImage texels;
    if (!r.read_texels(staging.get(), 0, texels)) return false;
    rr::Image counts(128, 128, rr::PixelKind::Rgba8Uint);
    for (UINT y = 0; y < 128; ++y)
        for (UINT x = 0; x < 128; ++x) {
            std::uint32_t v;
            std::memcpy(&v, texels.data.data() + (y * 128 + x) * 4, 4);
            std::uint8_t* px = counts.row(y) + x * 4;
            px[0] = static_cast<std::uint8_t>(v);
            px[1] = px[2] = 0;
            px[3] = 255;
        }
    out.add("counts", std::move(counts));
    return r.shot(out, target);
}
const Add kEarlyZ(spec("ps", "early_depth_stencil_with_uav_writes").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) { return early_scene(r, out, true); });
const Add kLateZ(spec("ps", "late_depth_with_uav_writes_runs_hidden_fragments").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) { return early_scene(r, out, false); });

const Add kAtomics(spec("ps", "atomics_on_structured_buffer").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    Target target;
    Com<ID3D11Buffer> buffer = r.buf(64 * 4, D3D11_BIND_UNORDERED_ACCESS, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    const char* source =
        "RWStructuredBuffer<uint> u : register(u1);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { uint x = uint(i.p.x), y = uint(i.p.y); uint bin = ((x >> 2) ^ (y >> 3)) & 15; uint o;\n"
        "  InterlockedAdd(u[bin], 1); InterlockedMax(u[16 + (bin & 3)], x + y); InterlockedMin(u[20 + (bin & 3)], 1000 + (x ^ y)); InterlockedOr(u[24], 1u << (bin & 31));\n"
        "  InterlockedXor(u[25], x * 7 + y); InterlockedAnd(u[26], ~(1u << (bin & 15))); InterlockedCompareExchange(u[27], 0, x + 1000, o); return float4(bin / 15.0, i.uv.y, 0.3, 1); }\n";
    Com<ID3D11PixelShader> ps = r.ps(source);
    if (!buffer || !ps || !r.color(target)) return false;
    Com<ID3D11UnorderedAccessView> uav = D3d11Tools::structured_uav(r.kit.dev, buffer.get(), 64);
    if (!uav) return r.fail("UAV creation failed");
    std::vector<UINT> init(64, 0);
    for (int k = 20; k < 24; ++k) init[k] = 0xFFFFFFFFu;
    init[26] = 0xFFFFFFFFu;
    r.ctx->UpdateSubresource(buffer.get(), 0, nullptr, init.data(), 0, 0);
    r.bind(target);
    ID3D11RenderTargetView* rtv = target.view.get();
    ID3D11UnorderedAccessView* u = uav.get();
    const UINT initial = 0;
    r.ctx->OMSetRenderTargetsAndUnorderedAccessViews(1, &rtv, nullptr, 1, 1, &u, &initial);
    r.draw_fs(ps.get());
    Bytes bytes;
    if (!r.read_bytes(buffer.get(), bytes)) return false;
    rr::Image words(4, 16, rr::PixelKind::Rgba32Uint);
    std::memcpy(words.bytes().data(), bytes.data(), 256);
    out.add("words", std::move(words));
    return r.shot(out, target);
});

const Add kRov(spec("ps", "rasterizer_ordered_view_blending").variant(1).tolerance(rr::Tolerance::floats(1e-4f)), [](Rig& r, Cap& out) {
    Target target;
    Com<ID3D11Texture2D> tex = r.tex(128, 128, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
    const char* source =
        "RasterizerOrderedTexture2D<float> rov : register(u1);\ncbuffer K : register(b0) { float4 k; };\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { uint2 c = uint2(i.p.xy); float v = rov[c]; rov[c] = v * 0.5 + k.x * (0.3 + 0.7 * i.uv.x); return float4(0, 0, 0, 1); }\n";
    Com<ID3D11PixelShader> ps = r.ps(source);
    Com<ID3D11Buffer> cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    if (!tex || !ps || !cb || !r.color(target)) return false;
    Com<ID3D11UnorderedAccessView> uav = r.uav(tex.get());
    if (!uav) return false;
    const float zero[4] = {0, 0, 0, 0};
    r.ctx->ClearUnorderedAccessViewFloat(uav.get(), zero);
    r.bind(target);
    ID3D11RenderTargetView* rtv = target.view.get();
    ID3D11UnorderedAccessView* u = uav.get();
    const UINT initial = 0;
    r.ctx->OMSetRenderTargetsAndUnorderedAccessViews(1, &rtv, nullptr, 1, 1, &u, &initial);
    ID3D11Buffer* b = cb.get();
    r.ctx->PSSetConstantBuffers(0, 1, &b);
    Basic basic;
    if (!basic.make(r, "struct VO { float4 p : SV_Position; float3 c : COLOR; };\nRasterizerOrderedTexture2D<float> rov : register(u1);\ncbuffer K : register(b0) { float4 k; };\nfloat4 main(VO i) : SV_Target { uint2 c = uint2(i.p.xy); float v = rov[c]; rov[c] = v * 0.5 + k.x * (0.3 + 0.7 * i.c.r); return float4(0, 0, 0, 1); }\n")) return false;
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    std::vector<Vtx> verts;
    for (int layer = 0; layer < 6; ++layer) {
        const auto fan = ring_fan(3, 0.9f - 0.08f * layer, 0.0f, 0.0f, layer * 0.7f);
        verts.insert(verts.end(), fan.begin(), fan.end());
    }
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    if (!vb) return false;
    basic.vertices(r, vb.get());
    for (int layer = 0; layer < 6; ++layer) {
        const float value[4] = {0.15f + 0.1f * layer, 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, value, 0, 0);
        r.ctx->Draw(9, static_cast<UINT>(layer * 9));
    }
    rr::Image image;
    if (!r.read_image(tex.get(), DXGI_FORMAT_R32_FLOAT, 0, image)) return false;
    out.add("rov", std::move(image));
    return true;
}, [](Rig& r) -> std::string {
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
    return SUCCEEDED(r.dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options, sizeof options)) && options.ROVsSupported ? std::string() : std::string("rasterizer ordered views");
});

const Add kClipDistance(spec("ps", "sv_clipdistance_and_cullDistance").tolerance(rr::Tolerance::raster(3, 1)), [](Rig& r, Cap& out) {
    Target target;
    const char* vs_source =
        "struct VI { float2 p : POSITION; float3 c : COLOR; };\nstruct VO { float4 p : SV_Position; float3 c : COLOR; float2 clip : SV_ClipDistance0; float cull : SV_CullDistance0; };\n"
        "VO main(VI i, uint id : SV_VertexID) { VO o; o.p = float4(i.p, 0, 1); o.c = i.c; o.clip = float2(i.p.x + 0.3, 0.5 - i.p.y); o.cull = (id / 3 == 2) ? -1.0 : 1.0; return o; }\n";
    const char* ps_source = "struct VO { float4 p : SV_Position; float3 c : COLOR; };\nfloat4 main(VO i) : SV_Target { return float4(i.c, 1); }\n";
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(vs_source, &code);
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(6, 0.9f, 0.0f, 0.0f, 0.2f));
    Com<ID3D11InputLayout> il = r.layout(layout2d(), code);
    if (!vs || !ps || !vb || !il || !r.color(target)) return false;
    r.bind(target);
    r.ctx->IASetInputLayout(il.get());
    const UINT stride = sizeof(Vtx), offset = 0;
    ID3D11Buffer* b = vb.get();
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->Draw(18, 0);
    return r.shot(out, target);
});

const Add kVertexTexture(spec("ps", "vertex_shader_texture_fetch_displacement").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    Bytes height(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const std::uint8_t h = static_cast<std::uint8_t>(128 + 100 * std::sin(x * 0.4f) * std::cos(y * 0.35f));
            height[(y * 32 + x) * 4] = h;
            height[(y * 32 + x) * 4 + 1] = h;
            height[(y * 32 + x) * 4 + 2] = h;
            height[(y * 32 + x) * 4 + 3] = 255;
        }
    Com<ID3D11Texture2D> tex = r.tex_data(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, height);
    const char* vs_source =
        "cbuffer F : register(b0) { column_major float4x4 m; };\nTexture2D t : register(t0);\nSamplerState s : register(s0);\n"
        "struct VI { float2 p : POSITION; float3 c : COLOR; };\nstruct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
        "VO main(VI i) { VO o; float h = t.SampleLevel(s, i.p * 0.5 + 0.5, 0).r; o.p = mul(m, float4(i.p.x, h * 0.6 - 0.3, i.p.y, 1)); o.c = float3(h, 1 - h, 0.4) * 0.8 + 0.1; return o; }\n";
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(vs_source, &code);
    Com<ID3D11PixelShader> ps = r.ps(Hlsl2d::kPs);
    std::vector<std::uint32_t> indices;
    std::vector<Vtx> grid = grid_mesh(24, 1.0f, indices);
    for (Vtx& v : grid) { v.x = std::fmax(-1.0f, std::fmin(1.0f, v.x)); v.y = std::fmax(-1.0f, std::fmin(1.0f, v.y)); }
    Com<ID3D11Buffer> vb = r.vbuf(grid);
    Com<ID3D11Buffer> ib = r.ibuf(indices);
    const rr::Mat4 m = rr::Mat4::perspective_d3d(0.9f, 1.0f, 0.5f, 20.0f) * rr::Mat4::translate(0, 0, -3.2f) * rr::Mat4::rotate_x(0.8f) * rr::Mat4::rotate_y(0.4f);
    Com<ID3D11Buffer> cb = r.cbuf(m.m, 64);
    Com<ID3D11InputLayout> il = r.layout(layout2d(), code);
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!tex || !vs || !ps || !vb || !ib || !cb || !il || !smp || !ds || !r.color(target) || !r.depth(depth)) return false;
    Com<ID3D11ShaderResourceView> srv = r.srv(tex.get());
    if (!srv) return false;
    r.bind(target, &depth);
    const UINT stride = sizeof(Vtx), offset = 0;
    ID3D11Buffer* b = vb.get();
    ID3D11Buffer* c = cb.get();
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* sp = smp.get();
    r.ctx->IASetInputLayout(il.get());
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R32_UINT, 0);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->VSSetConstantBuffers(0, 1, &c);
    r.ctx->VSSetShaderResources(0, 1, &v);
    r.ctx->VSSetSamplers(0, 1, &sp);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
    return r.shot(out, target);
});

struct DepthOutRow {
    const char* name;
    const char* semantic;
    D3D11_COMPARISON_FUNC func;
};

const DepthOutRow kDepthOut[] = {{"sv_depth", "SV_Depth", D3D11_COMPARISON_LESS}, {"sv_depth_greater_equal", "SV_DepthGreaterEqual", D3D11_COMPARISON_LESS},
                                 {"sv_depth_less_equal", "SV_DepthLessEqual", D3D11_COMPARISON_LESS}};

struct DepthOutRegistrar {
    DepthOutRegistrar() {
        for (const DepthOutRow& row : kDepthOut) {
            Add(spec("ps", (std::string("depth_output_") + row.name).c_str()).tolerance(rr::Tolerance::blend(3).with_edge(1)), [&row](Rig& r, Cap& out) {
                Target target;
                Depth depth;
                const bool greater = std::string(row.semantic) == "SV_DepthGreaterEqual";
                const bool less = std::string(row.semantic) == "SV_DepthLessEqual";
                const std::string src = std::string(kPsIn) + "struct PO { float4 c : SV_Target; float d : " + row.semantic + "; };\n" +
                    "PO main(VO i) { PO o; o.c = float4(i.uv, 0.5 + 0.5 * sin(i.p.x * 0.2), 1); float z = i.p.z; o.d = " +
                    (greater ? "z + 0.2 * i.uv.x" : less ? "z - 0.0001 * i.uv.x" : "0.2 + 0.6 * i.uv.x") + "; return o; }\n";
                Com<ID3D11PixelShader> ps = r.ps(src);
                Com<ID3D11VertexShader> vs = r.vs(Hlsl3d::kFsVsZ);
                Com<ID3D11Buffer> zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
                Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS_EQUAL, true));
                if (!ps || !vs || !zb || !ds || !r.color(target) || !r.depth(depth)) return false;
                r.bind(target, &depth);
                ID3D11Buffer* cb = zb.get();
                r.ctx->VSSetConstantBuffers(0, 1, &cb);
                r.ctx->OMSetDepthStencilState(ds.get(), 0);
                r.ctx->IASetInputLayout(nullptr);
                r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                r.ctx->VSSetShader(vs.get(), nullptr, 0);
                r.ctx->PSSetShader(ps.get(), nullptr, 0);
                const float z1[4] = {0.5f, 0, 0, 0};
                r.ctx->UpdateSubresource(zb.get(), 0, nullptr, z1, 0, 0);
                r.ctx->Draw(3, 0);
                rr::Image depth_image;
                if (!r.shot(out, target) || !r.depth_image(depth, depth_image)) return false;
                out.add("depth", std::move(depth_image));
                return true;
            }, [](Rig& r) { return need_fl(r, D3D_FEATURE_LEVEL_11_0); });
        }
    }
};
const DepthOutRegistrar kDepthOutRegistrar;

const Add kFrontFace(spec("ps", "sv_isfrontface_and_primitiveid").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    CubePipe cubes;
    const char* ps_source =
        "struct VsOut { float4 pos : SV_Position; float3 col : COLOR; };\n"
        "float4 main(VsOut i, bool front : SV_IsFrontFace, uint id : SV_PrimitiveID) : SV_Target { float3 c = front ? i.col : (1.0 - i.col) * 0.5; return float4(c * (0.6 + 0.4 * ((id & 3) / 3.0)), 1); }\n";
    if (!r.color(target) || !r.depth(depth) || !cubes.make(r, ps_source, D3D11_CULL_NONE)) return false;
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!ds) return false;
    r.bind(target, &depth);
    cubes.bind(r);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    cubes.draw(r, CubePipe::camera() * rr::Mat4::rotate_y(0.7f) * rr::Mat4::rotate_x(0.5f));
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
