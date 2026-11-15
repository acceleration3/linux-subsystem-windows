#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const rr::Tolerance kTri = rr::Tolerance::raster(2, 1);

const char* const kGsHead = "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n";

std::vector<Vtx> scattered_points(unsigned n, unsigned seed) {
    std::vector<Vtx> out;
    std::uint32_t state = seed;
    for (unsigned i = 0; i < n; ++i) {
        const float x = rr::Pattern::unit_random(state) * 1.7f - 0.85f;
        const float y = rr::Pattern::unit_random(state) * 1.7f - 0.85f;
        out.push_back(vtx(x, y, rr::Pattern::unit_random(state)));
    }
    return out;
}

const Add kSprites(spec("geom", "gs_point_sprites").tolerance(kTri), [](Rig& r, Cap& out) {
    const std::string gs = std::string(kGsHead) +
        "[maxvertexcount(4)] void main(point VO v[1], inout TriangleStream<VO> s) { float sz = 0.03 + 0.05 * v[0].c.r; VO o; o.c = v[0].c;\n"
        "  o.p = float4(v[0].p.xy + float2(-sz, -sz), 0, 1); s.Append(o); o.p = float4(v[0].p.xy + float2(-sz, sz), 0, 1); s.Append(o);\n"
        "  o.p = float4(v[0].p.xy + float2(sz, -sz), 0, 1); s.Append(o); o.p = float4(v[0].p.xy + float2(sz, sz), 0, 1); s.Append(o); }\n";
    return run_topology_public(r, out, scattered_points(48, 7), D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, gs.c_str());
});

const Add kAmplify(spec("geom", "gs_amplification_max_vertices").tolerance(kTri), [](Rig& r, Cap& out) {
    const std::string gs = std::string(kGsHead) +
        "[maxvertexcount(96)] void main(triangle VO v[3], inout TriangleStream<VO> s) { VO o;\n"
        "  float2 m = (v[0].p.xy + v[1].p.xy + v[2].p.xy) / 3;\n"
        "  for (int k = 0; k < 32; ++k) { float t = k / 31.0; float sc = 1.0 - 0.85 * t; float a = t * 3.0;\n"
        "    [unroll] for (int j = 0; j < 3; ++j) { float2 d = (v[j].p.xy - m) * sc; float2 q = float2(d.x * cos(a) - d.y * sin(a), d.x * sin(a) + d.y * cos(a));\n"
        "      o.p = float4(m + q, 0, 1); o.c = lerp(v[j].c, float3(1 - t, t, 0.5), 0.5); s.Append(o); }\n"
        "    s.RestartStrip(); } }\n";
    std::vector<Vtx> tri = {vtx(-0.8f, -0.7f, 0.0f), vtx(0.8f, -0.7f, 0.3f), vtx(0.0f, 0.85f, 0.6f)};
    return run_topology_public(r, out, tri, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, gs.c_str());
});

const Add kGsInstancing(spec("geom", "gs_instancing").tolerance(kTri), [](Rig& r, Cap& out) {
    const std::string gs = std::string(kGsHead) +
        "[instance(4)][maxvertexcount(3)] void main(triangle VO v[3], uint inst : SV_GSInstanceID, inout TriangleStream<VO> s) { VO o;\n"
        "  float2 off = float2((inst & 1) ? 0.5 : -0.5, (inst & 2) ? -0.5 : 0.5);\n"
        "  [unroll] for (int j = 0; j < 3; ++j) { o.p = float4(v[j].p.xy * 0.45 + off, 0, 1); o.c = v[j].c * (0.5 + 0.17 * inst); s.Append(o); } }\n";
    std::vector<Vtx> tri = {vtx(-0.8f, -0.7f, 0.0f), vtx(0.8f, -0.7f, 0.3f), vtx(0.0f, 0.85f, 0.6f), vtx(-0.5f, 0.5f, 0.1f), vtx(0.5f, 0.0f, 0.5f), vtx(0.0f, -0.6f, 0.9f)};
    return run_topology_public(r, out, tri, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, gs.c_str());
});

