#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

struct PartitionRow {
    const char* name;
};

const char* const kPartitions[] = {"integer", "fractional_even", "fractional_odd", "pow2"};

struct TessRegistrar {
    TessRegistrar() {
        for (const char* partition : kPartitions) {
            Add(spec("geom", (std::string("tess_tri_") + partition).c_str()).tolerance(rr::Tolerance::raster(2, 1)),
                [partition](Rig& r, Cap& out) { return run_patch_public(r, out, 3, false, partition, "triangle_cw", 5.3f, 4.6f, false); });
            Add(spec("geom", (std::string("tess_quad_") + partition).c_str()).tolerance(rr::Tolerance::raster(2, 1)),
                [partition](Rig& r, Cap& out) { return run_patch_public(r, out, 4, true, partition, "triangle_ccw", 6.4f, 3.2f, false); });
        }
        Add(spec("geom", "tess_tri_wireframe_fractional_odd").tolerance(rr::Tolerance::raster(2, 2)),
            [](Rig& r, Cap& out) { return run_patch_public(r, out, 3, false, "fractional_odd", "triangle_cw", 7.0f, 5.0f, true); });
        Add(spec("geom", "tess_quad_wireframe_fractional_even").tolerance(rr::Tolerance::raster(2, 2)),
            [](Rig& r, Cap& out) { return run_patch_public(r, out, 4, true, "fractional_even", "triangle_cw", 8.0f, 6.0f, true); });
        Add(spec("geom", "tess_quad_factor_64").tolerance(rr::Tolerance::raster(2, 2)),
            [](Rig& r, Cap& out) { return run_patch_public(r, out, 4, true, "integer", "triangle_cw", 64.0f, 64.0f, true); });
        Add(spec("geom", "tess_quad_factor_one").tolerance(rr::Tolerance::raster(2, 1)),
            [](Rig& r, Cap& out) { return run_patch_public(r, out, 16, true, "integer", "triangle_cw", 1.0f, 1.0f, false); });
        Add(spec("geom", "tess_bezier_patch_wireframe").tolerance(rr::Tolerance::raster(2, 2)),
            [](Rig& r, Cap& out) { return run_patch_public(r, out, 16, true, "fractional_odd", "triangle_cw", 9.0f, 9.0f, true); });
    }
};
const TessRegistrar kTessRegistrar;

const char* const kHullTail = "VO main(InputPatch<VO, 4> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";

const Add kEdgeFactors(spec("geom", "tess_quad_per_edge_factors").tolerance(rr::Tolerance::raster(2, 2)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    const std::string input = Hlsl2d::kVo;
    const std::string hull = input +
        "struct HC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\n"
        "HC pc(InputPatch<VO, 4> p) { HC h; h.e[0] = 2; h.e[1] = 9; h.e[2] = 4; h.e[3] = 16; h.i[0] = 3; h.i[1] = 7; return h; }\n"
        "[domain(\"quad\")][partitioning(\"fractional_even\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(4)][patchconstantfunc(\"pc\")]\n" + kHullTail;
    const std::string domain = input +
        "struct HC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\n"
        "[domain(\"quad\")] VO main(HC hc, float2 uv : SV_DomainLocation, const OutputPatch<VO, 4> p) { VO o;\n"
        "  float2 q = lerp(lerp(p[0].p.xy, p[1].p.xy, uv.x), lerp(p[2].p.xy, p[3].p.xy, uv.x), uv.y); o.p = float4(q, 0, 1); o.c = float3(uv, 0.6); return o; }\n";
    Com<ID3D11HullShader> h = r.hs(hull);
    Com<ID3D11DomainShader> d = r.ds(domain);
    const std::vector<Vtx> pts = {vtx(-0.8f, -0.8f, 0), vtx(0.8f, -0.7f, 0.2f), vtx(-0.7f, 0.8f, 0.4f), vtx(0.8f, 0.8f, 0.6f)};
    Com<ID3D11Buffer> vb = r.vbuf(pts);
    Com<ID3D11RasterizerState> rs = r.rast(rdesc(D3D11_FILL_WIREFRAME));
    if (!h || !d || !vb || !rs) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    basic.vertices(r, vb.get());
    r.ctx->HSSetShader(h.get(), nullptr, 0);
    r.ctx->DSSetShader(d.get(), nullptr, 0);
    r.ctx->RSSetState(rs.get());
    r.ctx->Draw(4, 0);
    return r.shot(out, target);
});

