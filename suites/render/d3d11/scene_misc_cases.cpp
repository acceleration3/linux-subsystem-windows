#include "render/d3d11/scene3d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

rr::Mat4 camera_vp() { return rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 80.0f) * rr::Mat4::rotate_x(0.5f) * rr::Mat4::translate(0.0f, -3.0f, -9.0f); }

const Add kForest(spec("scene", "instanced_forest_10000_instances_structured_buffer").size(256, 256).tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    if (!r.color(target, 256, 256) || !r.depth(depth, 256, 256)) return false;
    LitMesh cone;
    for (int i = 0; i < 8; ++i) {
        const float a0 = i * 0.785398f, a1 = (i + 1) * 0.785398f;
        const float p0[3] = {std::cos(a0), 0, std::sin(a0)}, p1[3] = {std::cos(a1), 0, std::sin(a1)};
        const float nx = std::cos((a0 + a1) * 0.5f), nz = std::sin((a0 + a1) * 0.5f);
        const std::uint32_t base = static_cast<std::uint32_t>(cone.vertices.size());
        cone.vertices.push_back({{p0[0], p0[1], p0[2]}, {nx, 0.5f, nz}, {0, 0}});
        cone.vertices.push_back({{p1[0], p1[1], p1[2]}, {nx, 0.5f, nz}, {1, 0}});
        cone.vertices.push_back({{0, 2.5f, 0}, {nx, 0.5f, nz}, {0.5f, 1}});
        cone.indices.insert(cone.indices.end(), {base, base + 2, base + 1});
    }
    GpuMesh trees;
    std::vector<float> instances;
    std::uint32_t state = 2024;
    for (int i = 0; i < 10000; ++i) {
        const float x = (rr::Pattern::unit_random(state) * 2 - 1) * 28.0f;
        const float z = (rr::Pattern::unit_random(state) * 2 - 1) * 28.0f;
        const float s = 0.18f + rr::Pattern::unit_random(state) * 0.22f;
        instances.insert(instances.end(), {x, z, s, rr::Pattern::unit_random(state)});
    }
    Com<ID3D11Buffer> data = r.buf(static_cast<UINT>(instances.size() * 4), D3D11_BIND_SHADER_RESOURCE, instances.data(), D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16);
    Com<ID3D11ShaderResourceView> srv = D3d11Tools::structured_srv(r.kit.dev, data.get(), 0, 10000);
    LitPipe pipe;
    Bytes code;
    const char* vs_source =
        "cbuffer Obj : register(b0) { column_major float4x4 vp; };\nStructuredBuffer<float4> inst : register(t0);\nstruct VI { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
        "struct VO { float4 pos : SV_Position; float3 n : NORMAL; float h : H; float t : T; };\n"
        "VO main(VI i, uint id : SV_InstanceID) { float4 d = inst[id]; VO o; float3 w = float3(d.x + i.p.x * d.z * 3.5, i.p.y * d.z * (1.0 + d.w), d.y + i.p.z * d.z * 3.5); o.pos = mul(vp, float4(w, 1)); o.n = i.n; o.h = i.p.y / 2.5; o.t = d.w; return o; }\n";
    Com<ID3D11VertexShader> vs = r.vs(vs_source, &code);
    Com<ID3D11PixelShader> ps = r.ps("struct VO { float4 pos : SV_Position; float3 n : NORMAL; float h : H; float t : T; };\nfloat4 main(VO i) : SV_Target { float l = saturate(dot(normalize(i.n), normalize(float3(0.4, 0.8, 0.3)))); return float4(lerp(float3(0.05, 0.25, 0.08), float3(0.25, 0.55, 0.2), i.h + 0.3 * i.t) * (0.35 + 0.65 * l), 1); }\n");
    Com<ID3D11InputLayout> il = r.layout(lit_layout(), code);
    Com<ID3D11Buffer> cb = r.cbuf(camera_vp().m, 64);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!trees.make(r, cone) || !data || !srv || !vs || !ps || !il || !cb || !ds) return false;
    const float sky[4] = {0.55f, 0.7f, 0.85f, 1};
    r.bind(target, &depth, sky);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->RSSetState(r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_NONE)).get());
    ID3D11Buffer* c = cb.get();
    ID3D11ShaderResourceView* v = srv.get();
    r.ctx->IASetInputLayout(il.get());
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->VSSetConstantBuffers(0, 1, &c);
    r.ctx->VSSetShaderResources(0, 1, &v);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    trees.bind(r);
    trees.draw(r, 10000);
    return r.shot(out, target);
});