const Add kRestart(spec("geom", "gs_restart_strip").tolerance(kTri), [](Rig& r, Cap& out) {
    const std::string gs = std::string(kGsHead) +
        "[maxvertexcount(24)] void main(point VO v[1], inout TriangleStream<VO> s) { VO o;\n"
        "  for (int k = 0; k < 3; ++k) { float2 c = v[0].p.xy + float2(0, (k - 1) * 0.28);\n"
        "    for (int j = 0; j < 4; ++j) { o.p = float4(c + float2((j >> 1) * 0.3 - 0.15, (j & 1) * 0.16 - 0.08), 0, 1); o.c = v[0].c * (0.4 + 0.3 * k + 0.1 * j); s.Append(o); }\n"
        "    s.RestartStrip(); } }\n";
    std::vector<Vtx> pts = {vtx(-0.55f, 0.0f, 0.0f), vtx(0.0f, 0.0f, 0.3f), vtx(0.55f, 0.0f, 0.6f)};
    return run_topology_public(r, out, pts, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, gs.c_str());
});

const Add kTriToLines(spec("geom", "gs_triangle_to_line_strip_outline").tolerance(rr::Tolerance::raster(16, 2)), [](Rig& r, Cap& out) {
    const std::string gs = std::string(kGsHead) +
        "[maxvertexcount(4)] void main(triangle VO v[3], inout LineStream<VO> s) { s.Append(v[0]); s.Append(v[1]); s.Append(v[2]); s.Append(v[0]); }\n";
    std::vector<Vtx> tris = ring_fan(5, 0.8f);
    return run_topology_public(r, out, tris, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, gs.c_str());
});

const Add kPrimitiveId(spec("geom", "gs_cull_by_primitive_id").tolerance(kTri), [](Rig& r, Cap& out) {
    const std::string gs = std::string(kGsHead) +
        "[maxvertexcount(3)] void main(triangle VO v[3], uint id : SV_PrimitiveID, inout TriangleStream<VO> s) { if ((id % 3) == 1) return; VO o;\n"
        "  [unroll] for (int j = 0; j < 3; ++j) { o = v[j]; o.c = v[j].c * (1.0 - 0.07 * id); s.Append(o); } }\n";
    return run_topology_public(r, out, ring_fan(12, 0.85f), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, gs.c_str());
});