const Add kCulled(spec("geom", "tess_zero_factor_culls_patch").tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    const std::string input = Hlsl2d::kVo;
    const std::string hull = input +
        "struct HC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n"
        "HC pc(InputPatch<VO, 3> p, uint id : SV_PrimitiveID) { HC h; float f = (id % 2 == 0) ? 4 : 0; h.e[0] = h.e[1] = h.e[2] = f; h.i = f; return h; }\n"
        "[domain(\"tri\")][partitioning(\"integer\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(3)][patchconstantfunc(\"pc\")]\n"
        "VO main(InputPatch<VO, 3> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
    const std::string domain = input +
        "struct HC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n"
        "[domain(\"tri\")] VO main(HC hc, float3 bc : SV_DomainLocation, const OutputPatch<VO, 3> p) { VO o;\n"
        "  float2 q = p[0].p.xy * bc.x + p[1].p.xy * bc.y + p[2].p.xy * bc.z; o.p = float4(q, 0, 1); o.c = p[0].c * bc.x + p[1].c * bc.y + p[2].c * bc.z; return o; }\n";
    Com<ID3D11HullShader> h = r.hs(hull);
    Com<ID3D11DomainShader> d = r.ds(domain);
    std::vector<Vtx> pts;
    for (int k = 0; k < 4; ++k) {
        const float cx = -0.5f + (k % 2) * 1.0f;
        const float cy = 0.5f - (k / 2) * 1.0f;
        pts.push_back(vtx(cx - 0.4f, cy - 0.4f, 0.1f * k));
        pts.push_back(vtx(cx + 0.4f, cy - 0.35f, 0.1f * k + 0.3f));
        pts.push_back(vtx(cx, cy + 0.42f, 0.1f * k + 0.6f));
    }
    Com<ID3D11Buffer> vb = r.vbuf(pts);
    if (!h || !d || !vb) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    basic.vertices(r, vb.get());
    r.ctx->HSSetShader(h.get(), nullptr, 0);
    r.ctx->DSSetShader(d.get(), nullptr, 0);
    r.ctx->Draw(12, 0);
    return r.shot(out, target);
});

bool isoline(Rig& r, Cap& out, const char* partition) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    const std::string input = Hlsl2d::kVo;
    const std::string hull = input +
        "struct HC { float e[2] : SV_TessFactor; };\n"
        "HC pc(InputPatch<VO, 4> p) { HC h; h.e[0] = 6; h.e[1] = 12; return h; }\n"
        "[domain(\"isoline\")][partitioning(\"" + partition + "\")][outputtopology(\"line\")][outputcontrolpoints(4)][patchconstantfunc(\"pc\")]\n" + kHullTail;
    const std::string domain = input +
        "struct HC { float e[2] : SV_TessFactor; };\n"
        "[domain(\"isoline\")] VO main(HC hc, float2 uv : SV_DomainLocation, const OutputPatch<VO, 4> p) { VO o;\n"
        "  float t = uv.x; float u = 1 - t;\n"
        "  float2 q = u*u*u*p[0].p.xy + 3*u*u*t*p[1].p.xy + 3*u*t*t*p[2].p.xy + t*t*t*p[3].p.xy + float2(0, (uv.y - 0.5) * 0.5);\n"
        "  o.p = float4(q, 0, 1); o.c = lerp(p[0].c, p[3].c, t) * (0.5 + uv.y); return o; }\n";
    Com<ID3D11HullShader> h = r.hs(hull);
    Com<ID3D11DomainShader> d = r.ds(domain);
    const std::vector<Vtx> pts = {vtx(-0.85f, -0.3f, 0.0f), vtx(-0.3f, 0.8f, 0.2f), vtx(0.3f, -0.8f, 0.5f), vtx(0.85f, 0.3f, 0.8f)};
    Com<ID3D11Buffer> vb = r.vbuf(pts);
    if (!h || !d || !vb) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    basic.vertices(r, vb.get());
    r.ctx->HSSetShader(h.get(), nullptr, 0);
    r.ctx->DSSetShader(d.get(), nullptr, 0);
    r.ctx->Draw(4, 0);
    return r.shot(out, target);
}

