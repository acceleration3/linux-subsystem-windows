#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

bool msaa_scene(Rig& r, Cap& out, UINT samples, DXGI_FORMAT format, bool standard_pattern) {
    Target target;
    Depth depth;
    CubePipe cubes;
    Basic basic;
    if (standard_pattern) {
        D3D11_TEXTURE2D_DESC d = D3d11Tools::desc2d(128, 128, format, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, samples);
        d.SampleDesc.Quality = D3D11_STANDARD_MULTISAMPLE_PATTERN;
        d.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture2D> t;
        if (!r.ok(r.dev->CreateTexture2D(&d, nullptr, t.put()), "multisample texture with the standard pattern")) return false;
        target.texture = std::move(t);
        target.view = r.rtv(target.texture.get());
        d.SampleDesc.Count = 1;
        d.SampleDesc.Quality = 0;
        d.BindFlags = 0;
        if (!target.view || !r.ok(r.dev->CreateTexture2D(&d, nullptr, target.resolved.put()), "resolve target")) return false;
        target.format = format;
        target.width = target.height = 128;
        target.samples = samples;
    } else if (!r.color(target, 128, 128, format, samples)) {
        return false;
    }
    if (!r.depth(depth, 128, 128, DXGI_FORMAT_D32_FLOAT, samples, standard_pattern ? D3D11_STANDARD_MULTISAMPLE_PATTERN : 0) || !cubes.make(r)) return false;
    D3D11_RASTERIZER_DESC rd = rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK);
    rd.FrontCounterClockwise = TRUE;
    rd.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!rs || !ds) return false;
    r.bind(target, &depth);
    cubes.bind(r);
    r.ctx->RSSetState(rs.get());
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(-0.7f, 0.1f, 0) * rr::Mat4::rotate_y(0.5f) * rr::Mat4::rotate_x(0.45f));
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(0.8f, -0.2f, 0.7f) * rr::Mat4::rotate_y(-0.8f) * rr::Mat4::rotate_z(0.6f) * rr::Mat4::scale(0.8f, 0.8f, 0.8f));
    return r.shot(out, target);
}

struct Registrar {
    Registrar() {
        for (UINT samples : {2u, 4u, 8u, 16u}) {
            Add(spec("msaa", ("color_depth_resolve_" + std::to_string(samples) + "x").c_str()).tolerance(rr::Tolerance::tight(8)),
                [samples](Rig& r, Cap& out) { return msaa_scene(r, out, samples, DXGI_FORMAT_R8G8B8A8_UNORM, false); },
                [samples](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, samples); });
        }
        Add(spec("msaa", "float16_color_resolve_4x").tolerance(rr::Tolerance::tight(8)),
            [](Rig& r, Cap& out) { return msaa_scene(r, out, 4, DXGI_FORMAT_R16G16B16A16_FLOAT, false); }, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R16G16B16A16_FLOAT, 4); });
    }
};
const Registrar kRegistrar;

const Add kDepthShader(spec("msaa", "depth_texture2dms_shader_resolve_4x").tolerance(rr::Tolerance::tight(8)), [](Rig& r, Cap& out) {
    Target target;
    Depth depth;
    CubePipe cubes;
    if (!r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 4) || !r.depth(depth, 128, 128, DXGI_FORMAT_D32_FLOAT, 4) || !cubes.make(r)) return false;
    D3D11_RASTERIZER_DESC rd = rdesc(D3D11_FILL_SOLID, D3D11_CULL_BACK);
    rd.FrontCounterClockwise = TRUE;
    rd.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    if (!rs || !ds) return false;
    r.bind(target, &depth);
    cubes.bind(r);
    r.ctx->RSSetState(rs.get());
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(-0.4f, 0.1f, 0) * rr::Mat4::rotate_y(0.5f) * rr::Mat4::rotate_x(0.45f));
    cubes.draw(r, CubePipe::camera() * rr::Mat4::translate(0.9f, -0.2f, 0.7f) * rr::Mat4::rotate_y(-0.8f) * rr::Mat4::rotate_z(0.6f));
    Target display;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    Com<ID3D11ShaderResourceView> srv = r.srv(depth.texture.get(), &sd);
    Com<ID3D11PixelShader> ps = r.ps(
        "Texture2DMS<float, 4> t : register(t0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float4 main(VO i) : SV_Target { int2 c = int2(i.p.xy); float s = 0; float mn = 1; [unroll] for (int k = 0; k < 4; ++k) { float d = t.Load(c, k); s += d; mn = min(mn, d); } s *= 0.25; return float4(s, mn, abs(s - mn) * 20.0, 1); }\n");
    if (!srv || !ps || !r.color(display)) return false;
    r.unbind_srvs();
    r.bind(display);
    ID3D11ShaderResourceView* v = srv.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.draw_fs(ps.get());
    return r.shot(out, display);
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R32_FLOAT, 4); });

