#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

constexpr UINT kStandardPattern = 0xffffffffu;

std::string needs_samples(Rig& r, DXGI_FORMAT f, UINT n) {
    return r.msaa_quality(f, n) != 0 ? "" : std::to_string(n) + "x multisampling of the format";
}

void msaa_target(Rig& r, UINT samples, DXGI_FORMAT format, bool with_depth, bool standard_pattern = false) {
    if (standard_pattern) {
        D3D10_TEXTURE2D_DESC d = D3d10Tools::desc2d(128, 128, format, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DEFAULT, 0, 1, 1, samples);
        d.SampleDesc.Quality = kStandardPattern;
        r.check(r.d->CreateTexture2D(&d, nullptr, r.main.texture.put()), "standard pattern target");
        if (!r.ok()) return;
        r.main.view = r.rtv(r.main.texture.get());
        d.SampleDesc.Count = 1;
        d.SampleDesc.Quality = 0;
        r.check(r.d->CreateTexture2D(&d, nullptr, r.main.resolved.put()), "resolve target");
        r.main.format = format;
        r.main.width = r.main.height = 128;
        r.main.samples = samples;
        if (with_depth) {
            D3D10_TEXTURE2D_DESC dd = D3d10Tools::desc2d(128, 128, DXGI_FORMAT_D32_FLOAT, D3D10_BIND_DEPTH_STENCIL, D3D10_USAGE_DEFAULT, 0, 1, 1, samples);
            dd.SampleDesc.Quality = kStandardPattern;
            r.check(r.d->CreateTexture2D(&dd, nullptr, r.depth_main.texture.put()), "standard pattern depth");
            if (r.ok()) r.depth_main.view = r.dsv(r.depth_main.texture.get());
            r.depth_main.format = DXGI_FORMAT_D32_FLOAT;
        }
    } else {
        r.target(r.main, 128, 128, format, samples);
        if (with_depth) r.depth_target(r.depth_main, 128, 128, DXGI_FORMAT_D32_FLOAT, samples);
    }
    if (!r.ok()) return;
    r.bind_rt(r.main, with_depth ? &r.depth_main : nullptr);
    r.clear(r.main, 0.1f, 0.1f, 0.15f, 1);
    if (with_depth) r.clear_depth(r.depth_main);
    r.viewport(128, 128);
    r.use_std();
    r.no_cull();
}

void draw_aa_scene(Rig& r) {
    r.draw_std(Geo::triangle(-0.35f, 0.15f, 0.62f, 0.5f, 0.9f, 0.3f, 0.2f));
    r.draw_std(Geo::triangle(0.35f, -0.05f, 0.6f, 0.4f, 0.2f, 0.6f, 0.95f));
    std::vector<Vtx> fan;
    for (int i = 0; i < 18; ++i) {
        const float a0 = 6.2831853f * i / 18.0f, a1 = 6.2831853f * (i + 1) / 18.0f;
        fan.push_back(vtx(0.0f, -0.45f, 0.3f, palette(i)));
        fan.push_back(vtx(0.32f * std::cos(a1), -0.45f + 0.32f * std::sin(a1), 0.3f, palette(i + 1)));
        fan.push_back(vtx(0.32f * std::cos(a0), -0.45f + 0.32f * std::sin(a0), 0.3f, palette(i)));
    }
    r.draw_std(fan);
}