const Add kIsoInteger(spec("geom", "tess_isoline_integer").tolerance(rr::Tolerance::raster(2, 2)), [](Rig& r, Cap& out) { return isoline(r, out, "integer"); });
const Add kIsoOdd(spec("geom", "tess_isoline_fractional_odd").tolerance(rr::Tolerance::raster(2, 2)), [](Rig& r, Cap& out) { return isoline(r, out, "fractional_odd"); });

const Add kTessPoints(spec("geom", "tess_point_output_topology").tolerance(rr::Tolerance::raster(2, 1)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    const std::string input = Hlsl2d::kVo;
    const std::string hull = input +
        "struct HC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n"
        "HC pc(InputPatch<VO, 3> p) { HC h; h.e[0] = h.e[1] = h.e[2] = 9; h.i = 9; return h; }\n"
        "[domain(\"tri\")][partitioning(\"integer\")][outputtopology(\"point\")][outputcontrolpoints(3)][patchconstantfunc(\"pc\")]\n"
        "VO main(InputPatch<VO, 3> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
    const std::string domain = input +
        "struct HC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n"
        "[domain(\"tri\")] VO main(HC hc, float3 bc : SV_DomainLocation, const OutputPatch<VO, 3> p) { VO o;\n"
        "  o.p = float4(p[0].p.xy * bc.x + p[1].p.xy * bc.y + p[2].p.xy * bc.z, 0, 1); o.c = p[0].c * bc.x + p[1].c * bc.y + p[2].c * bc.z; return o; }\n";
    Com<ID3D11HullShader> h = r.hs(hull);
    Com<ID3D11DomainShader> d = r.ds(domain);
    const std::vector<Vtx> pts = {vtx(-0.8f, -0.7f, 0), vtx(0.8f, -0.6f, 0.4f), vtx(0.0f, 0.8f, 0.7f)};
    Com<ID3D11Buffer> vb = r.vbuf(pts);
    if (!h || !d || !vb) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    basic.vertices(r, vb.get());
    r.ctx->HSSetShader(h.get(), nullptr, 0);
    r.ctx->DSSetShader(d.get(), nullptr, 0);
    r.ctx->Draw(3, 0);
    return r.shot(out, target);
});