const Add kMrt(spec("msaa", "two_render_targets_resolved_4x").tolerance(rr::Tolerance::tight(8)), [](Rig& r, Cap& out) {
    Target a, b;
    Basic basic;
    const char* ps_source =
        "struct VO { float4 p : SV_Position; float3 c : COLOR; };\nstruct PO { float4 a : SV_Target0; float4 b : SV_Target1; };\n"
        "PO main(VO i) { PO o; o.a = float4(i.c, 1); o.b = float4(i.c.bgr * 0.5, 1); return o; }\n";
    if (!r.color(a, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 4) || !r.color(b, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 4) || !basic.make(r, ps_source)) return false;
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(5, 0.85f));
    if (!rs || !vb) return false;
    ID3D11RenderTargetView* views[2] = {a.view.get(), b.view.get()};
    const float clear[4] = {0.1f, 0.1f, 0.12f, 1};
    r.ctx->OMSetRenderTargets(2, views, nullptr);
    r.ctx->ClearRenderTargetView(views[0], clear);
    r.ctx->ClearRenderTargetView(views[1], clear);
    r.viewport(0, 0, 128, 128);
    r.ctx->RSSetState(rs.get());
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(15, 0);
    return r.shot(out, a, "rt0") && r.shot(out, b, "rt1");
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

const Add kArraySlice(spec("msaa", "array_slice_resolve_subresource_index_4x").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> ms = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, 1, 3, 4);
    Com<ID3D11Texture2D> single = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 1, 3, 1);
    Basic basic;
    if (!ms || !single || !basic.make(r)) return false;
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    if (!rs) return false;
    for (UINT slice = 0; slice < 3; ++slice) {
        D3D11_RENDER_TARGET_VIEW_DESC d{};
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
        d.Texture2DMSArray.FirstArraySlice = slice;
        d.Texture2DMSArray.ArraySize = 1;
        Com<ID3D11RenderTargetView> rtv = r.rtv(ms.get(), &d);
        Com<ID3D11Buffer> vb = r.vbuf(ring_fan(3 + slice, 0.8f, 0, 0, slice * 0.4f));
        if (!rtv || !vb) return false;
        ID3D11RenderTargetView* v = rtv.get();
        const float clear[4] = {0.1f * slice, 0.1f, 0.2f, 1};
        r.ctx->OMSetRenderTargets(1, &v, nullptr);
        r.ctx->ClearRenderTargetView(v, clear);
        r.viewport(0, 0, 64, 64);
        r.ctx->RSSetState(rs.get());
        basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        basic.vertices(r, vb.get());
        r.ctx->Draw((3 + slice) * 3, 0);
    }
    for (UINT slice : {2u, 0u, 1u}) {
        r.ctx->ResolveSubresource(single.get(), slice, ms.get(), slice, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    Target target;
    Com<ID3D11PixelShader> ps = r.ps("Texture2DArray t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return t.Sample(s, float3(frac(i.uv * float2(3, 1)), floor(i.uv.x * 3))); }\n");
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11ShaderResourceView> srv = r.srv(single.get());
    if (!ps || !smp || !srv || !r.color(target)) return false;
    r.bind(target);
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* s = smp.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &s);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

const Add kFoliage(spec("msaa", "alpha_to_coverage_foliage_4x").tolerance(rr::Tolerance::psnr(22)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    const char* ps_source =
        "struct VO { float4 p : SV_Position; float3 c : COLOR; };\nfloat4 main(VO i) : SV_Target { float a = saturate(1.0 - 2.2 * length(frac(i.p.xy * 0.045 + i.c.xy) - 0.5)); return float4(i.c, a); }\n";
    if (!r.color(target, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, 4) || !basic.make(r, ps_source)) return false;
    D3D11_BLEND_DESC bd = bdesc();
    bd.AlphaToCoverageEnable = TRUE;
    Com<ID3D11BlendState> blend = r.blend(bd);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    std::vector<Vtx> verts;
    std::uint32_t state = 4;
    for (int i = 0; i < 24; ++i) {
        const float cx = rr::Pattern::unit_random(state) * 1.6f - 0.8f;
        const float cy = rr::Pattern::unit_random(state) * 1.6f - 0.8f;
        const float rot = rr::Pattern::unit_random(state) * 6.28f;
        const auto fan = ring_fan(3, 0.28f, cx, cy, rot);
        for (Vtx v : fan) {
            v.r = 0.2f + 0.5f * rr::Pattern::unit_random(state) * 0 + 0.03f * i;
            v.g = 0.6f - 0.02f * i;
            v.b = 0.2f;
            verts.push_back(v);
        }
    }
    Com<ID3D11Buffer> vb = r.vbuf(verts);
    if (!blend || !rs || !vb) return false;
    const float clear[4] = {0.5f, 0.7f, 0.9f, 1};
    r.bind(target, nullptr, clear);
    r.ctx->RSSetState(rs.get());
    const float factor[4] = {};
    r.ctx->OMSetBlendState(blend.get(), factor, 0xffffffffu);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(static_cast<UINT>(verts.size()), 0);
    return r.shot(out, target);
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

}  // namespace

}  // namespace lsw::suites::render::d3d11