const Add kUi(spec("scene", "ui_compositing_alpha_quads_scissored_panels").size(256, 256).tolerance(rr::Tolerance::blend(3).with_edge(1)), [](Rig& r, Cap& out) {
    Target target;
    if (!r.color(target, 256, 256)) return false;
    const char* vs_source =
        "cbuffer Q : register(b0) { float4 rect; float4 color; float4 extra; };\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; float4 c : COLOR; float4 e : EXTRA; };\n"
        "VO main(uint id : SV_VertexID) { VO o; float2 t = float2(id & 1, id >> 1); float2 p = lerp(rect.xy, rect.zw, t); o.p = float4(p.x / 128.0 - 1, 1 - p.y / 128.0, 0, 1); o.uv = t; o.c = color; o.e = extra; return o; }\n";
    const char* ps_source =
        "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; float4 c : COLOR; float4 e : EXTRA; };\n"
        "float4 main(VO i) : SV_Target { float2 d = abs(i.uv - 0.5) * 2; float corner = i.e.x; float r = length(max(d - (1 - corner), 0) / max(corner, 0.001)); float cover = saturate((1 - r) * 12.0) * (corner > 0 ? 1 : 1);\n"
        "  float3 col = i.c.rgb * (0.8 + 0.2 * i.uv.y); return float4(col, i.c.a * cover); }\n";
    Com<ID3D11VertexShader> vs = r.vs(vs_source);
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    Com<ID3D11Buffer> cb = r.buf(48, D3D11_BIND_CONSTANT_BUFFER);
    D3D11_BLEND_DESC bd = bdesc();
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    Com<ID3D11BlendState> blend = r.blend(bd);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    if (!vs || !ps || !cb || !blend || !rs) return false;
    const float bg[4] = {0.18f, 0.2f, 0.28f, 1};
    r.bind(target, nullptr, bg);
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    ID3D11Buffer* b = cb.get();
    r.ctx->VSSetConstantBuffers(0, 1, &b);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->RSSetState(rs.get());
    const float factor[4] = {};
    r.ctx->OMSetBlendState(blend.get(), factor, 0xffffffffu);
    auto quad = [&](float x0, float y0, float x1, float y1, float cr, float cg, float cb_, float a, float corner, const D3D11_RECT& clip) {
        const float data[12] = {x0, y0, x1, y1, cr, cg, cb_, a, corner, 0, 0, 0};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, data, 0, 0);
        r.ctx->RSSetScissorRects(1, &clip);
        r.ctx->Draw(4, 0);
    };
    const D3D11_RECT all{0, 0, 256, 256};
    quad(10, 10, 246, 246, 0.1f, 0.12f, 0.2f, 0.85f, 0.08f, all);
    for (int panel = 0; panel < 3; ++panel) {
        const float x = 20.0f + panel * 76.0f;
        const D3D11_RECT clip{static_cast<LONG>(x), 24, static_cast<LONG>(x + 66), 150 + panel * 20};
        quad(x, 24, x + 66, 232, 0.25f + 0.2f * panel, 0.35f, 0.7f - 0.2f * panel, 0.9f, 0.12f, clip);
        for (int row = 0; row < 7; ++row) quad(x + 4, 30.0f + row * 26.0f - panel * 9.0f, x + 62, 48.0f + row * 26.0f - panel * 9.0f, 0.9f, 0.7f - 0.08f * row, 0.2f + 0.1f * panel, 0.65f, 0.3f, clip);
    }
    for (int k = 0; k < 6; ++k) quad(30 + k * 28, 190 + (k % 2) * 14, 70 + k * 28, 236, 1.0f, 0.4f + 0.1f * k, 0.3f, 0.5f, 0.5f, all);
    return r.shot(out, target);
});