const Add kPn(spec("geom", "tess_pn_triangles_octahedron").size(128, 128).tolerance(rr::Tolerance::psnr(28)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    if (!r.color(target) || !r.depth(depth)) return false;
    const char* vs_source =
        "struct VI { float3 p : POSITION; float3 n : NORMAL; };\nstruct VO { float3 p : POSITION; float3 n : NORMAL; };\nVO main(VI i) { VO o; o.p = i.p; o.n = i.n; return o; }\n";
    const char* hs_source =
        "struct VO { float3 p : POSITION; float3 n : NORMAL; };\n"
        "struct HC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; float3 b210 : B210; float3 b120 : B120; float3 b021 : B021; float3 b012 : B012; float3 b102 : B102; float3 b201 : B201; float3 b111 : B111; };\n"
        "float3 edge(float3 pa, float3 pb, float3 na) { return (2 * pa + pb - dot(pb - pa, na) * na) / 3; }\n"
        "HC pc(InputPatch<VO, 3> v) { HC h; h.e[0] = h.e[1] = h.e[2] = 6; h.i = 6;\n"
        "  h.b210 = edge(v[0].p, v[1].p, v[0].n); h.b120 = edge(v[1].p, v[0].p, v[1].n); h.b021 = edge(v[1].p, v[2].p, v[1].n); h.b012 = edge(v[2].p, v[1].p, v[2].n);\n"
        "  h.b102 = edge(v[2].p, v[0].p, v[2].n); h.b201 = edge(v[0].p, v[2].p, v[0].n);\n"
        "  float3 e = (h.b210 + h.b120 + h.b021 + h.b012 + h.b102 + h.b201) / 6; float3 vv = (v[0].p + v[1].p + v[2].p) / 3; h.b111 = e + (e - vv) / 2; return h; }\n"
        "[domain(\"tri\")][partitioning(\"fractional_odd\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(3)][patchconstantfunc(\"pc\")]\n"
        "VO main(InputPatch<VO, 3> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
    const char* ds_source =
        "cbuffer Frame : register(b0) { column_major float4x4 m; };\n"
        "struct VO { float3 p : POSITION; float3 n : NORMAL; };\n"
        "struct HC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; float3 b210 : B210; float3 b120 : B120; float3 b021 : B021; float3 b012 : B012; float3 b102 : B102; float3 b201 : B201; float3 b111 : B111; };\n"
        "struct DO { float4 p : SV_Position; float3 n : NORMAL; };\n"
        "[domain(\"tri\")] DO main(HC h, float3 bc : SV_DomainLocation, const OutputPatch<VO, 3> v) { DO o; float u = bc.x, w = bc.y, t = bc.z;\n"
        "  float3 pos = v[0].p * u*u*u + v[1].p * w*w*w + v[2].p * t*t*t + h.b210 * 3*u*u*w + h.b120 * 3*u*w*w + h.b201 * 3*u*u*t + h.b021 * 3*w*w*t + h.b102 * 3*u*t*t + h.b012 * 3*w*t*t + h.b111 * 6*u*w*t;\n"
        "  o.n = normalize(v[0].n * u + v[1].n * w + v[2].n * t); o.p = mul(m, float4(pos, 1)); return o; }\n";
    const char* ps_source = "struct DO { float4 p : SV_Position; float3 n : NORMAL; };\nfloat4 main(DO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.5, 0.7, 0.6)))); return float4(float3(0.9, 0.6, 0.3) * (0.2 + 0.8 * l), 1); }\n";
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(vs_source, &code);
    Com<ID3D11HullShader> hs = r.hs(hs_source);
    Com<ID3D11DomainShader> ds = r.ds(ds_source);
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    if (!vs || !hs || !ds || !ps) return false;
    const float v[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const int faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    std::vector<float> verts;
    for (const auto& f : faces)
        for (int k : f) {
            verts.insert(verts.end(), {v[k][0], v[k][1], v[k][2]});
            verts.insert(verts.end(), {v[k][0], v[k][1], v[k][2]});
        }
    Com<ID3D11Buffer> vb = r.buf(static_cast<UINT>(verts.size() * 4), D3D11_BIND_VERTEX_BUFFER, verts.data());
    const rr::Mat4 mvp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f) * rr::Mat4::translate(0, 0, -4) * rr::Mat4::rotate_y(0.5f) * rr::Mat4::rotate_x(0.4f);
    Com<ID3D11Buffer> cb = r.cbuf(mvp.m, 64);
    Com<ID3D11InputLayout> il = r.layout({elem("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0), elem("NORMAL", DXGI_FORMAT_R32G32B32_FLOAT, 12)}, code);
    Com<ID3D11DepthStencilState> ds_state = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!vb || !cb || !il || !ds_state) return false;
    r.bind(target, &depth);
    const UINT stride = 24;
    const UINT offset = 0;
    ID3D11Buffer* b = vb.get();
    ID3D11Buffer* c = cb.get();
    r.ctx->IASetInputLayout(il.get());
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->HSSetShader(hs.get(), nullptr, 0);
    r.ctx->DSSetShader(ds.get(), nullptr, 0);
    r.ctx->DSSetConstantBuffers(0, 1, &c);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->OMSetDepthStencilState(ds_state.get(), 0);
    r.ctx->Draw(24, 0);
    return r.shot(out, target);
});

const Add kDisplace(spec("geom", "tess_terrain_displacement_texture").size(256, 256).tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    if (!r.color(target, 256, 256) || !r.depth(depth, 256, 256)) return false;
    Bytes height(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const float h = 0.5f + 0.25f * std::sin(x * 0.2f) * std::cos(y * 0.17f) + 0.2f * std::sin((x + y) * 0.07f);
            const std::uint8_t b = static_cast<std::uint8_t>(std::fmin(std::fmax(h, 0.0f), 1.0f) * 255);
            height[(y * 64 + x) * 4 + 0] = b;
            height[(y * 64 + x) * 4 + 1] = b;
            height[(y * 64 + x) * 4 + 2] = b;
            height[(y * 64 + x) * 4 + 3] = 255;
        }
    Com<ID3D11Texture2D> tex = r.tex_chain(64, 64, height);
    Com<ID3D11ShaderResourceView> srv = r.srv(tex.get());
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    const char* vs_source = "struct VI { float2 p : POSITION; }; struct VO { float2 p : POSITION; }; VO main(VI i) { VO o; o.p = i.p; return o; }\n";
    const char* hs_source =
        "struct VO { float2 p : POSITION; };\n"
        "struct HC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\n"
        "HC pc(InputPatch<VO, 4> p) { HC h; h.e[0] = h.e[1] = h.e[2] = h.e[3] = 8; h.i[0] = h.i[1] = 8; return h; }\n"
        "[domain(\"quad\")][partitioning(\"integer\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(4)][patchconstantfunc(\"pc\")]\n"
        "VO main(InputPatch<VO, 4> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
    const char* ds_source =
        "cbuffer Frame : register(b0) { column_major float4x4 m; };\nTexture2D t : register(t0);\nSamplerState s : register(s0);\n"
        "struct VO { float2 p : POSITION; };\nstruct HC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\n"
        "struct DO { float4 p : SV_Position; float h : H; float3 n : NORMAL; };\n"
        "float hgt(float2 q) { return t.SampleLevel(s, q * 0.5 + 0.5, 0).r; }\n"
        "[domain(\"quad\")] DO main(HC hc, float2 uv : SV_DomainLocation, const OutputPatch<VO, 4> p) { DO o;\n"
        "  float2 q = lerp(lerp(p[0].p, p[1].p, uv.x), lerp(p[2].p, p[3].p, uv.x), uv.y); float h = hgt(q); float e = 0.02;\n"
        "  o.n = normalize(float3(hgt(q - float2(e, 0)) - hgt(q + float2(e, 0)), 2 * e / 0.8, hgt(q - float2(0, e)) - hgt(q + float2(0, e))));\n"
        "  o.h = h; o.p = mul(m, float4(q.x * 2, h * 0.8, q.y * 2, 1)); return o; }\n";
    const char* ps_source = "struct DO { float4 p : SV_Position; float h : H; float3 n : NORMAL; };\nfloat4 main(DO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.3)))); return float4(lerp(float3(0.2, 0.5, 0.2), float3(0.9, 0.85, 0.7), i.h) * (0.25 + 0.75 * l), 1); }\n";
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(vs_source, &code);
    Com<ID3D11HullShader> hs = r.hs(hs_source);
    Com<ID3D11DomainShader> ds = r.ds(ds_source);
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    std::vector<float> verts;
    for (int py = 0; py < 3; ++py)
        for (int px = 0; px < 3; ++px) {
            const float x0 = -1 + px * 2.0f / 3, x1 = -1 + (px + 1) * 2.0f / 3, y0 = -1 + py * 2.0f / 3, y1 = -1 + (py + 1) * 2.0f / 3;
            verts.insert(verts.end(), {x0, y0, x1, y0, x0, y1, x1, y1});
        }
    Com<ID3D11Buffer> vb = r.buf(static_cast<UINT>(verts.size() * 4), D3D11_BIND_VERTEX_BUFFER, verts.data());
    const rr::Mat4 mvp = rr::Mat4::perspective_d3d(0.9f, 1.0f, 0.5f, 30.0f) * rr::Mat4::translate(0, -0.3f, -4.2f) * rr::Mat4::rotate_x(0.7f) * rr::Mat4::rotate_y(0.4f);
    Com<ID3D11Buffer> cb = r.cbuf(mvp.m, 64);
    Com<ID3D11InputLayout> il = r.layout({elem("POSITION", DXGI_FORMAT_R32G32_FLOAT, 0)}, code);
    Com<ID3D11DepthStencilState> state = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!vs || !hs || !ds || !ps || !srv || !smp || !vb || !cb || !il || !state) return false;
    r.bind(target, &depth);
    const UINT stride = 8, offset = 0;
    ID3D11Buffer* b = vb.get();
    ID3D11Buffer* c = cb.get();
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* s = smp.get();
    r.ctx->IASetInputLayout(il.get());
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->HSSetShader(hs.get(), nullptr, 0);
    r.ctx->DSSetShader(ds.get(), nullptr, 0);
    r.ctx->DSSetConstantBuffers(0, 1, &c);
    r.ctx->DSSetShaderResources(0, 1, &v);
    r.ctx->DSSetSamplers(0, 1, &s);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->OMSetDepthStencilState(state.get(), 0);
    r.ctx->Draw(36, 0);
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11

namespace lsw::suites::render::d3d11 {
namespace {
const Add kIsoCoords(spec("geom", "tess_isoline_domain_coordinates").tolerance(rr::Tolerance::raster(2, 2)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    const std::string input = Hlsl2d::kVo;
    const std::string hull = input +
        "struct HC { float e[2] : SV_TessFactor; };\n"
        "HC pc(InputPatch<VO, 2> p) { HC h; h.e[0] = 5; h.e[1] = 8; return h; }\n"
        "[domain(\"isoline\")][partitioning(\"integer\")][outputtopology(\"line\")][outputcontrolpoints(2)][patchconstantfunc(\"pc\")]\n"
        "VO main(InputPatch<VO, 2> p, uint i : SV_OutputControlPointID) { return p[i]; }\n";
    const std::string domain = input +
        "struct HC { float e[2] : SV_TessFactor; };\n"
        "[domain(\"isoline\")] VO main(HC hc, float2 uv : SV_DomainLocation, const OutputPatch<VO, 2> p) { VO o;\n"
        "  o.p = float4(uv.x * 1.6 - 0.8, uv.y * 1.6 - 0.8 + 0.1 * sin(uv.x * 9.0), 0, 1); o.c = float3(uv, 0.7); return o; }\n";
    Com<ID3D11HullShader> h = r.hs(hull);
    Com<ID3D11DomainShader> d = r.ds(domain);
    Com<ID3D11Buffer> vb = r.vbuf(std::vector<Vtx>{vtx(0, 0, 0), vtx(0, 0, 0.5f)});
    if (!h || !d || !vb) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST);
    basic.vertices(r, vb.get());
    r.ctx->HSSetShader(h.get(), nullptr, 0);
    r.ctx->DSSetShader(d.get(), nullptr, 0);
    r.ctx->Draw(2, 0);
    return r.shot(out, target);
});
}
}
