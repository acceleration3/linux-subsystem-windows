#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

Com<ID3D10Texture2D> coded_levels(Rig& r, UINT size, UINT slices, bool rt_flags, UINT misc = 0) {
    const UINT levels = rr::Pattern::level_count(size, size);
    std::vector<rr::Pattern::Bytes> store;
    std::vector<const void*> data;
    std::vector<UINT> pitch;
    for (UINT s = 0; s < slices; ++s)
        for (UINT l = 0; l < levels; ++l) {
            const UINT w = std::max(1u, size >> l);
            const float* c = palette(l + s);
            store.push_back(rr::Pattern::checker(w, w, std::max(1u, w / 4), rr::Pattern::pack(static_cast<unsigned>(c[0] * 255), static_cast<unsigned>(c[1] * 255), static_cast<unsigned>(c[2] * 255)),
                                                 rr::Pattern::pack(static_cast<unsigned>(c[0] * 120), static_cast<unsigned>(c[1] * 120), static_cast<unsigned>(c[2] * 120))));
        }
    for (UINT i = 0; i < store.size(); ++i) {
        data.push_back(store[i].data());
        pitch.push_back(std::max(1u, size >> (i % levels)) * 4);
    }
    return r.tex2d(size, size, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE | (rt_flags ? D3D10_BIND_RENDER_TARGET : 0), levels, slices, data, pitch, misc);
}

const char* kQuadrantPs =
    "Texture2D t : register(t0); SamplerState s : register(s0);\n"
    "float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { float2 q = frac(uv * 2); int cell = int(uv.x * 2) + 2 * int(uv.y * 2); return t.SampleLevel(s, q, LEVELS); }\n";

std::string quadrant_ps(const char* levels) {
    std::string s = kQuadrantPs;
    s.replace(s.find("LEVELS"), 6, levels);
    return s;
}