const Add kText(spec("scene", "text_rendering_glyph_atlas_alpha_blend").size(256, 256).tolerance(rr::Tolerance::blend(3).with_edge(1)), [](Rig& r, Cap& out) {
    Target target;
    Bytes atlas(128 * 128);
    for (int g = 0; g < 64; ++g) {
        const int gx = (g % 8) * 16, gy = (g / 8) * 16;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const bool bar_h = (y == 3 + g % 5) && x > 2 && x < 13;
                const bool bar_v = (x == 3 + (g / 5) % 8) && y > 2 && y < 13;
                const bool ring = std::fabs(std::sqrt(static_cast<float>((x - 8) * (x - 8) + (y - 8) * (y - 8))) - (3.0f + g % 4)) < 0.9f && (g % 3 == 0);
                atlas[(gy + y) * 128 + gx + x] = (bar_h || bar_v || ring) ? 255 : 0;
            }
    }
    D3D11_SUBRESOURCE_DATA init{atlas.data(), 128, 0};
    Com<ID3D11Texture2D> tex = r.tex(128, 128, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE, 1, 1, 1, &init);
    std::vector<float> glyphs;
    const char* text = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789 LSW RENDER SUITE D3D11 ALPHA ATLAS";
    int column = 0, line = 0;
    for (const char* c = text; *c; ++c) {
        if (column >= 20) { column = 0; ++line; }
        const int g = (*c * 7) % 64;
        glyphs.insert(glyphs.end(), {16.0f + column * 11.0f, 20.0f + line * 20.0f, static_cast<float>(g), static_cast<float>(line)});
        ++column;
    }
    Com<ID3D11Buffer> data = r.buf(static_cast<UINT>(glyphs.size() * 4), D3D11_BIND_SHADER_RESOURCE, glyphs.data(), D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16);
    Com<ID3D11ShaderResourceView> glyph_srv = D3d11Tools::structured_srv(r.kit.dev, data.get(), 0, static_cast<UINT>(glyphs.size() / 4));
    Com<ID3D11ShaderResourceView> atlas_srv = r.srv(tex.get());
    Com<ID3D11VertexShader> vs = r.vs("StructuredBuffer<float4> glyphs : register(t1);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; float l : L; };\n"
        "VO main(uint id : SV_VertexID, uint inst : SV_InstanceID) { float4 g = glyphs[inst]; float2 t = float2(id & 1, id >> 1); float2 p = g.xy + t * float2(12, 18); VO o; o.p = float4(p.x / 128.0 - 1, 1 - p.y / 128.0, 0, 1);\n"
        "  float2 cell = float2(fmod(g.z, 8), floor(g.z / 8)); o.uv = (cell + t) / 8.0; o.l = g.w; return o; }\n");
    Com<ID3D11PixelShader> ps = r.ps("Texture2D<float> atlas : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; float l : L; };\n"
        "float4 main(VO i) : SV_Target { float a = atlas.Sample(s, i.uv); return float4(lerp(float3(1, 0.9, 0.5), float3(0.6, 0.9, 1), i.l / 4.0), a); }\n");
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    D3D11_BLEND_DESC bd = bdesc();
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    Com<ID3D11BlendState> blend = r.blend(bd);
    if (!tex || !data || !glyph_srv || !atlas_srv || !vs || !ps || !smp || !blend || !r.color(target, 256, 256)) return false;
    const float bg[4] = {0.07f, 0.08f, 0.12f, 1};
    r.bind(target, nullptr, bg);
    ID3D11ShaderResourceView* views[2] = {atlas_srv.get(), glyph_srv.get()};
    ID3D11SamplerState* s = smp.get();
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->VSSetShaderResources(1, 1, &views[1]);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    r.ctx->PSSetShaderResources(0, 1, &views[0]);
    r.ctx->PSSetSamplers(0, 1, &s);
    const float factor[4] = {};
    r.ctx->OMSetBlendState(blend.get(), factor, 0xffffffffu);
    r.ctx->DrawInstanced(4, static_cast<UINT>(glyphs.size() / 4), 0, 0);
    return r.shot(out, target);
});