const Add kSilhouette(spec("geom", "gs_adjacency_silhouette_edges").tolerance(rr::Tolerance::raster(16, 2)), [](Rig& r, Cap& out) {
    Target target;
    const float v[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const int faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    std::vector<std::uint32_t> indices;
    for (int f = 0; f < 8; ++f) {
        for (int e = 0; e < 3; ++e) {
            const int a = faces[f][e];
            const int b = faces[f][(e + 1) % 3];
            int opposite = -1;
            for (int g = 0; g < 8 && opposite < 0; ++g) {
                if (g == f) continue;
                for (int k = 0; k < 3; ++k)
                    if (faces[g][k] == b && faces[g][(k + 1) % 3] == a) opposite = faces[g][(k + 2) % 3];
            }
            indices.push_back(static_cast<std::uint32_t>(a));
            indices.push_back(static_cast<std::uint32_t>(opposite));
        }
    }
    const char* vs_source = "cbuffer F : register(b0) { column_major float4x4 m; };\nstruct VO { float4 p : SV_Position; float3 w : WORLD; };\nVO main(float3 p : POSITION) { VO o; o.p = mul(m, float4(p, 1)); o.w = p; return o; }\n";
    const char* gs_source =
        "cbuffer F : register(b0) { column_major float4x4 m; };\nstruct VO { float4 p : SV_Position; float3 w : WORLD; };\n"
        "struct GO { float4 p : SV_Position; float3 c : COLOR; };\n"
        "float3 face_n(float3 a, float3 b, float3 c) { return cross(b - a, c - a); }\n"
        "[maxvertexcount(12)] void main(triangleadj VO v[6], inout LineStream<GO> s) { float3 eye = float3(0.4, 0.6, 3.0); GO o;\n"
        "  float3 n0 = face_n(v[0].w, v[2].w, v[4].w); float vis0 = dot(n0, eye - v[0].w);\n"
        "  for (int e = 0; e < 3; ++e) { int i0 = e * 2, i1 = (e * 2 + 2) % 6, ia = e * 2 + 1;\n"
        "    float3 na = face_n(v[i0].w, v[ia].w, v[i1].w); float vis1 = dot(na, eye - v[i0].w);\n"
        "    bool sil = (vis0 > 0) != (vis1 > 0);\n"
        "    if (vis0 > 0 && sil) { o.c = float3(1, 0.9, 0.2); o.p = v[i0].p; s.Append(o); o.p = v[i1].p; s.Append(o); s.RestartStrip(); } } }\n";
    const char* ps_source = "struct GO { float4 p : SV_Position; float3 c : COLOR; };\nfloat4 main(GO i) : SV_Target { return float4(i.c, 1); }\n";
    const char* body_ps = "float4 main(float4 p : SV_Position) : SV_Target { return float4(0.1, 0.2, 0.45, 1); }\n";
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(vs_source, &code);
    Com<ID3D11GeometryShader> gs = r.gs(gs_source);
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    Com<ID3D11PixelShader> body = r.ps(body_ps);
    if (!vs || !gs || !ps || !body || !r.color(target)) return false;
    std::vector<float> positions;
    for (auto& p : v) positions.insert(positions.end(), {p[0], p[1], p[2]});
    Com<ID3D11Buffer> vb = r.buf(sizeof(float) * 18, D3D11_BIND_VERTEX_BUFFER, positions.data());
    Com<ID3D11Buffer> ib = r.ibuf(indices);
    const rr::Mat4 mvp = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f) * rr::Mat4::translate(0, 0, -3.5f) * rr::Mat4::rotate_y(0.4f) * rr::Mat4::rotate_x(0.5f);
    Com<ID3D11Buffer> cb = r.cbuf(mvp.m, 64);
    Com<ID3D11InputLayout> il = r.layout({elem("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, 0)}, code);
    if (!vb || !ib || !cb || !il) return false;
    r.bind(target);
    const UINT stride = 12, offset = 0;
    ID3D11Buffer* b = vb.get();
    ID3D11Buffer* c = cb.get();
    r.ctx->IASetInputLayout(il.get());
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R32_UINT, 0);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->VSSetConstantBuffers(0, 1, &c);
    r.ctx->GSSetConstantBuffers(0, 1, &c);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ);
    r.ctx->PSSetShader(body.get(), nullptr, 0);
    r.ctx->GSSetShader(nullptr, nullptr, 0);
    r.ctx->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->GSSetShader(gs.get(), nullptr, 0);
    r.ctx->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
    return r.shot(out, target);
});

bool array_routing(Rig& r, Cap& out, UINT slices, bool cube, bool viewports) {
    Target display;
    if (!r.color(display)) return false;
    const UINT size = 64;
    Com<ID3D11Texture2D> t;
    Com<ID3D11RenderTargetView> rtv;
    if (!viewports) {
        t = r.tex(size, size, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, slices, 1, nullptr, cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0);
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rd.Texture2DArray.ArraySize = slices;
        rtv = r.rtv(t.get(), &rd);
    } else {
        t = r.tex(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        rtv = r.rtv(t.get());
    }
    if (!t || !rtv) return false;
    const std::string gs = std::string() +
        "struct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
        "struct GO { float4 p : SV_Position; float3 c : COLOR; uint rt : SV_RenderTargetArrayIndex; uint vp : SV_ViewportArrayIndex; };\n"
        "[instance(" + std::to_string(slices) + ")][maxvertexcount(3)] void main(triangle VO v[3], uint inst : SV_GSInstanceID, inout TriangleStream<GO> s) { GO o;\n"
        "  [unroll] for (int j = 0; j < 3; ++j) { float a = " + std::string(viewports ? "inst * 0.3 + 0.25" : "inst * 0.6") + "; float2 q = v[j].p.xy; o.p = float4(q.x * cos(a) - q.y * sin(a), q.x * sin(a) + q.y * cos(a), 0, 1);\n"
        "    o.c = float3(v[j].c.bgr * 0.5 + float3(inst & 1, (inst >> 1) & 1, (inst >> 2) & 1) * 0.5); o.rt = " + (viewports ? "0" : "inst") + "; o.vp = " + (viewports ? "inst" : "0") + "; s.Append(o); } }\n";
    const char* ps_source = "struct GO { float4 p : SV_Position; float3 c : COLOR; };\nfloat4 main(GO i) : SV_Target { return float4(i.c, 1); }\n";
    Bytes code;
    Com<ID3D11VertexShader> vs = r.vs(Hlsl2d::kVs, &code);
    Com<ID3D11GeometryShader> geometry = r.gs(gs);
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    Basic basic;
    if (!basic.make(r) || !geometry || !ps) return false;
    const std::vector<Vtx> tri = {vtx(-0.7f, -0.6f, 0.0f), vtx(0.8f, -0.5f, 0.3f), vtx(0.1f, 0.8f, 0.6f)};
    Com<ID3D11Buffer> vb = r.vbuf(tri);
    if (!vb) return false;
    const float clear[4] = {0.05f, 0.05f, 0.08f, 1};
    ID3D11RenderTargetView* rv = rtv.get();
    r.ctx->OMSetRenderTargets(1, &rv, nullptr);
    r.ctx->ClearRenderTargetView(rtv.get(), clear);
    if (viewports) {
        D3D11_VIEWPORT vps[4];
        for (int k = 0; k < 4; ++k) vps[k] = {(k & 1) * 64.0f, (k >> 1) * 64.0f, 64, 64, 0, 1};
        r.ctx->RSSetViewports(4, vps);
    } else {
        r.viewport(0, 0, size, size);
    }
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->GSSetShader(geometry.get(), nullptr, 0);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->RSSetState(r.rast(rdesc()).get());
    r.ctx->Draw(3, 0);
    r.ctx->GSSetShader(nullptr, nullptr, 0);
    r.unbind_srvs();
    ID3D11RenderTargetView* none = nullptr;
    r.ctx->OMSetRenderTargets(1, &none, nullptr);
    Com<ID3D11ShaderResourceView> srv = r.srv(t.get());
    if (!srv) return false;
    std::string show;
    if (viewports) {
        show = "Texture2D t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return t.Sample(s, i.uv); }\n";
    } else if (cube) {
        show = "TextureCube t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
               "float4 main(VO i) : SV_Target { float a = i.uv.x * 6.2831853; float h = i.uv.y * 2.0 - 1.0; float q = sqrt(1.0 - h * h); return t.Sample(s, float3(cos(a) * q, h, sin(a) * q)); }\n";
    } else {
        show = "Texture2DArray t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
               "float4 main(VO i) : SV_Target { float2 q = frac(i.uv * 2.0); return t.Sample(s, float3(q, floor(i.uv.x * 2.0) + 2.0 * floor(i.uv.y * 2.0))); }\n";
    }
    Com<ID3D11PixelShader> show_ps = r.ps(show);
    Com<ID3D11SamplerState> sampler = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP));
    if (!show_ps || !sampler) return false;
    r.bind(display);
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* sp = sampler.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &sp);
    r.draw_fs(show_ps.get());
    return r.shot(out, display);
}

