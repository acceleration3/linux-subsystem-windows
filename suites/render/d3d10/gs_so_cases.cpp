#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

const char* kVoDecl =
    "struct VO { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD0; };\n";

std::vector<Vtx> triangles() {
    std::vector<Vtx> v;
    const float centers[4][2] = {{-0.5f, 0.5f}, {0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}};
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned k = 0; k < 3; ++k) {
            const float a = 1.5708f + 2.0944f * static_cast<float>(k) - 0.2f * static_cast<float>(i);
            v.push_back(vtx(centers[i][0] + 0.4f * std::cos(a), centers[i][1] + 0.4f * std::sin(a), 0.5f, palette(i * 3 + k)));
        }
    return v;
}

std::vector<Vtx> spokes_for_gs() {
    std::vector<Vtx> v;
    for (unsigned i = 0; i < 6; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / 6.0f;
        v.push_back(vtx(0.1f * std::cos(a), 0.1f * std::sin(a), 0.5f, palette(i)));
        v.push_back(vtx(0.7f * std::cos(a), 0.7f * std::sin(a), 0.5f, palette(i + 3)));
    }
    return v;
}

std::vector<Vtx> sprite_points(unsigned count) {
    std::vector<Vtx> v = ring_points(count, 0.75f);
    for (unsigned i = 0; i < v.size(); ++i) v[i].u = 0.04f + 0.015f * static_cast<float>(i % 5);
    return v;
}

void run_gs(Rig& r, rr::CaptureSet& out, const std::string& gs_source, const std::vector<Vtx>& vertices, D3D10_PRIMITIVE_TOPOLOGY top, bool wire = false) {
    r.begin();
    r.use_std(top);
    Com<ID3D10GeometryShader> gs = r.gs(std::string(kVoDecl) + gs_source);
    Com<ID3D10RasterizerState> rs = r.raster([&] { auto d = Rig::raster_desc(); if (wire) d.FillMode = D3D10_FILL_WIREFRAME; return d; }());
    if (!r.ok()) return;
    r.d->GSSetShader(gs.get());
    r.d->RSSetState(rs.get());
    r.draw_std(vertices, top);
    r.cap_main(out);
}

struct SoPass {
    Com<ID3D10VertexShader> vs;
    Com<ID3D10GeometryShader> gs;
    Com<ID3D10InputLayout> layout;
};

const char* kSoVs =
    "cbuffer C : register(b0) { float4 xf; };\n"
    "struct VI { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
    "struct VO { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
    "VO main(VI i) { VO o; o.p = float3(i.p.xy * xf.xy + xf.zw, i.p.z); o.c = i.c * float4(0.8, 1.0, 1.2, 1.0); o.uv = i.uv; return o; }\n";

const char* kSoGsTri =
    "struct VO { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
    "[maxvertexcount(6)] void main(triangle VO v[3], inout TriangleStream<VO> s) {\n"
    "  VO o; float3 mid = (v[0].p + v[1].p + v[2].p) / 3;\n"
    "  [unroll] for (int i = 0; i < 3; ++i) { o = v[i]; s.Append(o); } s.RestartStrip();\n"
    "  [unroll] for (int j = 0; j < 3; ++j) { o = v[j]; o.p = mid + (v[j].p - mid) * 0.4; o.c = float4(1 - v[j].c.rgb, 1); o.p.z = 0.4; s.Append(o); } }\n";

const std::vector<D3D10_SO_DECLARATION_ENTRY> kSoDecl = {{"POSITION", 0, 0, 3, 0}, {"COLOR", 0, 0, 4, 0}, {"TEXCOORD", 0, 0, 2, 0}};

std::vector<Vtx> so_source() {
    std::vector<Vtx> v = Geo::triangle(-0.45f, 0.4f, 0.45f, 0.5f, 1, 0.3f, 0.3f);
    std::vector<Vtx> b = Geo::triangle(0.45f, 0.3f, 0.4f, 0.5f, 0.3f, 1, 0.4f);
    std::vector<Vtx> c = Geo::quad(-0.7f, -0.9f, 0.6f, -0.2f, 0.5f, 0.3f, 0.5f, 1);
    v.insert(v.end(), b.begin(), b.end());
    v.insert(v.end(), c.begin(), c.end());
    return v;
}