const Registrar kViews{[] {
    single(spec("tex", "generate_mips_from_rendered_level").tolerance(rr::Tolerance::filter(12, 0.02)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        Com<ID3D10Texture2D> tex = r.tex2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, 7, 1, {}, {}, D3D10_RESOURCE_MISC_GENERATE_MIPS);
        if (!r.ok()) return;
        Com<ID3D10RenderTargetView> rtv = r.rtv(tex.get());
        Com<ID3D10ShaderResourceView> srv = r.srv(tex.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        Com<ID3D10PixelShader> pattern = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { float2 g = floor(uv * 8); float chk = fmod(g.x + g.y, 2); return float4(chk, uv.x * (1 - chk) + 0.1, uv.y, 1); }\n");
        if (!r.ok()) return;
        r.use_std();
        ID3D10RenderTargetView* rt = rtv.get();
        r.d->OMSetRenderTargets(1, &rt, nullptr);
        r.viewport(64, 64);
        r.d->PSSetShader(pattern.get());
        r.draw_std(Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1));
        if (!r.mutate("tex_genmips")) r.d->GenerateMips(srv.get());
        r.bind_rt(r.main);
        r.viewport(128, 128);
        paint(r, o, quadrant_ps("cell == 0 ? 0 : cell == 1 ? 1 : cell == 2 ? 2 : 3"), {srv.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "srv_most_detailed_mip_and_mip_count").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture2D> tex = coded_levels(r, 64, 1, false);
        D3D10_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        vd.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
        vd.Texture2D.MostDetailedMip = r.mutate("tex_srvmip") ? 0u : 2u;
        vd.Texture2D.MipLevels = 2;
        Com<ID3D10ShaderResourceView> srv = r.srv(tex.get(), &vd);
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT, D3D10_TEXTURE_ADDRESS_WRAP));
        if (!r.ok()) return;
        paint(r, o, quadrant_ps("cell == 0 ? 0 : cell == 1 ? 1 : cell == 2 ? 6 : -3"), {srv.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "srv_array_first_slice_and_count").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture2D> tex = coded_levels(r, 16, 6, false);
        D3D10_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        vd.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2DARRAY;
        vd.Texture2DArray.MipLevels = 1;
        vd.Texture2DArray.FirstArraySlice = r.mutate("tex_srvarray") ? 0u : 2u;
        vd.Texture2DArray.ArraySize = 3;
        Com<ID3D10ShaderResourceView> srv = r.srv(tex.get(), &vd);
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT, D3D10_TEXTURE_ADDRESS_WRAP));
        if (!r.ok()) return;
        paint(r, o, "Texture2DArray t : register(t0); SamplerState s : register(s0);\n" PS_HEAD
                    "  float2 q = frac(uv * 2); int cell = int(uv.x * 2) + 2 * int(uv.y * 2); float k = cell == 0 ? 0 : cell == 1 ? 1 : cell == 2 ? 2 : 5; return t.SampleLevel(s, float3(q * 2, k), 0); }\n",
              {srv.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "render_into_each_mip_level").tolerance(rr::Tolerance::filter(8, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        Com<ID3D10Texture2D> tex = r.tex2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, 4);
        Com<ID3D10PixelShader> disc = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { float2 d = uv - 0.5; return dot(d, d) < 0.12 ? c : float4(c.rgb * 0.3, 1); }\n");
        Com<ID3D10ShaderResourceView> srv = r.srv(tex.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        if (!r.ok()) return;
        r.use_std();
        r.d->PSSetShader(disc.get());
        for (UINT m = 0; m < 4; ++m) {
            D3D10_RENDER_TARGET_VIEW_DESC rd{};
            rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rd.ViewDimension = D3D10_RTV_DIMENSION_TEXTURE2D;
            rd.Texture2D.MipSlice = (r.mutate("tex_rtvmip") && m == 1) ? 0u : m;
            Com<ID3D10RenderTargetView> rtv = r.rtv(tex.get(), &rd);
            if (!r.ok()) return;
            ID3D10RenderTargetView* rt = rtv.get();
            r.d->OMSetRenderTargets(1, &rt, nullptr);
            r.viewport(64u >> m, 64u >> m);
            const float* c = palette(m + 1);
            r.d->ClearRenderTargetView(rt, palette(m + 4));
            r.draw_std(Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1));
            std::vector<Vtx> v = Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1);
            for (auto& x : v) { x.r = c[0]; x.g = c[1]; x.b = c[2]; x.a = 1; }
            r.draw_std(v);
        }
        r.bind_rt(r.main);
        r.viewport(128, 128);
        paint(r, o, quadrant_ps("cell"), {srv.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "render_into_3d_texture_w_slices").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        D3D10_TEXTURE3D_DESC d{};
        d.Width = d.Height = d.Depth = 16;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D10_USAGE_DEFAULT;
        d.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
        Com<ID3D10Texture3D> tex;
        r.check(r.d->CreateTexture3D(&d, nullptr, tex.put()), "CreateTexture3D");
        if (!r.ok()) return;
        D3D10_RENDER_TARGET_VIEW_DESC all{};
        all.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        all.ViewDimension = D3D10_RTV_DIMENSION_TEXTURE3D;
        all.Texture3D.WSize = 16;
        Com<ID3D10RenderTargetView> whole = r.rtv(tex.get(), &all);
        Com<ID3D10ShaderResourceView> srv = r.srv(tex.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        Com<ID3D10PixelShader> grad = r.ps("float4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { return float4(c.rgb * (0.4 + 0.6 * uv.x), 1) * (0.5 + 0.5 * step(0.5, frac(uv.y * 4))); }\n");
        if (!r.ok()) return;
        const float zero[4] = {0, 0, 0, 1};
        r.d->ClearRenderTargetView(whole.get(), zero);
        r.use_std();
        r.d->PSSetShader(grad.get());
        const UINT slices[4] = {0, 3, 7, 12};
        for (int i = 0; i < 4; ++i) {
            D3D10_RENDER_TARGET_VIEW_DESC rd = all;
            rd.Texture3D.FirstWSlice = (r.mutate("tex_rtv3d") && i == 2) ? 0u : slices[i];
            rd.Texture3D.WSize = 1;
            Com<ID3D10RenderTargetView> rtv = r.rtv(tex.get(), &rd);
            if (!r.ok()) return;
            ID3D10RenderTargetView* rt = rtv.get();
            r.d->OMSetRenderTargets(1, &rt, nullptr);
            r.viewport(16, 16);
            std::vector<Vtx> v = Geo::quad_uv(-1, -1, 1, 1, 0.5f, 0, 0, 1, 1);
            for (auto& x : v) { x.r = palette(i)[0]; x.g = palette(i)[1]; x.b = palette(i)[2]; x.a = 1; }
            r.draw_std(v);
        }
        r.bind_rt(r.main);
        r.viewport(128, 128);
        paint(r, o, "Texture3D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD
                    "  int cell = int(uv.x * 2) + 2 * int(uv.y * 2); float2 q = frac(uv * 2); float w = cell == 0 ? 0 : cell == 1 ? 3 : cell == 2 ? 7 : 12; return t.SampleLevel(s, float3(q, (w + 0.5) / 16), 0) + 0.0 * t.SampleLevel(s, float3(q, 0.99), 0); }\n",
              {srv.get()}, {smp.get()}, full_uv());
    });
    struct DepthView {
        const char* name;
        DXGI_FORMAT typeless, dsv, srv;
        float tol;
    };
    const DepthView views[] = {{"d32_float_as_r32_float", DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 6e-7f},
                               {"d24_unorm_as_r24_x8", DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, 6e-7f},
                               {"d16_unorm_as_r16_unorm", DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, 5e-5f}};
    for (const DepthView& v : views) {
        const DepthView copy = v;
        single(spec("tex", std::string("sample_depth_") + v.name).tolerance(rr::Tolerance::floats(v.tol, 0.0f).with_outliers(2e-4)), [copy](Rig& r, rr::CaptureSet& o) {
            r.begin(128, 128, DXGI_FORMAT_R32G32B32A32_FLOAT);
            Com<ID3D10Texture2D> tex = r.tex2d(128, 128, copy.typeless, D3D10_BIND_DEPTH_STENCIL | D3D10_BIND_SHADER_RESOURCE);
            D3D10_DEPTH_STENCIL_VIEW_DESC dd{};
            dd.Format = copy.dsv;
            dd.ViewDimension = D3D10_DSV_DIMENSION_TEXTURE2D;
            Com<ID3D10DepthStencilView> dsv = r.dsv(tex.get(), &dd);
            D3D10_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = copy.srv;
            sd.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;
            Com<ID3D10ShaderResourceView> srv = r.srv(tex.get(), &sd);
            ScenePipe pipe(r, "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1) : SV_Target { return float4(1, 1, 1, 1); }\n");
            MeshBuffers sphere = upload(r, Geo::sphere(16, 24, 1.0f));
            MeshBuffers ring = upload(r, Geo::torus(24, 12, 1.2f, 0.35f));
            auto dd2 = Rig::depth_desc();
            dd2.DepthEnable = TRUE;
            Com<ID3D10DepthStencilState> ds = r.depth(dd2);
            Com<ID3D10PixelShader> show = r.ps("Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position, float4 c : COLOR, float2 uv : TEXCOORD0) : SV_Target { float d = t.Load(int3(p.xy, 0)).r; return float4(d, d * d, (d - 0.5) * 2, 1); }\n");
            if (!r.ok()) return;
            r.no_cull();
            r.d->OMSetRenderTargets(0, nullptr, dsv.get());
            r.d->ClearDepthStencilView(dsv.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
            r.viewport(128, 128);
            r.d->OMSetDepthStencilState(ds.get(), 0);
            pipe.bind(r);
            r.d->PSSetShader(nullptr);
            pipe.draw(r, sphere, camera(0.4f, 0.3f, 4.0f) * rr::Mat4::translate(-0.3f, 0, 0));
            pipe.draw(r, ring, camera(0.4f, 0.3f, 4.0f) * rr::Mat4::translate(0.3f, 0.1f, 0.2f) * rr::Mat4::rotate_x(1.1f));
            r.bind_rt(r.main);
            r.d->OMSetDepthStencilState(nullptr, 0);
            r.ps_srv(0, srv.get());
            r.use_std();
            r.d->PSSetShader(show.get());
            r.draw_std(full_uv());
            r.cap_main(o);
        });
    }
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