const Add kRtArray(spec("geom", "gs_render_target_array_index").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) { return array_routing(r, out, 4, false, false); });
const Add kCubeFaces(spec("geom", "gs_cube_faces_single_pass").tolerance(rr::Tolerance::psnr(24)), [](Rig& r, Cap& out) { return array_routing(r, out, 6, true, false); });
const Add kViewportArray(spec("geom", "gs_viewport_array_index").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) { return array_routing(r, out, 4, false, true); });

struct SoRig {
    Com<ID3D11Buffer> buffers[4];

    bool make(Rig& r, UINT count, UINT size) {
        for (UINT i = 0; i < count; ++i) {
            buffers[i] = r.buf(size, D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER);
            if (!buffers[i]) return false;
        }
        return true;
    }
};

const char* const kSoPassVs =
    "struct VI { float4 p : POSITION; float3 c : COLOR; };\nstruct VO { float4 p : SV_Position; float3 c : COLOR; };\nVO main(VI i) { VO o; o.p = i.p; o.c = i.c; return o; }\n";

std::vector<D3D11_INPUT_ELEMENT_DESC> so_layout() { return {elem("POSITION", DXGI_FORMAT_R32G32B32A32_FLOAT, 0), elem("COLOR", DXGI_FORMAT_R32G32B32_FLOAT, 16)}; }

