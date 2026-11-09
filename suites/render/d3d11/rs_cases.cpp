#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kSlopedVs =
    "cbuffer Z : register(b0) { float4 zc; };\n"
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VO main(uint id : SV_VertexID) { VO o; float2 t = float2((id << 1) & 2, id & 2); float2 q = t * float2(2, -2) + float2(-1, 1);\n"
    "  o.p = float4(q, zc.x + zc.y * q.x, 1); o.uv = t; return o; }\n";

bool cube_state(Rig& r, Cap& out, const D3D11_RASTERIZER_DESC& desc) {
    Target target;
    Depth depth;
    CubePipe cubes;
    if (!r.color(target) || !r.depth(depth) || !cubes.make(r)) return false;
    Com<ID3D11RasterizerState> rs = r.rast(desc);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!rs || !ds) return false;
    r.bind(target, &depth);
    cubes.bind(r);
    r.ctx->RSSetState(rs.get());
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(-0.7f, 0, 0) * rr::Mat4::rotate_y(0.6f) * rr::Mat4::rotate_x(0.4f));
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(0.8f, 0.3f, 0.6f) * rr::Mat4::rotate_y(-0.9f) * rr::Mat4::rotate_z(0.5f) * rr::Mat4::scale(0.8f, 0.8f, 0.8f));
    return r.shot(out, target);
}

struct Registrar {
    Registrar() {
        const rr::Tolerance tri = rr::Tolerance::raster();
        Add(spec("rs", "fill_solid").tolerance(tri), [](Rig& r, Cap& out) { return cube_state(r, out, rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK)); });
        Add(spec("rs", "fill_wireframe").tolerance(rr::Tolerance::raster(16, 2)), [](Rig& r, Cap& out) { return cube_state(r, out, rdesc(D3D11_FILL_WIREFRAME, D3D11_CULL_NONE)); });
        struct CullRow {
            const char* name;
            D3D11_CULL_MODE cull;
        };
        static const CullRow culls[] = {{"none", D3D11_CULL_NONE}, {"front", D3D11_CULL_FRONT}, {"back", D3D11_CULL_BACK}};
        for (const CullRow& c : culls) {
            for (bool ccw : {false, true}) {
                Add(spec("rs", (std::string("cull_") + c.name + (ccw ? "_front_ccw" : "_front_cw")).c_str()).tolerance(tri), [&c, ccw](Rig& r, Cap& out) {
                    D3D11_RASTERIZER_DESC d = rdesc(D3D11_FILL_SOLID, c.cull);
                    d.FrontCounterClockwise = ccw;
                    return cube_state(r, out, d);
                });
            }
        }
    }
};
const Registrar kRegistrar;