void so_capture_pass(Rig& r, rr::CaptureSet& out, bool use_gs, UINT passes, bool append, bool via_drawauto) {
    r.begin();
    Com<ID3D10VertexShader> svs = r.vs(kSoVs);
    Com<ID3D10InputLayout> layout = r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                              {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                              {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0}});
    Com<ID3D10GeometryShader> sgs = r.gs_so(use_gs ? kSoGsTri : "", kSoDecl, sizeof(Vtx));
    Com<ID3D10Buffer> cb = r.cbuf(16);
    const std::vector<Vtx> source = so_source();
    Com<ID3D10Buffer> input = r.vbuf(source);
    const UINT capacity = 4096;
    Com<ID3D10Buffer> sob = r.vbuf(nullptr, capacity, D3D10_BIND_STREAM_OUTPUT);
    if (!r.ok()) return;
    D3D10_DEPTH_STENCIL_DESC off = Rig::depth_desc();
    Com<ID3D10DepthStencilState> no_depth = r.depth(off);
    r.d->OMSetDepthStencilState(no_depth.get(), 0);
    r.d->IASetInputLayout(layout.get());
    r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.d->VSSetShader(svs.get());
    r.d->GSSetShader(sgs.get());
    r.d->PSSetShader(nullptr);
    r.vs_cb(0, cb.get());
    r.std_vb(input.get());
    for (UINT pass = 0; pass < passes; ++pass) {
        const float xf[4] = {pass == 0 ? 1.0f : 0.5f, pass == 0 ? 1.0f : 0.5f, pass == 0 ? 0.0f : 0.3f, pass == 0 ? 0.0f : -0.3f};
        r.d->UpdateSubresource(cb.get(), 0, nullptr, xf, 0, 0);
        ID3D10Buffer* target = sob.get();
        const UINT offset = (pass == 0 || !append) ? 0 : 0xffffffffu;
        r.d->SOSetTargets(1, &target, &offset);
        r.d->Draw(static_cast<UINT>(source.size()), 0);
    }
    r.unbind_all();
    r.d->GSSetShader(nullptr);
    r.d->OMSetDepthStencilState(nullptr, 0);
    r.use_std();
    r.std_vb(sob.get());
    if (via_drawauto) {
        r.d->DrawAuto();
    } else {
        r.d->Draw(static_cast<UINT>(source.size()) * (use_gs ? 2 : 1) * passes, 0);
    }
    r.cap_main(out);
    if (!use_gs) {
        r.cap_buffer(out, "streamed", sob.get(), 3);
    }
}

const char* kSoVsWide =
    "cbuffer C : register(b0) { float4 xf; };\n"
    "struct VI { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
    "struct VO { float4 p : POSITION; float4 c : COLOR; float4 uv : TEXCOORD0; };\n"
    "VO main(VI i) { VO o; o.p = float4(i.p.xy * xf.xy + xf.zw, i.p.z, 1); o.c = i.c * float4(0.8, 1.0, 1.2, 1.0); o.uv = float4(i.uv, 0, 1); return o; }\n";