std::vector<D3D11_SO_DECLARATION_ENTRY> so_decl(UINT stream = 0, UINT slot = 0) {
    return {{stream, "SV_Position", 0, 0, 4, static_cast<BYTE>(slot)}, {stream, "COLOR", 0, 0, 3, static_cast<BYTE>(slot)}};
}

void read_bytes_as_image(Rig& r, ID3D11Buffer* buffer, Cap& out, const char* label, unsigned vertices) {
    Bytes bytes;
    if (!r.read_bytes(buffer, bytes)) return;
    rr::Image image(7, vertices, rr::PixelKind::R32Float);
    std::memcpy(image.bytes().data(), bytes.data(), std::min(bytes.size(), image.bytes().size()));
    out.add(label, std::move(image));
}

bool draw_from(Rig& r, Basic& basic, ID3D11Buffer* buffer, UINT stride, D3D11_PRIMITIVE_TOPOLOGY topology) {
    r.ctx->SOSetTargets(0, nullptr, nullptr);
    basic.bind(r, topology);
    UINT offset = 0;
    r.ctx->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    r.ctx->GSSetShader(nullptr, nullptr, 0);
    r.ctx->DrawAuto();
    return true;
}

const Add kSoDrawAuto(spec("geom", "stream_output_gs_then_draw_auto").tolerance(kTri), [](Rig& r, Cap& out) {
    Target target;
    Basic pass;
    SoRig so;
    if (!r.color(target) || !pass.make(r, nullptr, kSoPassVs, so_layout()) || !so.make(r, 1, 4096)) return false;
    Basic first;
    if (!first.make(r)) return false;
    const std::string gs = std::string(kGsHead) +
        "[maxvertexcount(9)] void main(triangle VO v[3], inout TriangleStream<VO> s) { float2 m = (v[0].p.xy + v[1].p.xy + v[2].p.xy) / 3; VO o;\n"
        "  for (int k = 0; k < 3; ++k) { [unroll] for (int j = 0; j < 3; ++j) { o.p = float4(lerp(m, v[j].p.xy, 0.6 - 0.15 * k) + float2(0, 0.0), 0, 1); o.c = v[j].c * (1.0 - 0.3 * k); s.Append(o); } s.RestartStrip(); } }\n";
    Com<ID3D11GeometryShader> geometry = r.gs_so(gs, so_decl(), {28}, D3D11_SO_NO_RASTERIZED_STREAM);
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(5, 0.8f));
    if (!geometry || !vb) return false;
    r.bind(target);
    first.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    first.vertices(r, vb.get());
    r.ctx->GSSetShader(geometry.get(), nullptr, 0);
    ID3D11Buffer* targets = so.buffers[0].get();
    const UINT zero = 0;
    r.ctx->SOSetTargets(1, &targets, &zero);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    r.ctx->Draw(15, 0);
    r.ctx->SOSetTargets(0, nullptr, nullptr);
    r.ctx->IASetInputLayout(nullptr);
    pass.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    draw_from(r, pass, so.buffers[0].get(), 28, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (!r.shot(out, target)) return false;
    read_bytes_as_image(r, so.buffers[0].get(), out, "buffer", 45);
    return true;
});