bool bias_scene(Rig& r, Cap& out, int variant) {
    Target target;
    Depth depth;
    if (!r.color(target) || !r.depth(depth, 128, 128, DXGI_FORMAT_D24_UNORM_S8_UINT)) return false;
    Com<ID3D11VertexShader> vs = r.vs(kSlopedVs);
    Com<ID3D11PixelShader> back = r.ps("struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return float4(0.2, 0.3 + 0.4 * i.uv.y, 0.9, 1); }\n");
    Com<ID3D11PixelShader> front = r.ps("struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return float4(0.9, 0.6 * i.uv.x, 0.2, 1); }\n");
    Com<ID3D11Buffer> zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    Com<ID3D11DepthStencilState> write = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    Com<ID3D11DepthStencilState> test = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!vs || !back || !front || !zb || !write || !test) return false;
    r.bind(target, &depth);
    ID3D11Buffer* cb = zb.get();
    r.ctx->VSSetConstantBuffers(0, 1, &cb);
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    const float slope = variant == 0 ? 0.0f : 0.35f;
    const float plane[4] = {0.5f, slope, 0, 0};
    r.ctx->UpdateSubresource(zb.get(), 0, nullptr, plane, 0, 0);
    r.ctx->OMSetDepthStencilState(write.get(), 0);
    r.ctx->PSSetShader(back.get(), nullptr, 0);
    r.ctx->Draw(3, 0);
    D3D11_RASTERIZER_DESC d = rdesc();
    d.ScissorEnable = TRUE;
    D3D11_RECT halves[2] = {{0, 0, 64, 128}, {64, 0, 128, 128}};
    r.ctx->PSSetShader(front.get(), nullptr, 0);
    r.ctx->OMSetDepthStencilState(test.get(), 0);
    for (int h = 0; h < 2; ++h) {
        D3D11_RASTERIZER_DESC e = d;
        switch (variant) {
            case 0: e.DepthBias = h == 0 ? -64 : 0; break;
            case 1: e.SlopeScaledDepthBias = h == 0 ? -4.0f : 0.0f; break;
            case 2: e.SlopeScaledDepthBias = -4.0f; e.DepthBiasClamp = h == 0 ? 0.0f : -1e-7f; break;
            default: e.DepthBias = h == 0 ? 2000 : -2000; break;
        }
        Com<ID3D11RasterizerState> rs = r.rast(e);
        if (!rs) return false;
        r.ctx->RSSetState(rs.get());
        r.ctx->RSSetScissorRects(1, &halves[h]);
        r.ctx->Draw(3, 0);
    }
    return r.shot(out, target);
}

const Add kBias0(spec("rs", "depth_bias_constant").tolerance(rr::Tolerance::blend(2).with_edge(1)), [](Rig& r, Cap& out) { return bias_scene(r, out, 0); });
const Add kBias1(spec("rs", "depth_bias_slope_scaled").tolerance(rr::Tolerance::blend(2).with_edge(2)), [](Rig& r, Cap& out) { return bias_scene(r, out, 1); });
const Add kBias2(spec("rs", "depth_bias_clamp").tolerance(rr::Tolerance::blend(2).with_edge(2)), [](Rig& r, Cap& out) { return bias_scene(r, out, 2); });
const Add kBias3(spec("rs", "depth_bias_positive_and_negative").tolerance(rr::Tolerance::blend(2).with_edge(1)), [](Rig& r, Cap& out) { return bias_scene(r, out, 3); });

bool clip_scene(Rig& r, Cap& out, bool clip) {
    Target target;
    Depth depth;
    if (!r.color(target) || !r.depth(depth)) return false;
    Com<ID3D11VertexShader> vs = r.vs(kSlopedVs);
    Com<ID3D11PixelShader> ps = r.ps("struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return float4(i.uv.x, 0.5 + 0.5 * sin(i.p.z * 20.0), i.uv.y, 1); }\n");
    Com<ID3D11Buffer> zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    D3D11_RASTERIZER_DESC d = rdesc();
    d.DepthClipEnable = clip;
    Com<ID3D11RasterizerState> rs = r.rast(d);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS_EQUAL, true));
    if (!vs || !ps || !zb || !rs || !ds) return false;
    r.bind(target, &depth);
    ID3D11Buffer* cb = zb.get();
    r.ctx->VSSetConstantBuffers(0, 1, &cb);
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->RSSetState(rs.get());
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    const float plane[4] = {0.5f, 0.9f, 0, 0};
    r.ctx->UpdateSubresource(zb.get(), 0, nullptr, plane, 0, 0);
    r.ctx->Draw(3, 0);
    rr::Image depth_image;
    if (!r.shot(out, target) || !r.depth_image(depth, depth_image)) return false;
    out.add("depth", std::move(depth_image));
    return true;
}
const Add kClipOn(spec("rs", "depth_clip_enabled").tolerance(rr::Tolerance::blend(3).with_edge(1)), [](Rig& r, Cap& out) { return clip_scene(r, out, true); });
const Add kClipOff(spec("rs", "depth_clip_disabled").tolerance(rr::Tolerance::blend(3).with_edge(1)), [](Rig& r, Cap& out) { return clip_scene(r, out, false); });