void so_multi_buffers(Rig& r, rr::CaptureSet& out, bool separate) {
    r.begin();
    Com<ID3D10VertexShader> svs = r.vs(separate ? kSoVsWide : kSoVs);
    Com<ID3D10InputLayout> layout = r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                              {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                              {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0}});
    std::vector<D3D10_SO_DECLARATION_ENTRY> decl;
    UINT stride = sizeof(Vtx);
    if (separate) {
        decl = {{"POSITION", 0, 0, 4, 0}, {"COLOR", 0, 0, 4, 1}, {"TEXCOORD", 0, 0, 4, 2}};
        stride = 0;
    } else {
        decl = kSoDecl;
    }
    Com<ID3D10GeometryShader> sgs = r.gs_so("", decl, stride);
    Com<ID3D10Buffer> cb = r.cbuf(16);
    const float xf[4] = {0.8f, 0.8f, 0.1f, 0.1f};
    r.d->UpdateSubresource(cb.get(), 0, nullptr, xf, 0, 0);
    const std::vector<Vtx> source = so_source();
    Com<ID3D10Buffer> input = r.vbuf(source);
    const UINT count = static_cast<UINT>(source.size());
    Com<ID3D10Buffer> bufs[3];
    for (auto& b : bufs) b = r.vbuf(nullptr, count * 40, D3D10_BIND_STREAM_OUTPUT);
    if (!r.ok()) return;
    Com<ID3D10DepthStencilState> no_depth = r.depth(Rig::depth_desc());
    r.d->OMSetDepthStencilState(no_depth.get(), 0);
    r.d->IASetInputLayout(layout.get());
    r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.d->VSSetShader(svs.get());
    r.d->GSSetShader(sgs.get());
    r.d->PSSetShader(nullptr);
    r.vs_cb(0, cb.get());
    r.std_vb(input.get());
    ID3D10Buffer* targets[3] = {bufs[0].get(), bufs[1].get(), bufs[2].get()};
    const UINT offsets[3] = {0, 0, 0};
    r.d->SOSetTargets(separate ? 3 : 1, targets, offsets);
    r.d->Draw(count, 0);
    r.unbind_all();
    r.d->GSSetShader(nullptr);
    r.d->OMSetDepthStencilState(nullptr, 0);
    if (separate) {
        Com<ID3D10VertexShader> vs = r.vs(Std::kVs);
        Com<ID3D10InputLayout> in = r.layout({{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                              {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
                                              {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 0, D3D10_INPUT_PER_VERTEX_DATA, 0}});
        Com<ID3D10PixelShader> ps = r.ps(Std::kPsUv);
        if (!r.ok()) return;
        ID3D10Buffer* vbs[3] = {bufs[0].get(), bufs[1].get(), bufs[2].get()};
        const UINT strides[3] = {16, 16, 16};
        r.d->IASetInputLayout(in.get());
        r.d->IASetVertexBuffers(0, 3, vbs, strides, offsets);
        r.d->VSSetShader(vs.get());
        r.d->PSSetShader(ps.get());
        r.d->Draw(count, 0);
    } else {
        r.use_std();
        r.std_vb(bufs[0].get());
        r.d->Draw(count, 0);
    }
    r.cap_main(out);
}

const Registrar kGs{[] {
    single(spec("gs", "point_sprites").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        run_gs(r, o,
               "[maxvertexcount(4)] void main(point VO v[1], inout TriangleStream<VO> s) { VO o = v[0]; float h = v[0].uv.x;\n"
               "  o.p = v[0].p + float4(-h, h, 0, 0); o.uv = float2(0, 0); s.Append(o); o.p = v[0].p + float4(h, h, 0, 0); o.uv = float2(1, 0); s.Append(o);\n"
               "  o.p = v[0].p + float4(-h, -h, 0, 0); o.uv = float2(0, 1); s.Append(o); o.p = v[0].p + float4(h, -h, 0, 0); o.uv = float2(1, 1); s.Append(o); }\n",
               sprite_points(48), D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
    });
    single(spec("gs", "amplify_disk_strip").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        run_gs(r, o,
               "[maxvertexcount(66)] void main(point VO v[1], inout TriangleStream<VO> s) { VO o = v[0]; float h = v[0].uv.x * 3;\n"
               "  [unroll] for (int k = 0; k <= 32; ++k) { float a = k * 6.2831853 / 32; o.p = v[0].p + float4(cos(a) * h, sin(a) * h, 0, 0); o.c = v[0].c * (0.5 + 0.5 * cos(a * 2));\n"
               "    s.Append(o); o.p = v[0].p; o.c = v[0].c; s.Append(o); } }\n",
               sprite_points(10), D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
    });
    single(spec("gs", "restart_strip_segments").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        run_gs(r, o,
               "[maxvertexcount(12)] void main(triangle VO v[3], inout TriangleStream<VO> s) { VO o;\n"
               "  [unroll] for (int e = 0; e < 3; ++e) { float2 a = v[e].p.xy, b = v[(e + 1) % 3].p.xy; float2 d = normalize(b - a); float2 n = float2(-d.y, d.x) * 0.03;\n"
               "    o = v[e]; o.p = float4(a + n, 0.5, 1); s.Append(o); o.p = float4(a - n, 0.5, 1); s.Append(o); o.p = float4(b + n, 0.5, 1); s.Append(o); o.p = float4(b - n, 0.5, 1); s.Append(o);\n"
               "    s.RestartStrip(); } }\n",
               triangles(), D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    });
    single(spec("gs", "line_to_triangles").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        run_gs(r, o,
               "[maxvertexcount(3)] void main(line VO v[2], inout TriangleStream<VO> s) { VO o = v[0]; float2 d = v[1].p.xy - v[0].p.xy; float2 n = float2(-d.y, d.x) * 0.15;\n"
               "  s.Append(v[0]); s.Append(v[1]); o.p = float4((v[0].p.xy + v[1].p.xy) * 0.5 + n, 0.5, 1); o.c = float4(1, 1, 1, 1); s.Append(o); }\n",
               spokes_for_gs(), D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
    });
    single(spec("gs", "primitive_id_colors").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
        Com<ID3D10GeometryShader> gs = r.gs(std::string(kVoDecl) +
                                            "struct GO { float4 p : SV_Position; float4 c : COLOR; uint id : SV_PrimitiveID; };\n"
                                            "[maxvertexcount(3)] void main(triangle VO v[3], uint pid : SV_PrimitiveID, inout TriangleStream<GO> s) { GO o;\n"
                                            "  [unroll] for (int i = 0; i < 3; ++i) { o.p = v[i].p; o.c = float4(frac(pid * 0.31), frac(pid * 0.57 + 0.2), frac(pid * 0.13 + 0.5), 1); o.id = pid; s.Append(o); } }\n");
        if (!r.ok()) return;
        r.d->GSSetShader(gs.get());
        std::vector<Vtx> v;
        for (unsigned i = 0; i < 16; ++i) {
            const float x = -0.9f + 0.45f * static_cast<float>(i % 4);
            const float y = 0.9f - 0.45f * static_cast<float>(i / 4);
            v.push_back(vtx(x, y, 0.5f, palette(0)));
            v.push_back(vtx(x + 0.4f, y, 0.5f, palette(0)));
            v.push_back(vtx(x, y - 0.4f, 0.5f, palette(0)));
        }
        r.draw_std(v);
        r.cap_main(o);
    });
    single(spec("gs", "constant_buffer_explode").tolerance(scene_tol(100, 1e-3, 16)), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        Com<ID3D10VertexShader> vs = r.vs(kSceneVs);
        Com<ID3D10InputLayout> layout = r.layout(mesh_elements());
        Com<ID3D10GeometryShader> gs = r.gs(
            "cbuffer C : register(b0) { float4x4 m; float4 tint; };\n"
            "struct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; };\n"
            "struct GO { float4 p : SV_Position; float4 c : COLOR; };\n"
            "[maxvertexcount(3)] void main(triangle VO v[3], uint pid : SV_PrimitiveID, inout TriangleStream<GO> s) { float3 cr = cross(v[1].w - v[0].w, v[2].w - v[0].w); float3 fn = length(cr) > 1e-6 ? cr / length(cr) : float3(0, 1, 0);\n"
            "  float shade = 0.3 + 0.7 * saturate(dot(fn, normalize(float3(0.5, 0.8, 0.6)))); GO o;\n"
            "  [unroll] for (int i = 0; i < 3; ++i) { o.p = mul(m, float4(v[i].w + fn * tint.x, 1)); o.c = float4(shade * (0.4 + 0.6 * frac(pid * 0.173)), shade * 0.7, shade * frac(pid * 0.41 + 0.3), 1); s.Append(o); } }\n");
        Com<ID3D10PixelShader> ps = r.ps(Std::kPs);
        MeshBuffers m = upload(r, Geo::sphere(10, 14, 1.0f));
        struct { rr::Mat4 m; float tint[4]; } c;
        c.m = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 20.0f) * rr::Mat4::translate(0, 0, -4.0f) * rr::Mat4::rotate_y(0.5f) * rr::Mat4::rotate_x(0.4f);
        c.tint[0] = 0.18f;
        Com<ID3D10Buffer> cb = r.cb(c);
        Com<ID3D10DepthStencilState> ds = r.depth([] { auto d = Rig::depth_desc(); d.DepthEnable = TRUE; return d; }());
        if (!r.ok()) return;
        r.vs_cb(0, cb.get());
        r.gs_cb(0, cb.get());
        r.d->OMSetDepthStencilState(ds.get(), 0);
        r.d->IASetInputLayout(layout.get());
        r.d->VSSetShader(vs.get());
        r.d->GSSetShader(gs.get());
        r.d->PSSetShader(ps.get());
        draw_mesh(r, m);
        r.cap_main(o);
    });
    single(spec("gs", "texture_fetch_displace").tolerance(rr::Tolerance::blend(3)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
        Com<ID3D10Texture2D> tex = r.rgba_tex(8, 8, rr::Pattern::noise(8, 8, 17));
        Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
        Com<ID3D10GeometryShader> gs = r.gs(std::string(kVoDecl) +
                                            "Texture2D t : register(t0);\n"
                                            "[maxvertexcount(4)] void main(point VO v[1], inout TriangleStream<VO> s) { VO o = v[0]; float4 tx = t.Load(int3(v[0].uv * 8, 0)); float h = 0.03 + 0.05 * tx.r;\n"
                                            "  o.c = float4(tx.rgb, 1); o.p = v[0].p + float4(-h, h, 0, 0); s.Append(o); o.p = v[0].p + float4(h, h, 0, 0); s.Append(o);\n"
                                            "  o.p = v[0].p + float4(-h, -h, 0, 0); s.Append(o); o.p = v[0].p + float4(h, -h, 0, 0); s.Append(o); }\n");
        if (!r.ok()) return;
        std::vector<Vtx> v;
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x) v.push_back(vtx(-0.8f + 0.228f * x, 0.8f - 0.228f * y, 0.5f, palette(0), (x + 0.5f) / 8, (y + 0.5f) / 8));
        r.d->GSSetShader(gs.get());
        ID3D10ShaderResourceView* sv = view.get();
        r.d->GSSetShaderResources(0, 1, &sv);
        r.draw_std(v, D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
        r.cap_main(o);
    });
    single(spec("gs", "render_target_array_index").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        Com<ID3D10Texture2D> array = r.tex2d(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, 1, 3);
        D3D10_RENDER_TARGET_VIEW_DESC vd{};
        vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        vd.ViewDimension = D3D10_RTV_DIMENSION_TEXTURE2DARRAY;
        vd.Texture2DArray.ArraySize = 3;
        Com<ID3D10RenderTargetView> view = r.rtv(array.get(), &vd);
        r.no_cull();
        r.use_std(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
        Com<ID3D10GeometryShader> gs = r.gs(std::string(kVoDecl) +
                                            "struct GO { float4 p : SV_Position; float4 c : COLOR; uint rt : SV_RenderTargetArrayIndex; };\n"
                                            "[maxvertexcount(9)] void main(triangle VO v[3], inout TriangleStream<GO> s) { GO o;\n"
                                            "  [unroll] for (uint k = 0; k < 3; ++k) { [unroll] for (int i = 0; i < 3; ++i) { o.p = v[i].p; o.p.x = o.p.x * (1.0 - 0.25 * k) + 0.1 * k; o.c = v[i].c; if (k == 1) o.c = o.c.gbra; if (k == 2) o.c = o.c.brga; o.rt = k; s.Append(o); } s.RestartStrip(); } }\n");
        if (!r.ok()) return;
        float clear[4] = {0.05f, 0.05f, 0.1f, 1};
        r.d->ClearRenderTargetView(view.get(), clear);
        ID3D10RenderTargetView* rt = view.get();
        r.d->OMSetRenderTargets(1, &rt, nullptr);
        r.viewport(128, 128);
        r.d->GSSetShader(gs.get());
        r.draw_std(triangles());
        for (UINT s = 0; s < 3; ++s) r.cap_tex(o, "slice" + std::to_string(s), array.get(), s);
    });
    single(spec("gs", "cube_face_replication").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        Com<ID3D10Texture2D> cube = r.tex2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, 1, 6, {}, {}, D3D10_RESOURCE_MISC_TEXTURECUBE);
        D3D10_RENDER_TARGET_VIEW_DESC vd{};
        vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        vd.ViewDimension = D3D10_RTV_DIMENSION_TEXTURE2DARRAY;
        vd.Texture2DArray.ArraySize = 6;
        Com<ID3D10RenderTargetView> view = r.rtv(cube.get(), &vd);
        r.no_cull();
        r.use_std(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
        Com<ID3D10GeometryShader> gs = r.gs(std::string(kVoDecl) +
                                            "struct GO { float4 p : SV_Position; float4 c : COLOR; uint rt : SV_RenderTargetArrayIndex; };\n"
                                            "[maxvertexcount(18)] void main(triangle VO v[3], inout TriangleStream<GO> s) { GO o;\n"
                                            "  [unroll] for (uint f = 0; f < 6; ++f) { [unroll] for (int i = 0; i < 3; ++i) { o.p = v[i].p; o.p.xy = o.p.xy * 0.8 + float2((int(f) % 3) - 1, (int(f) / 3) - 0.5) * 0.15;\n"
                                            "      o.c = v[i].c * (0.4 + 0.12 * f); o.c.a = 1; o.rt = f; s.Append(o); } s.RestartStrip(); } }\n");
        if (!r.ok()) return;
        float clear[4] = {0.0f, 0.0f, 0.0f, 1};
        r.d->ClearRenderTargetView(view.get(), clear);
        ID3D10RenderTargetView* rt = view.get();
        r.d->OMSetRenderTargets(1, &rt, nullptr);
        r.viewport(64, 64);
        r.d->GSSetShader(gs.get());
        r.draw_std(triangles());
        for (UINT s = 0; s < 6; ++s) r.cap_tex(o, "face" + std::to_string(s), cube.get(), s);
    });
    single(spec("gs", "viewport_array_index").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false);
        Com<ID3D10GeometryShader> gs = r.gs(std::string(kVoDecl) +
                                            "struct GO { float4 p : SV_Position; float4 c : COLOR; uint vp : SV_ViewportArrayIndex; };\n"
                                            "[maxvertexcount(12)] void main(triangle VO v[3], inout TriangleStream<GO> s) { GO o;\n"
                                            "  [unroll] for (uint k = 0; k < 4; ++k) { [unroll] for (int i = 0; i < 3; ++i) { o.p = v[i].p; o.c = v[i].c; if (k & 1) o.c = o.c.bgra; if (k & 2) o.c.rgb *= 0.5; o.vp = k; s.Append(o); } s.RestartStrip(); } }\n");
        if (!r.ok()) return;
        D3D10_VIEWPORT vps[4] = {{0, 0, 64, 64, 0, 1}, {64, 0, 64, 64, 0, 1}, {0, 64, 64, 64, 0, 1}, {64, 64, 64, 64, 0, 1}};
        r.d->RSSetViewports(4, vps);
        r.d->GSSetShader(gs.get());
        r.draw_std(triangles());
        r.cap_main(o);
    });
    single(spec("gs", "silhouette_extrusion").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        Com<ID3D10VertexShader> vs = r.vs(kSceneVs);
        Com<ID3D10InputLayout> layout = r.layout(mesh_elements());
        Com<ID3D10GeometryShader> gs = r.gs(
            "cbuffer C : register(b0) { float4x4 m; float4 light; };\n"
            "struct VO { float4 p : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; };\n"
            "struct GO { float4 p : SV_Position; float4 c : COLOR; };\n"
            "bool toward(float3 a, float3 b, float3 c) { return dot(cross(b - a, c - a), light.xyz) > 0; }\n"
            "[maxvertexcount(12)] void main(triangleadj VO v[6], inout TriangleStream<GO> s) { if (!toward(v[0].w, v[2].w, v[4].w)) return; GO o; o.c = float4(0.1, 0.6, 1.0, 1);\n"
            "  [unroll] for (int e = 0; e < 3; ++e) { int a = e * 2; int b = ((e + 1) % 3) * 2; int x = e * 2 + 1;\n"
            "    if (!toward(v[a].w, v[x].w, v[b].w)) { float3 pa = v[a].w, pb = v[b].w; o.p = mul(m, float4(pa, 1)); s.Append(o); o.p = mul(m, float4(pa - light.xyz * 3, 1)); s.Append(o);\n"
            "      o.p = mul(m, float4(pb, 1)); s.Append(o); o.p = mul(m, float4(pb - light.xyz * 3, 1)); s.Append(o); s.RestartStrip(); } } }\n");
        Com<ID3D10PixelShader> ps = r.ps(Std::kPs);
        Com<ID3D10PixelShader> body = r.ps("float4 main(float4 p : SV_Position, float3 n : NORMAL) : SV_Target { return float4(0.5 + 0.5 * normalize(n), 1); }\n");
        const MeshData torus = Geo::torus(16, 8, 0.8f, 0.35f);
        MeshBuffers m;
        m.vertices = r.vbuf(torus.vertices.data(), static_cast<UINT>(torus.vertices.size() * sizeof(MeshVertex)));
        const std::vector<std::uint32_t> adj = adjacency(torus);
        m.indices = r.ibuf32(adj);
        m.count = static_cast<UINT>(adj.size());
        MeshBuffers plain = upload(r, torus);
        struct { rr::Mat4 m; float light[4]; } c;
        c.m = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 30.0f) * rr::Mat4::translate(0, 0, -4.5f) * rr::Mat4::rotate_x(0.9f) * rr::Mat4::rotate_y(0.3f);
        c.light[0] = 0.6f; c.light[1] = 0.5f; c.light[2] = 0.3f; c.light[3] = 0;
        Com<ID3D10Buffer> cb = r.cb(c);
        auto rd = Rig::raster_desc();
        Com<ID3D10RasterizerState> rs = r.raster(rd);
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        if (!r.ok()) return;
        r.vs_cb(0, cb.get());
        r.gs_cb(0, cb.get());
        r.d->RSSetState(rs.get());
        r.d->OMSetDepthStencilState(ds.get(), 0);
        r.d->IASetInputLayout(layout.get());
        r.d->VSSetShader(vs.get());
        r.d->PSSetShader(body.get());
        draw_mesh(r, plain);
        const UINT stride = sizeof(MeshVertex);
        const UINT offset = 0;
        ID3D10Buffer* vb = m.vertices.get();
        r.d->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        r.d->IASetIndexBuffer(m.indices.get(), DXGI_FORMAT_R32_UINT, 0);
        r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ);
        r.d->GSSetShader(gs.get());
        r.d->PSSetShader(ps.get());
        r.d->DrawIndexed(m.count, 0, 0);
        r.cap_main(o);
    });
}};

const Registrar kSo{[] {
    single(spec("so", "vertex_shader_only_drawauto").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_capture_pass(r, o, false, 1, false, true); });
    single(spec("so", "vertex_shader_only_buffer_contents").tolerance(rr::Tolerance::blend()).tolerance_for("streamed", rr::Tolerance::floats(1e-5f, 1e-5f)),
           [](Rig& r, rr::CaptureSet& o) { so_capture_pass(r, o, false, 1, false, false); });
    single(spec("so", "geometry_shader_drawauto").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_capture_pass(r, o, true, 1, false, true); });
    single(spec("so", "append_two_passes").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_capture_pass(r, o, false, 2, true, true); });
    single(spec("so", "overwrite_two_passes").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_capture_pass(r, o, false, 2, false, true); });
    single(spec("so", "geometry_shader_append_two_passes").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_capture_pass(r, o, true, 2, true, true); });
    single(spec("so", "geometry_shader_overwrite_two_passes").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_capture_pass(r, o, true, 2, false, true); });
    single(spec("so", "interleaved_single_buffer").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_multi_buffers(r, o, false); });
    single(spec("so", "three_separate_buffers").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_multi_buffers(r, o, true); });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