const Add kSoVsOnly(spec("geom", "stream_output_vertex_shader_only").tolerance(kTri), [](Rig& r, Cap& out) {
    Target target;
    Basic pass;
    SoRig so;
    if (!r.color(target) || !pass.make(r, nullptr, kSoPassVs, so_layout()) || !so.make(r, 1, 4096)) return false;
    const char* vs_source =
        "struct VI { float2 p : POSITION; float3 c : COLOR; };\nstruct VO { float4 p : SV_Position; float3 c : COLOR; };\n"
        "VO main(VI i) { VO o; o.p = float4(i.p.x * 0.8 + 0.1 * sin(i.p.y * 5.0), i.p.y * 0.8, 0.5, 1); o.c = i.c.bgr; return o; }\n";
    Bytes code;
    Com<ID3D11GeometryShader> geometry = r.vs_so(vs_source, so_decl(), {28}, D3D11_SO_NO_RASTERIZED_STREAM, &code);
    Com<ID3D11VertexShader> vs = r.vs(vs_source);
    Com<ID3D11InputLayout> il = r.layout(layout2d(), code);
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(7, 0.9f));
    if (!geometry || !vs || !il || !vb) return false;
    r.bind(target);
    r.ctx->IASetInputLayout(il.get());
    UINT stride = sizeof(Vtx), offset = 0;
    ID3D11Buffer* b = vb.get();
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->GSSetShader(geometry.get(), nullptr, 0);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* t = so.buffers[0].get();
    const UINT zero = 0;
    r.ctx->SOSetTargets(1, &t, &zero);
    r.ctx->Draw(21, 0);
    draw_from(r, pass, so.buffers[0].get(), 28, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->IASetInputLayout(pass.il_.get());
    return r.shot(out, target);
});