bool scissor_scene(Rig& r, Cap& out, bool enabled) {
    Target target;
    if (!r.color(target)) return false;
    Com<ID3D11PixelShader> ps = r.ps("struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return float4(i.uv, 0.5 + 0.5 * sin(i.p.x * 0.3), 1); }\n");
    D3D11_RASTERIZER_DESC d = rdesc();
    d.ScissorEnable = enabled;
    Com<ID3D11RasterizerState> rs = r.rast(d);
    if (!ps || !rs) return false;
    r.bind(target);
    r.ctx->RSSetState(rs.get());
    const D3D11_RECT rects[2] = {{10, 12, 90, 70}, {0, 0, 128, 128}};
    r.ctx->RSSetScissorRects(1, &rects[0]);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}
const Add kScissorOn(spec("rs", "scissor_enabled").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) { return scissor_scene(r, out, true); });
const Add kScissorOff(spec("rs", "scissor_disabled").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) { return scissor_scene(r, out, false); });

const char* const kZPs = "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return float4(i.p.z, frac(i.p.x / 16.0), frac(i.p.y / 16.0), 1); }\n";

const Add kViewportDepth(spec("rs", "viewport_min_max_depth").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    if (!r.color(target) || !r.depth(depth)) return false;
    Com<ID3D11VertexShader> vs = r.vs(kSlopedVs);
    Com<ID3D11PixelShader> ps = r.ps(kZPs);
    Com<ID3D11Buffer> zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    if (!vs || !ps || !zb) return false;
    r.bind(target, &depth);
    ID3D11Buffer* cb = zb.get();
    r.ctx->VSSetConstantBuffers(0, 1, &cb);
    const float plane[4] = {0.5f, 0.4f, 0, 0};
    r.ctx->UpdateSubresource(zb.get(), 0, nullptr, plane, 0, 0);
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.viewport(0, 0, 64, 128, 0.2f, 0.6f);
    r.ctx->Draw(3, 0);
    r.viewport(64, 0, 64, 128, 0.6f, 1.0f);
    r.ctx->Draw(3, 0);
    rr::Image depth_image;
    if (!r.shot(out, target) || !r.depth_image(depth, depth_image)) return false;
    out.add("depth", std::move(depth_image));
    return true;
});

bool viewport_scene(Rig& r, Cap& out, float x, float y, float w, float h) {
    Target target;
    if (!r.color(target)) return false;
    Com<ID3D11PixelShader> ps = r.ps("struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return float4(i.uv, frac(i.p.x / 8.0) * 0.5 + frac(i.p.y / 8.0) * 0.5, 1); }\n");
    if (!ps) return false;
    r.bind(target);
    r.viewport(x, y, w, h);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}
const Add kFractional(spec("rs", "viewport_fractional").tolerance(rr::Tolerance::blend(3).with_edge(1)), [](Rig& r, Cap& out) { return viewport_scene(r, out, 10.5f, 7.25f, 100.75f, 90.5f); });
const Add kViewportOut(spec("rs", "viewport_extends_beyond_target").tolerance(rr::Tolerance::blend(3).with_edge(1)), [](Rig& r, Cap& out) { return viewport_scene(r, out, -40.0f, -30.0f, 200.0f, 200.0f); });