const Registrar kMsaa{[] {
    for (UINT n : {2u, 4u, 8u}) {
        single(spec("msaa", "color_resolve_" + std::to_string(n) + "x").tolerance(rr::Tolerance::raster(40, 1)),
               [n](Rig& r, rr::CaptureSet& o) {
                   msaa_target(r, n, DXGI_FORMAT_R8G8B8A8_UNORM, false);
                   if (!r.ok()) return;
                   draw_aa_scene(r);
                   r.cap_main(o);
               },
               [n](Rig& r) { return needs_samples(r, DXGI_FORMAT_R8G8B8A8_UNORM, n); });
    }
    single(spec("msaa", "float_target_resolve_4x").tolerance(rr::Tolerance::floats(0.15f, 0.05f).with_edge(1)), [](Rig& r, rr::CaptureSet& o) {
        msaa_target(r, 4, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        if (!r.ok()) return;
        draw_aa_scene(r);
        r.cap_main(o);
    }, [](Rig& r) { return needs_samples(r, DXGI_FORMAT_R16G16B16A16_FLOAT, 4); });
    single(spec("msaa", "depth_test_intersecting_4x").tolerance(rr::Tolerance::raster(40, 1)), [](Rig& r, rr::CaptureSet& o) {
        msaa_target(r, 4, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        if (!r.ok()) return;
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        if (!r.ok()) return;
        r.d->OMSetDepthStencilState(ds.get(), 0);
        std::vector<Vtx> a = Geo::quad(-0.8f, -0.7f, 0.8f, 0.7f, 0.5f, 0.9f, 0.3f, 0.2f);
        std::vector<Vtx> b = Geo::quad(-0.7f, -0.8f, 0.7f, 0.8f, 0.5f, 0.2f, 0.5f, 0.95f);
        for (auto& v : a) v.z = 0.3f + 0.3f * (v.x + 0.8f);
        for (auto& v : b) v.z = 0.7f - 0.3f * (v.x + 0.7f);
        r.draw_std(a);
        r.draw_std(b);
        r.cap_main(o);
    }, [](Rig& r) { return needs_samples(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
    for (UINT n : {4u, 8u}) {
        single(spec("msaa", "standard_pattern_" + std::to_string(n) + "x", 1).tolerance(rr::Tolerance::blend(3)), [n](Rig& r, rr::CaptureSet& o) {
            msaa_target(r, n, DXGI_FORMAT_R8G8B8A8_UNORM, false, true);
            if (!r.ok()) return;
            draw_aa_scene(r);
            r.cap_main(o);
        });
    }
    single(spec("msaa", "centroid_vs_linear_edges").tolerance(rr::Tolerance::raster(40, 1)), [](Rig& r, rr::CaptureSet& o) {
        msaa_target(r, 4, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        if (!r.ok()) return;
        Com<ID3D10PixelShader> centroid = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, centroid float2 uv : TEXCOORD0) : SV_Target { return float4(uv.x, uv.y, 0.4, 1); }\n");
        Com<ID3D10PixelShader> plain = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return float4(uv.x, uv.y, 0.4, 1); }\n");
        if (!r.ok()) return;
        std::vector<Vtx> left = Geo::triangle(-0.45f, 0.0f, 0.5f, 0.5f, 1, 1, 1);
        left[0].u = 0; left[0].v = 0; left[1].u = 1; left[1].v = 0; left[2].u = 0; left[2].v = 1;
        std::vector<Vtx> right = left;
        for (auto& v : right) v.x += 0.9f;
        r.d->PSSetShader(plain.get());
        r.draw_std(left);
        r.d->PSSetShader(centroid.get());
        r.draw_std(right);
        r.cap_main(o);
    }, [](Rig& r) { return needs_samples(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
    single(spec("msaa", "per_sample_shading_sample_index", 1).tolerance(rr::Tolerance::raster(40, 1)), [](Rig& r, rr::CaptureSet& o) {
        msaa_target(r, 4, DXGI_FORMAT_R8G8B8A8_UNORM, false, true);
        if (!r.ok()) return;
        Com<ID3D10PixelShader> ps = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0, uint si : SV_SampleIndex) : SV_Target { return float4(si * 0.3 + 0.1, uv.x, 1 - si * 0.2, 1); }\n");
        if (!r.ok()) return;
        r.d->PSSetShader(ps.get());
        r.draw_std(Geo::quad_uv(-0.8f, -0.8f, 0.8f, 0.8f, 0.5f, 0, 0, 1, 1));
        r.cap_main(o);
    });
    single(spec("msaa", "alpha_to_coverage_4x").tolerance(rr::Tolerance::psnr(24.0)), [](Rig& r, rr::CaptureSet& o) {
        msaa_target(r, 4, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        if (!r.ok()) return;
        auto bd = Rig::blend_desc();
        bd.AlphaToCoverageEnable = TRUE;
        Com<ID3D10BlendState> bs = r.blend(bd);
        Com<ID3D10PixelShader> ps = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return float4(0.9, 0.6 * uv.y + 0.2, 0.2, uv.x); }\n");
        if (!r.ok()) return;
        const float f[4] = {1, 1, 1, 1};
        r.d->OMSetBlendState(bs.get(), f, 0xffffffffu);
        r.d->PSSetShader(ps.get());
        r.draw_std(Geo::quad_uv(-0.9f, -0.9f, 0.9f, 0.9f, 0.5f, 0, 0, 1, 1));
        r.cap_main(o);
    }, [](Rig& r) { return needs_samples(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
    single(spec("msaa", "sample_mask_halves").tolerance(rr::Tolerance::blend(3)), [](Rig& r, rr::CaptureSet& o) {
        msaa_target(r, 4, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        if (!r.ok()) return;
        Com<ID3D10BlendState> bs = r.blend(Rig::blend_desc());
        if (!r.ok()) return;
        const float f[4] = {1, 1, 1, 1};
        const UINT masks[4] = {0x3, 0x5, 0x1, 0xf};
        for (int i = 0; i < 4; ++i) {
            r.d->OMSetBlendState(bs.get(), f, masks[i]);
            r.draw_std(Geo::quad(-0.9f + 0.45f * i, -0.8f, -0.5f + 0.45f * i, 0.8f, 0.5f, palette(i)[0], palette(i)[1], palette(i)[2]));
        }
        r.cap_main(o);
    }, [](Rig& r) { return needs_samples(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
    single(spec("msaa", "load_individual_samples", 1).tolerance(rr::Tolerance::blend(3)), [](Rig& r, rr::CaptureSet& o) {
        msaa_target(r, 4, DXGI_FORMAT_R8G8B8A8_UNORM, false, true);
        if (!r.ok()) return;
        draw_aa_scene(r);
        Target out;
        r.target(out, 128, 128);
        Com<ID3D10ShaderResourceView> view = r.srv(r.main.texture.get());
        Com<ID3D10PixelShader> ps = r.ps("Texture2DMS<float4, 4> t : register(t0);\n" PS_HEAD "  int2 q = int2(p.xy); int s = (q.x >= 64 ? 1 : 0) + (q.y >= 64 ? 2 : 0); return t.Load(q, s); }\n");
        if (!r.ok()) return;
        r.bind_rt(out);
        r.d->PSSetShader(ps.get());
        r.ps_srv(0, view.get());
        r.draw_std(full_uv());
        r.cap(o, "", out);
    });
    single(spec("msaa", "resolve_subresource_srgb_target").tolerance(rr::Tolerance::raster(40, 1)), [](Rig& r, rr::CaptureSet& o) {
        msaa_target(r, 4, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false);
        if (!r.ok()) return;
        draw_aa_scene(r);
        r.cap_main(o);
    }, [](Rig& r) { return needs_samples(r, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4); });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