const Add kSoAppend(spec("geom", "stream_output_append_offset").tolerance(kTri), [](Rig& r, Cap& out) {
    Target target;
    Basic pass, first;
    SoRig so;
    if (!r.color(target) || !pass.make(r, nullptr, kSoPassVs, so_layout()) || !first.make(r) || !so.make(r, 1, 4096)) return false;
    const std::string gs = std::string(kGsHead) + "[maxvertexcount(3)] void main(triangle VO v[3], inout TriangleStream<VO> s) { s.Append(v[0]); s.Append(v[1]); s.Append(v[2]); }\n";
    Com<ID3D11GeometryShader> geometry = r.gs_so(gs, so_decl(), {28}, D3D11_SO_NO_RASTERIZED_STREAM);
    Com<ID3D11Buffer> vb1 = r.vbuf(ring_fan(4, 0.9f, -0.0f, 0.0f, 0.0f));
    Com<ID3D11Buffer> vb2 = r.vbuf(ring_fan(3, 0.4f, 0.0f, 0.0f, 0.5f));
    if (!geometry || !vb1 || !vb2) return false;
    r.bind(target);
    r.ctx->GSSetShader(geometry.get(), nullptr, 0);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* t = so.buffers[0].get();
    UINT offset = 0;
    r.ctx->SOSetTargets(1, &t, &offset);
    first.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    first.vertices(r, vb1.get());
    r.ctx->GSSetShader(geometry.get(), nullptr, 0);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    r.ctx->Draw(12, 0);
    r.ctx->SOSetTargets(0, nullptr, nullptr);
    offset = 0xFFFFFFFFu;
    r.ctx->SOSetTargets(1, &t, &offset);
    first.vertices(r, vb2.get());
    r.ctx->Draw(9, 0);
    r.ctx->SOSetTargets(0, nullptr, nullptr);
    r.ctx->IASetInputLayout(nullptr);
    draw_from(r, pass, so.buffers[0].get(), 28, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return r.shot(out, target);
});

const Add kSoStreams(spec("geom", "stream_output_four_streams_rasterized_stream").tolerance(kTri), [](Rig& r, Cap& out) {
    Target target;
    Basic pass, first;
    SoRig so;
    if (!r.color(target) || !pass.make(r, nullptr, kSoPassVs, so_layout()) || !first.make(r) || !so.make(r, 4, 2048)) return false;
    const std::string gs = std::string(kGsHead) +
        "[maxvertexcount(12)] void main(point VO v[1], inout PointStream<VO> s0, inout PointStream<VO> s1, inout PointStream<VO> s2, inout PointStream<VO> s3) { VO o = v[0];\n"
        "  o.p = float4(v[0].p.xy + float2(-0.0, 0.0), 0, 1); o.c = v[0].c; s0.Append(o);\n"
        "  o.p = float4(v[0].p.xy * 0.5, 0, 1); o.c = v[0].c.gbr; s1.Append(o);\n"
        "  o.p = float4(v[0].p.xy * 0.25 + float2(0.5, -0.5), 0, 1); o.c = v[0].c.brg; s2.Append(o);\n"
        "  o.p = float4(v[0].p.xy * 1.1, 0, 1); o.c = 1 - v[0].c; s3.Append(o); }\n";
    std::vector<D3D11_SO_DECLARATION_ENTRY> decl;
    for (UINT k = 0; k < 4; ++k) {
        const auto d = so_decl(k, k);
        decl.insert(decl.end(), d.begin(), d.end());
    }
    Com<ID3D11GeometryShader> geometry = r.gs_so(gs, decl, {28, 28, 28, 28}, 2);
    std::vector<Vtx> pts;
    for (int i = 0; i < 24; ++i) pts.push_back(vtx(0.8f * std::cos(i * 0.9f) * (0.4f + i * 0.025f), 0.8f * std::sin(i * 0.9f) * (0.4f + i * 0.025f), i / 24.0f));
    Com<ID3D11Buffer> vb = r.vbuf(pts);
    if (!geometry || !vb) return false;
    r.bind(target);
    first.bind(r, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    first.vertices(r, vb.get());
    r.ctx->GSSetShader(geometry.get(), nullptr, 0);
    ID3D11Buffer* t[4] = {so.buffers[0].get(), so.buffers[1].get(), so.buffers[2].get(), so.buffers[3].get()};
    const UINT zero[4] = {0, 0, 0, 0};
    r.ctx->SOSetTargets(4, t, zero);
    r.ctx->PSSetShader(pass.ps_.get(), nullptr, 0);
    r.ctx->Draw(24, 0);
    r.ctx->SOSetTargets(0, nullptr, nullptr);
    r.ctx->GSSetShader(nullptr, nullptr, 0);
    for (int k = 0; k < 4; ++k) {
        pass.bind(r, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        UINT stride = 28, offset = 0;
        r.ctx->IASetVertexBuffers(0, 1, &t[k], &stride, &offset);
        r.ctx->DrawAuto();
    }
    for (int k = 0; k < 4; ++k) read_bytes_as_image(r, t[k], out, ("stream" + std::to_string(k)).c_str(), 24);
    return r.shot(out, target);
}, [](Rig& r) { return need_fl(r, D3D_FEATURE_LEVEL_11_0); });

}  // namespace

}  // namespace lsw::suites::render::d3d11