const Add kSixteen(spec("rs", "viewports_sixteen_with_scissors").tolerance(rr::Tolerance::blend(3).with_edge(1)), [](Rig& r, Cap& out) {
    Target target;
    if (!r.color(target)) return false;
    const char* gs_source =
        "struct VO { float4 p : SV_Position; float3 c : COLOR; };\nstruct GO { float4 p : SV_Position; float3 c : COLOR; uint vp : SV_ViewportArrayIndex; };\n"
        "[instance(16)][maxvertexcount(3)] void main(triangle VO v[3], uint inst : SV_GSInstanceID, inout TriangleStream<GO> s) { GO o;\n"
        "  [unroll] for (int j = 0; j < 3; ++j) { o.p = v[j].p; o.c = float3(frac(v[j].c.r + inst * 0.0625), v[j].c.g, frac(inst * 0.37)); o.vp = inst; s.Append(o); } }\n";
    const char* ps_source = "struct GO { float4 p : SV_Position; float3 c : COLOR; };\nfloat4 main(GO i) : SV_Target { return float4(i.c, 1); }\n";
    Com<ID3D11GeometryShader> gs = r.gs(gs_source);
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    Basic basic;
    if (!gs || !ps || !basic.make(r)) return false;
    D3D11_RASTERIZER_DESC d = rdesc();
    d.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(d);
    Com<ID3D11Buffer> vb = r.vbuf(std::vector<Vtx>{vtx(-0.9f, -0.9f, 0.0f), vtx(1.0f, -0.8f, 0.4f), vtx(-0.2f, 0.95f, 0.8f)});
    if (!rs || !vb) return false;
    r.bind(target);
    D3D11_VIEWPORT vps[16];
    D3D11_RECT rects[16];
    for (int k = 0; k < 16; ++k) {
        vps[k] = {(k % 4) * 32.0f, (k / 4) * 32.0f, 32, 32, 0, 1};
        rects[k] = {(k % 4) * 32 + 2, (k / 4) * 32 + 3, (k % 4) * 32 + 30, (k / 4) * 32 + 27};
    }
    r.ctx->RSSetViewports(16, vps);
    r.ctx->RSSetScissorRects(16, rects);
    r.ctx->RSSetState(rs.get());
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->GSSetShader(gs.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->Draw(3, 0);
    return r.shot(out, target);
}, [](Rig& r) { return need_fl(r, D3D_FEATURE_LEVEL_10_0); });

const Add kFillRule(spec("rs", "fill_rule_shared_edges_exact").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    std::vector<Vtx> verts;
    std::uint32_t state = 99;
    constexpr int n = 8;
    float px[n + 1][n + 1], py[n + 1][n + 1];
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const float jx = (i == 0 || i == n) ? 0.0f : (rr::Pattern::unit_random(state) - 0.5f) * 0.1f;
            const float jy = (j == 0 || j == n) ? 0.0f : (rr::Pattern::unit_random(state) - 0.5f) * 0.1f;
            px[j][i] = -1.0f + 2.0f * (i + jx * n * 0.5f) / n;
            py[j][i] = -1.0f + 2.0f * (j + jy * n * 0.5f) / n;
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float h = static_cast<float>((i * 3 + j * 5) % 11) / 11.0f;
            auto v = [&](int a, int b, float t) { return vtx(px[b][a], py[b][a], h + t); };
            verts.push_back(v(i, j, 0.0f));
            verts.push_back(v(i, j + 1, 0.05f));
            verts.push_back(v(i + 1, j, 0.1f));
            verts.push_back(v(i + 1, j, 0.1f));
            verts.push_back(v(i, j + 1, 0.05f));
            verts.push_back(v(i + 1, j + 1, 0.15f));
        }
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    if (!vb) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(static_cast<UINT>(verts.size()), 0);
    return r.shot(out, target);
});

const Add kDegenerate(spec("rs", "degenerate_and_huge_triangles").tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    std::vector<Vtx> verts = {
        vtx(-0.5f, -0.5f, 0.1f), vtx(0.5f, 0.5f, 0.2f), vtx(0.0f, 0.0f, 0.3f),
        vtx(0.2f, 0.2f, 0.4f), vtx(0.2f, 0.2f, 0.5f), vtx(0.2f, 0.2f, 0.6f),
        vtx(-200.0f, -150.0f, 0.7f), vtx(300.0f, -100.0f, 0.8f), vtx(10.0f, 250.0f, 0.9f),
        vtx(-0.6f, 0.6f, 0.0f), vtx(-0.1f, 0.65f, 0.15f), vtx(-0.35f, 0.95f, 0.3f),
        vtx(0.1f, -0.9f, 0.5f), vtx(0.8f, -0.9f, 0.6f), vtx(0.45f, -0.3f, 0.7f),
    };
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    if (!vb) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(static_cast<UINT>(verts.size()), 0);
    return r.shot(out, target);
});