const Add kOit(spec("scene", "order_independent_transparency_per_pixel_linked_list").size(256, 256).tolerance(rr::Tolerance::blend(3).with_edge(1)), [](Rig& r, Cap& out) {
    Target target;
    if (!r.color(target, 256, 256)) return false;
    Com<ID3D11Texture2D> heads = r.tex(256, 256, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
    Com<ID3D11Buffer> nodes = r.buf(256 * 256 * 4 * 32, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 32);
    if (!heads || !nodes) return false;
    Com<ID3D11UnorderedAccessView> head_uav = r.uav(heads.get());
    Com<ID3D11ShaderResourceView> head_srv = r.srv(heads.get());
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 256 * 256 * 4;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;
    Com<ID3D11UnorderedAccessView> node_uav = r.uav(nodes.get(), &ud);
    Com<ID3D11ShaderResourceView> node_srv = D3d11Tools::structured_srv(r.kit.dev, nodes.get(), 0, 256 * 256 * 4);
    const char* insert_ps =
        "struct Node { float4 color; float depth; uint next; float2 pad; };\nRWTexture2D<uint> heads : register(u1);\nRWStructuredBuffer<Node> nodes : register(u2);\n"
        "cbuffer C : register(b1) { float4 col; };\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { uint idx = nodes.IncrementCounter(); uint prev; InterlockedExchange(heads[uint2(i.p.xy)], idx + 1, prev); Node n; n.color = float4(col.rgb * (0.7 + 0.3 * i.uv.y), 0.55); n.depth = i.p.z; n.next = prev; n.pad = 0; nodes[idx] = n; return 0; }\n";
    const char* resolve_ps =
        "struct Node { float4 color; float depth; uint next; float2 pad; };\nTexture2D<uint> heads : register(t0);\nStructuredBuffer<Node> nodes : register(t1);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { float4 c[8]; float z[8]; int n = 0; uint link = heads.Load(int3(int2(i.p.xy), 0)); while (link != 0 && n < 8) { Node nd = nodes[link - 1]; c[n] = nd.color; z[n] = nd.depth; ++n; link = nd.next; }\n"
        "  for (int a = 1; a < n; ++a) { float4 cc = c[a]; float zz = z[a]; int b = a - 1; while (b >= 0 && z[b] < zz) { c[b + 1] = c[b]; z[b + 1] = z[b]; --b; } c[b + 1] = cc; z[b + 1] = zz; }\n"
        "  float3 color = float3(0.16 + 0.1 * i.uv.y, 0.18, 0.26); for (int k = 0; k < n; ++k) color = lerp(color, c[k].rgb, c[k].a); return float4(color, 1); }\n";
    Com<ID3D11VertexShader> vs = r.vs(Hlsl3d::kFsVsZ);
    Com<ID3D11PixelShader> insert = r.ps(insert_ps);
    Com<ID3D11PixelShader> resolve = r.ps(resolve_ps);
    Com<ID3D11Buffer> zb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    Com<ID3D11Buffer> colb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    if (!head_uav || !head_srv || !node_uav || !node_srv || !vs || !insert || !resolve || !zb || !colb || !rs) return false;
    const UINT zero[4] = {};
    r.ctx->ClearUnorderedAccessViewUint(head_uav.get(), zero);
    r.bind(target);
    ID3D11RenderTargetView* rtv = target.view.get();
    ID3D11UnorderedAccessView* uavs[2] = {head_uav.get(), node_uav.get()};
    const UINT counts[2] = {0, 0};
    r.ctx->OMSetRenderTargetsAndUnorderedAccessViews(1, &rtv, nullptr, 1, 2, uavs, counts);
    r.ctx->RSSetState(rs.get());
    ID3D11Buffer* b0 = zb.get();
    ID3D11Buffer* b1 = colb.get();
    r.ctx->VSSetConstantBuffers(0, 1, &b0);
    r.ctx->PSSetConstantBuffers(1, 1, &b1);
    r.ctx->IASetInputLayout(nullptr);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->PSSetShader(insert.get(), nullptr, 0);
    const struct {
        D3D11_RECT rect;
        float z;
        float c[4];
    } layers[6] = {{{30, 30, 170, 170}, 0.30f, {1.0f, 0.2f, 0.2f, 0}}, {{80, 50, 220, 190}, 0.50f, {0.2f, 1.0f, 0.3f, 0}}, {{50, 90, 200, 230}, 0.20f, {0.2f, 0.4f, 1.0f, 0}},
                   {{110, 20, 240, 130}, 0.65f, {1.0f, 1.0f, 0.2f, 0}}, {{20, 120, 150, 240}, 0.40f, {1.0f, 0.3f, 1.0f, 0}}, {{90, 100, 190, 200}, 0.10f, {0.2f, 1.0f, 1.0f, 0}}};
    for (const auto& l : layers) {
        const float zc[4] = {l.z, 0, 0, 0};
        r.ctx->UpdateSubresource(zb.get(), 0, nullptr, zc, 0, 0);
        r.ctx->UpdateSubresource(colb.get(), 0, nullptr, l.c, 0, 0);
        r.ctx->RSSetScissorRects(1, &l.rect);
        r.ctx->Draw(3, 0);
    }
    ID3D11UnorderedAccessView* none[2] = {};
    r.ctx->OMSetRenderTargetsAndUnorderedAccessViews(1, &rtv, nullptr, 1, 2, none, nullptr);
    const D3D11_RECT all{0, 0, 256, 256};
    r.ctx->RSSetScissorRects(1, &all);
    ID3D11ShaderResourceView* views[2] = {head_srv.get(), node_srv.get()};
    r.ctx->PSSetShaderResources(0, 2, views);
    r.draw_fs(resolve.get());
    return r.shot(out, target);
});

const Add kPbr(spec("scene", "pbr_spheres_ibl_cubemap_and_brdf_lut").size(256, 256).tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    LitPipe pipe;
    GpuMesh sphere;
    if (!r.color(target, 256, 256) || !r.depth(depth, 256, 256) || !pipe.make(r) || !sphere.make(r, LitMesh::sphere(32, 20))) return false;
    std::vector<Bytes> faces;
    std::vector<D3D11_SUBRESOURCE_DATA> data;
    const unsigned size = 32;
    for (int f = 0; f < 6; ++f) {
        Bytes px(size * size * 4);
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) {
                const float u = x / float(size - 1) * 2 - 1, v = y / float(size - 1) * 2 - 1;
                float d[3];
                switch (f) {
                    case 0: d[0] = 1; d[1] = -v; d[2] = -u; break;
                    case 1: d[0] = -1; d[1] = -v; d[2] = u; break;
                    case 2: d[0] = u; d[1] = 1; d[2] = v; break;
                    case 3: d[0] = u; d[1] = -1; d[2] = -v; break;
                    case 4: d[0] = u; d[1] = -v; d[2] = 1; break;
                    default: d[0] = -u; d[1] = -v; d[2] = -1; break;
                }
                const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                const float h = d[1] / len;
                const float sun = std::pow(std::fmax(0.0f, (d[0] * 0.5f + d[1] * 0.7f + d[2] * 0.5f) / len / 0.9f), 24.0f);
                const float sky = 0.5f + 0.5f * h;
                std::uint8_t* p = &px[(y * size + x) * 4];
                p[0] = static_cast<std::uint8_t>(std::fmin(255.0f, (0.25f + 0.35f * sky + sun * 2.0f) * 255));
                p[1] = static_cast<std::uint8_t>(std::fmin(255.0f, (0.3f + 0.45f * sky + sun * 1.8f) * 255));
                p[2] = static_cast<std::uint8_t>(std::fmin(255.0f, (0.35f + 0.6f * sky + sun * 1.4f) * 255));
                p[3] = 255;
            }
        faces.push_back(px);
    }
    const unsigned levels = rr::Pattern::level_count(size, size);
    std::vector<std::vector<Bytes>> chains;
    for (int f = 0; f < 6; ++f) chains.push_back(rr::Pattern::mip_chain(faces[f], size, size, levels));
    for (int f = 0; f < 6; ++f)
        for (unsigned l = 0; l < levels; ++l) data.push_back({chains[f][l].data(), std::max(1u, size >> l) * 4, 0});
    Com<ID3D11Texture2D> cube = r.tex(size, size, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, levels, 6, 1, data.data(), D3D11_RESOURCE_MISC_TEXTURECUBE);
    Bytes lut(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const float nov = (x + 0.5f) / 32.0f, rough = (y + 0.5f) / 32.0f;
            const float a = 1.0f - nov * (1.0f - 0.04f) * (1.0f - rough * 0.5f);
            lut[(y * 32 + x) * 4 + 0] = static_cast<std::uint8_t>(std::fmax(0.0f, std::fmin(1.0f, (1.0f - rough) * (1 - a * 0.5f))) * 255);
            lut[(y * 32 + x) * 4 + 1] = static_cast<std::uint8_t>(std::fmax(0.0f, std::fmin(1.0f, a * 0.15f + rough * 0.08f)) * 255);
            lut[(y * 32 + x) * 4 + 2] = 0;
            lut[(y * 32 + x) * 4 + 3] = 255;
        }
    Com<ID3D11Texture2D> lut_tex = r.tex_data(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, lut);
    D3D11_SHADER_RESOURCE_VIEW_DESC cd{};
    cd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    cd.TextureCube.MipLevels = levels;
    Com<ID3D11ShaderResourceView> cube_srv = r.srv(cube.get(), &cd);
    Com<ID3D11ShaderResourceView> lut_srv = r.srv(lut_tex.get());
    const char* ps_source =
        "TextureCube env : register(t0);\nTexture2D lut : register(t1);\nSamplerState s : register(s0);\n"
        "float4 main(VO i) : SV_Target { float3 n = normalize(i.n); float3 v = normalize(float3(0, 3, 9) - i.w); float rough = extra.x; float metal = extra.y; float nov = saturate(dot(n, v));\n"
        "  float3 r = reflect(-v, n); float3 spec_env = env.SampleLevel(s, r, rough * 4.0).rgb; float3 diff_env = env.SampleLevel(s, n, 5.0).rgb; float2 brdf = lut.Sample(s, float2(nov, rough)).rg;\n"
        "  float3 f0 = lerp(0.04, color.rgb, metal); float3 albedo = color.rgb * (1 - metal); float3 color_out = albedo * diff_env + spec_env * (f0 * brdf.x + brdf.y); return float4(pow(color_out, 1.0 / 2.2), 1); }\n";
    Com<ID3D11PixelShader> ps = r.ps(std::string(Hlsl3::kLitIn) + ps_source);
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!cube || !lut_tex || !cube_srv || !lut_srv || !ps || !smp || !ds) return false;
    const float bg[4] = {0.1f, 0.12f, 0.16f, 1};
    r.bind(target, &depth, bg);
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    r.ctx->RSSetState(r.rast(rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK)).get());
    pipe.bind(r);
    sphere.bind(r);
    ID3D11ShaderResourceView* views[2] = {cube_srv.get(), lut_srv.get()};
    ID3D11SamplerState* s = smp.get();
    r.ctx->PSSetShaderResources(0, 2, views);
    r.ctx->PSSetSamplers(0, 1, &s);
    r.ctx->PSSetShader(ps.get(), nullptr, 0);
    const rr::Mat4 vp = rr::Mat4::perspective_d3d(0.9f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -9.0f);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x) {
            pipe.set(r, vp, trs(-2.9f + 1.95f * x, 1.9f - 1.9f * y, 0, 0.85f, 0.85f, 0.85f), 0.95f, 0.65f + 0.1f * y, 0.35f, 0.08f + 0.3f * x, y == 0 ? 0.0f : (y == 1 ? 0.5f : 1.0f));
            sphere.draw(r);
        }
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