bool line_scene(Rig& r, Cap& out, bool antialias, UINT samples, bool multisample) {
    Target target;
    Basic basic;
    if (!r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, samples) || !basic.make(r)) return false;
    std::vector<Vtx> verts;
    for (int i = 0; i < 12; ++i) {
        const float a = i * 0.52f;
        verts.push_back(vtx(0.1f * std::cos(a), 0.1f * std::sin(a), i / 12.0f));
        verts.push_back(vtx(0.9f * std::cos(a), 0.85f * std::sin(a), i / 12.0f + 0.05f));
    }
    D3D11_RASTERIZER_DESC d = rdesc();
    d.AntialiasedLineEnable = antialias;
    d.MultisampleEnable = multisample;
    Com<ID3D11RasterizerState> rs = r.rast(d);
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    if (!rs || !vb) return false;
    r.bind(target);
    r.ctx->RSSetState(rs.get());
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(static_cast<UINT>(verts.size()), 0);
    return r.shot(out, target);
}
const Add kAaLines(spec("rs", "antialiased_lines").tolerance(rr::Tolerance::psnr(22)), [](Rig& r, Cap& out) { return line_scene(r, out, true, 1, false); });
const Add kAliasedLines(spec("rs", "aliased_lines").tolerance(rr::Tolerance::psnr(22)), [](Rig& r, Cap& out) { return line_scene(r, out, false, 1, false); });
const Add kMsaaLinesAlias(spec("rs", "multisample_lines_aliased_4x").tolerance(rr::Tolerance::psnr(22)), [](Rig& r, Cap& out) { return line_scene(r, out, false, 4, true); },
                          [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
const Add kMsaaLinesAa(spec("rs", "multisample_lines_antialiased_4x").tolerance(rr::Tolerance::psnr(20)), [](Rig& r, Cap& out) { return line_scene(r, out, true, 4, true); },
                       [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

const Add kConservative(spec("rs", "conservative_rasterization_tier1").variant(1).tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target, 64, 64) || !basic.make(r)) return false;
    ID3D11Device3* raw3 = nullptr;
    r.dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(&raw3));
    Com<ID3D11Device3> dev3(raw3);
    if (!dev3) return r.fail("ID3D11Device3 is unavailable");
    D3D11_RASTERIZER_DESC2 d{};
    d.FillMode = D3D11_FILL_SOLID;
    d.CullMode = D3D11_CULL_NONE;
    d.DepthClipEnable = TRUE;
    d.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_ON;
    Com<ID3D11RasterizerState2> rs;
    if (!r.ok(dev3->CreateRasterizerState2(&d, rs.put()), "CreateRasterizerState2")) return false;
    std::vector<Vtx> verts = {vtx(-0.8f, -0.7f, 0.0f), vtx(-0.7f, -0.7f, 0.1f), vtx(-0.75f, -0.62f, 0.2f), vtx(0.0f, 0.1f, 0.4f), vtx(0.9f, 0.14f, 0.5f), vtx(0.45f, 0.2f, 0.6f),
                              vtx(-0.2f, -0.9f, 0.7f), vtx(0.7f, -0.2f, 0.8f), vtx(0.0f, 0.9f, 0.9f)};
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    if (!vb) return false;
    r.bind(target);
    r.viewport(0, 0, 64, 64);
    r.ctx->RSSetState(rs.get());
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(9, 0);
    return r.shot(out, target);
}, [](Rig& r) -> std::string {
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
    if (FAILED(r.dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options, sizeof options)) || options.ConservativeRasterizationTier == D3D11_CONSERVATIVE_RASTERIZATION_NOT_SUPPORTED) {
        return "conservative rasterization";
    }
    return {};
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
