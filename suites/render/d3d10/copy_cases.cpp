#include "render/d3d10/util.hpp"

#include <cstring>

namespace lsw::suites::render::d3d10 {
namespace {

const char* kShowPs = "Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  return t.SampleLevel(s, uv, LEVEL); }\n";

std::string show_level(int level) {
    std::string s = kShowPs;
    s.replace(s.find("LEVEL"), 5, std::to_string(level));
    return s;
}

void show(Rig& r, rr::CaptureSet& o, ID3D10Texture2D* tex, int level = 0, const D3D10_SHADER_RESOURCE_VIEW_DESC* desc = nullptr, D3D10_FILTER filter = D3D10_FILTER_MIN_MAG_MIP_POINT) {
    Com<ID3D10ShaderResourceView> view = r.srv(tex, desc);
    Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(filter));
    if (!r.ok()) return;
    paint(r, o, show_level(level), {view.get()}, {smp.get()}, full_uv());
}

const Registrar kCopy{[] {
    const rr::Tolerance tol = rr::Tolerance::blend();
    single(spec("copy", "copy_resource_texture_with_mips").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture2D> a = r.rgba_tex(64, 64, tile_pattern(64, 64, 2), true);
        D3D10_TEXTURE2D_DESC desc;
        a->GetDesc(&desc);
        Com<ID3D10Texture2D> b = r.tex2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, desc.MipLevels);
        if (!r.ok()) return;
        r.d->CopyResource(b.get(), a.get());
        Com<ID3D10ShaderResourceView> view = r.srv(b.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        Com<ID3D10PixelShader> ps = r.ps("Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  return t.SampleLevel(s, frac(uv * float2(2, 1)) , floor(uv.x * 2) * 2 + floor(uv.y * 2)); }\n");
        if (!r.ok()) return;
        r.d->PSSetShader(ps.get());
        r.ps_srv(0, view.get());
        r.ps_smp(0, smp.get());
        r.draw_std(full_uv());
        r.cap_main(o);
    });
    single(spec("copy", "copy_resource_vertex_buffer").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::vector<Vtx> v = Geo::triangle(-0.4f, 0.1f, 0.6f, 0.5f, 1, 0.3f, 0.2f);
        std::vector<Vtx> w = Geo::quad(0.1f, -0.8f, 0.9f, 0.2f, 0.5f, 0.2f, 0.4f, 1);
        v.insert(v.end(), w.begin(), w.end());
        Com<ID3D10Buffer> src = r.vbuf(v);
        Com<ID3D10Buffer> dst = r.buf(static_cast<UINT>(v.size() * sizeof(Vtx)), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, nullptr);
        if (!r.ok()) return;
        r.d->CopyResource(dst.get(), src.get());
        r.std_vb(dst.get());
        r.d->Draw(static_cast<UINT>(v.size()), 0);
        r.cap_main(o);
    });
    single(spec("copy", "copy_region_2d_box").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture2D> a = r.rgba_tex(64, 64, tile_pattern(64, 64, 1));
        Com<ID3D10Texture2D> b = r.rgba_tex(64, 64, rr::Pattern::checker(64, 64, 8, rr::Pattern::pack(20, 20, 30), rr::Pattern::pack(200, 60, 60)));
        if (!r.ok()) return;
        D3D10_BOX box = {10, 20, 0, 50, 44, 1};
        r.d->CopySubresourceRegion(b.get(), 0, 17, 5, 0, a.get(), 0, &box);
        D3D10_BOX box2 = {0, 0, 0, 16, 16, 1};
        r.d->CopySubresourceRegion(b.get(), 0, 40, 44, 0, a.get(), 0, &box2);
        show(r, o, b.get());
    });
    single(spec("copy", "copy_region_bc1_blocks").tolerance(rr::Tolerance::blend(6)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::vector<std::uint8_t> a(16 * 16 * 8), b(16 * 16 * 8);
        std::uint32_t s = 7;
        for (auto& x : a) { s = s * 1664525u + 1013904223u; x = static_cast<std::uint8_t>(s >> 16); }
        for (auto& x : b) { s = s * 1664525u + 1013904223u; x = static_cast<std::uint8_t>(s >> 16); }
        Com<ID3D10Texture2D> ta = r.tex2d(64, 64, DXGI_FORMAT_BC1_UNORM, D3D10_BIND_SHADER_RESOURCE, 1, 1, {a.data()}, {16 * 8});
        Com<ID3D10Texture2D> tb = r.tex2d(64, 64, DXGI_FORMAT_BC1_UNORM, D3D10_BIND_SHADER_RESOURCE, 1, 1, {b.data()}, {16 * 8});
        if (!r.ok()) return;
        D3D10_BOX box = {8, 12, 0, 44, 40, 1};
        r.d->CopySubresourceRegion(tb.get(), 0, 20, 4, 0, ta.get(), 0, &box);
        show(r, o, tb.get());
    });
    single(spec("copy", "copy_region_3d_box").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        D3D10_TEXTURE3D_DESC d{};
        d.Width = d.Height = d.Depth = 16;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D10_USAGE_DEFAULT;
        d.BindFlags = D3D10_BIND_SHADER_RESOURCE;
        std::vector<std::uint8_t> va(16 * 16 * 16 * 4), vb(16 * 16 * 16 * 4);
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    std::uint8_t* pa = &va[((z * 16 + y) * 16 + x) * 4];
                    std::uint8_t* pb = &vb[((z * 16 + y) * 16 + x) * 4];
                    pa[0] = static_cast<std::uint8_t>(x * 16); pa[1] = static_cast<std::uint8_t>(y * 16); pa[2] = static_cast<std::uint8_t>(z * 16); pa[3] = 255;
                    pb[0] = 30; pb[1] = static_cast<std::uint8_t>(z * 15); pb[2] = static_cast<std::uint8_t>(200 - x * 5); pb[3] = 255;
                }
        D3D10_SUBRESOURCE_DATA ia{va.data(), 16 * 4, 16 * 16 * 4}, ib{vb.data(), 16 * 4, 16 * 16 * 4};
        Com<ID3D10Texture3D> ta, tb;
        r.check(r.d->CreateTexture3D(&d, &ia, ta.put()), "3d a");
        r.check(r.d->CreateTexture3D(&d, &ib, tb.put()), "3d b");
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        if (!r.ok()) return;
        D3D10_BOX box = {2, 3, 4, 12, 13, 10};
        r.d->CopySubresourceRegion(tb.get(), 0, 3, 1, 5, ta.get(), 0, &box);
        Com<ID3D10ShaderResourceView> view = r.srv(tb.get());
        if (!r.ok()) return;
        paint(r, o, "Texture3D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  float2 cell = floor(uv * float2(4, 2)); float2 f = frac(uv * float2(4, 2)); return t.SampleLevel(s, float3(f, (cell.y * 4 + cell.x + 0.5) / 8.0), 0); }\n",
              {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("copy", "copy_region_buffer_ranges").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::vector<Vtx> a = Geo::quad(-0.9f, -0.9f, 0.0f, 0.0f, 0.5f, 0.9f, 0.3f, 0.2f);
        std::vector<Vtx> b = Geo::quad(0.0f, 0.0f, 0.9f, 0.9f, 0.5f, 0.2f, 0.4f, 0.9f);
        std::vector<Vtx> c = Geo::quad(-0.5f, 0.1f, -0.1f, 0.8f, 0.5f, 0.9f, 0.9f, 0.2f);
        std::vector<Vtx> all = a;
        all.insert(all.end(), b.begin(), b.end());
        all.insert(all.end(), c.begin(), c.end());
        Com<ID3D10Buffer> src = r.vbuf(all);
        Com<ID3D10Buffer> dst = r.buf(static_cast<UINT>(all.size() * sizeof(Vtx)), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, nullptr);
        if (!r.ok()) return;
        const UINT quad_bytes = 6 * sizeof(Vtx);
        D3D10_BOX second = {quad_bytes * 2, 0, 0, quad_bytes * 3, 1, 1};
        D3D10_BOX first = {0, 0, 0, quad_bytes, 1, 1};
        r.d->CopySubresourceRegion(dst.get(), 0, quad_bytes, 0, 0, src.get(), 0, &second);
        r.d->CopySubresourceRegion(dst.get(), 0, quad_bytes * 2, 0, 0, src.get(), 0, &first);
        r.std_vb(dst.get());
        r.d->Draw(18, 0);
        r.cap_main(o);
    });
    single(spec("copy", "copy_between_typeless_family_srgb_unorm").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture2D> a = r.rgba_tex(32, 32, rr::Pattern::gradient(32, 32), false, true);
        Com<ID3D10Texture2D> b = r.rgba_tex(32, 32, rr::Pattern::gradient(32, 32), false, false);
        if (!r.ok()) return;
        r.d->CopyResource(b.get(), a.get());
        show(r, o, b.get());
    });
    single(spec("copy", "copy_array_slice_mip_subresource").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::vector<rr::Pattern::Bytes> store;
        std::vector<const void*> data;
        std::vector<UINT> pitch;
        for (int s = 0; s < 4; ++s) {
            auto chain = rr::Pattern::mip_chain(tile_pattern(32, 32, static_cast<unsigned>(s)), 32, 32, 6);
            for (int m = 0; m < 6; ++m) store.push_back(chain[m]);
        }
        for (int s = 0; s < 4; ++s)
            for (int m = 0; m < 6; ++m) {
                data.push_back(store[s * 6 + m].data());
                pitch.push_back(std::max(1, 32 >> m) * 4);
            }
        Com<ID3D10Texture2D> a = r.tex2d(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, 6, 4, data, pitch);
        Com<ID3D10Texture2D> b = r.tex2d(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, 6, 4, data, pitch);
        if (!r.ok()) return;
        r.d->CopySubresourceRegion(b.get(), D3D10CalcSubresource(1, 0, 6), 0, 0, 0, a.get(), D3D10CalcSubresource(1, 2, 6), nullptr);
        r.d->CopySubresourceRegion(b.get(), D3D10CalcSubresource(0, 3, 6), 0, 0, 0, a.get(), D3D10CalcSubresource(0, 1, 6), nullptr);
        D3D10_SHADER_RESOURCE_VIEW_DESC v0{};
        v0.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        v0.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2DARRAY;
        v0.Texture2DArray.MipLevels = 6;
        v0.Texture2DArray.ArraySize = 4;
        Com<ID3D10ShaderResourceView> view = r.srv(b.get(), &v0);
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        if (!r.ok()) return;
        paint(r, o, "Texture2DArray t : register(t0); SamplerState s : register(s0);\n" PS_HEAD
                    "  float2 cell = floor(uv * float2(2, 2)); float2 f = frac(uv * float2(2, 2)); return t.SampleLevel(s, float3(f, cell.y * 2 + cell.x), cell.y * 1.0); }\n",
              {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("copy", "update_subresource_padded_pitch_and_box").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture2D> t = r.rgba_tex(64, 64, rr::Pattern::checker(64, 64, 4, rr::Pattern::pack(30, 30, 50), rr::Pattern::pack(90, 90, 120)));
        if (!r.ok()) return;
        const UINT pitch = 40 * 4 + 24;
        std::vector<std::uint8_t> src(pitch * 24, 0);
        for (UINT y = 0; y < 24; ++y)
            for (UINT x = 0; x < 40; ++x) {
                src[y * pitch + x * 4] = static_cast<std::uint8_t>(x * 6);
                src[y * pitch + x * 4 + 1] = static_cast<std::uint8_t>(y * 10);
                src[y * pitch + x * 4 + 2] = 200;
                src[y * pitch + x * 4 + 3] = 255;
            }
        D3D10_BOX box = {12, 30, 0, 52, 54, 1};
        r.d->UpdateSubresource(t.get(), 0, &box, src.data(), pitch, 0);
        show(r, o, t.get());
    });
    single(spec("copy", "update_subresource_buffer_box").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::vector<Vtx> a = Geo::quad(-0.9f, -0.9f, 0.9f, 0.9f, 0.5f, 0.9f, 0.3f, 0.2f);
        Com<ID3D10Buffer> vb = r.vbuf(a);
        std::vector<Vtx> patch = Geo::quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.2f, 0.4f, 0.95f);
        if (!r.ok()) return;
        D3D10_BOX box = {static_cast<UINT>(0), 0, 0, static_cast<UINT>(6 * sizeof(Vtx)), 1, 1};
        std::vector<Vtx> merged = a;
        for (int i = 0; i < 6; ++i) merged[i].x = a[i].x * 0.4f - 0.3f;
        r.d->UpdateSubresource(vb.get(), 0, &box, merged.data(), 0, 0);
        r.std_vb(vb.get());
        r.d->Draw(6, 0);
        std::vector<Vtx> second_half(patch.begin(), patch.begin() + 3);
        D3D10_BOX box2 = {static_cast<UINT>(3 * sizeof(Vtx)), 0, 0, static_cast<UINT>(6 * sizeof(Vtx)), 1, 1};
        std::vector<Vtx> tail(patch.begin() + 3, patch.end());
        r.d->UpdateSubresource(vb.get(), 0, &box2, tail.data(), 0, 0);
        r.d->Draw(6, 0);
        r.cap_main(o);
    });
    single(spec("copy", "map_dynamic_texture_write_discard").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        D3D10_TEXTURE2D_DESC d = D3d10Tools::desc2d(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DYNAMIC, D3D10_CPU_ACCESS_WRITE);
        Com<ID3D10Texture2D> t;
        r.check(r.d->CreateTexture2D(&d, nullptr, t.put()), "dynamic texture");
        if (!r.ok()) return;
        for (int pass = 0; pass < 2; ++pass) {
            D3D10_MAPPED_TEXTURE2D m{};
            r.check(t->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &m), "Map discard");
            if (!r.ok()) return;
            for (UINT y = 0; y < 32; ++y) {
                std::uint8_t* row = static_cast<std::uint8_t*>(m.pData) + y * m.RowPitch;
                for (UINT x = 0; x < 32; ++x) {
                    row[x * 4] = static_cast<std::uint8_t>(pass ? 255 - x * 8 : x * 8);
                    row[x * 4 + 1] = static_cast<std::uint8_t>(y * 8);
                    row[x * 4 + 2] = static_cast<std::uint8_t>(((x / 4 + y / 4) % 2) * 200 + pass * 30);
                    row[x * 4 + 3] = 255;
                }
            }
            t->Unmap(0);
        }
        show(r, o, t.get());
    });
    single(spec("copy", "staging_write_then_copy_to_default").tolerance(tol), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        D3D10_TEXTURE2D_DESC sd = D3d10Tools::desc2d(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D10_USAGE_STAGING, D3D10_CPU_ACCESS_WRITE | D3D10_CPU_ACCESS_READ);
        Com<ID3D10Texture2D> staging;
        r.check(r.d->CreateTexture2D(&sd, nullptr, staging.put()), "staging");
        Com<ID3D10Texture2D> dst = r.tex2d(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE);
        if (!r.ok()) return;
        D3D10_MAPPED_TEXTURE2D m{};
        r.check(staging->Map(0, D3D10_MAP_READ_WRITE, 0, &m), "Map staging");
        if (!r.ok()) return;
        for (UINT y = 0; y < 32; ++y) {
            std::uint8_t* row = static_cast<std::uint8_t*>(m.pData) + y * m.RowPitch;
            for (UINT x = 0; x < 32; ++x) {
                const bool ring = (x - 16) * (x - 16) + (y - 16) * (y - 16) < 150 && (x - 16) * (x - 16) + (y - 16) * (y - 16) > 40;
                row[x * 4] = ring ? 240 : 20;
                row[x * 4 + 1] = static_cast<std::uint8_t>(x * 8);
                row[x * 4 + 2] = static_cast<std::uint8_t>(y * 8);
                row[x * 4 + 3] = 255;
            }
        }
        staging->Unmap(0);
        r.d->CopyResource(dst.get(), staging.get());
        show(r, o, dst.get());
    });
    single(spec("copy", "clear_depth_stencil_partial_flags").tolerance(tol).uniform_ok("depth").uniform_ok("stencil"), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        r.depth_target(r.depth_main, 128, 128, DXGI_FORMAT_D24_UNORM_S8_UINT);
        if (!r.ok()) return;
        r.bind_rt(r.main, &r.depth_main);
        r.d->ClearDepthStencilView(r.depth_main.view.get(), D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL, 0.25f, 7);
        r.d->ClearDepthStencilView(r.depth_main.view.get(), D3D10_CLEAR_STENCIL, 0.9f, 33);
        r.use_std();
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        dd.DepthFunc = D3D10_COMPARISON_GREATER;
        dd.StencilEnable = TRUE;
        dd.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_EQUAL};
        dd.BackFace = dd.FrontFace;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        if (!r.ok()) return;
        r.d->OMSetDepthStencilState(ds.get(), 33);
        r.draw_std(Geo::quad(-0.8f, -0.8f, 0.8f, 0.8f, 0.5f, 0.9f, 0.5f, 0.2f));
        r.cap_main(o);
        r.cap_depth(o, "depth", r.depth_main);
        r.cap_stencil(o, "stencil", r.depth_main);
    });
    single(spec("copy", "clear_render_target_views_formats").tolerance(rr::Tolerance::floats(2e-3f, 2e-3f)), [](Rig& r, rr::CaptureSet& o) {
        const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                       DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R11G11B10_FLOAT};
        Target t[6];
        for (int i = 0; i < 6; ++i) r.target(t[i], 16, 16, formats[i]);
        if (!r.ok()) return;
        for (int i = 0; i < 6; ++i) r.clear(t[i], 0.2f + 0.1f * i, 0.7f - 0.1f * i, 0.45f, 0.6f);
        rr::Image row(96, 16, rr::PixelKind::Rgba32Float);
        for (int i = 0; i < 6; ++i) {
            rr::Image img;
            r.read(t[i], img);
            if (!r.ok()) return;
            rr::Image f = img.to_rgba8();
            for (UINT y = 0; y < 16; ++y)
                for (UINT x = 0; x < 16; ++x)
                    for (unsigned c = 0; c < 4; ++c) row.set_channel(i * 16 + x, y, c, f.channel(x, y, c) / 255.0);
        }
        o.add("", std::move(row));
    });
}};

const Registrar kCopyFrames{[] {
    struct S {
        Com<ID3D10Texture2D> tex;
        Com<ID3D10ShaderResourceView> view;
        Com<ID3D10SamplerState> smp;
        Com<ID3D10PixelShader> ps;
    };
    MultiBody<S> body;
    body.setup = [](Rig& r, S& s) {
        r.begin();
        r.use_std();
        D3D10_TEXTURE2D_DESC d = D3d10Tools::desc2d(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DYNAMIC, D3D10_CPU_ACCESS_WRITE);
        r.check(r.d->CreateTexture2D(&d, nullptr, s.tex.put()), "dynamic texture");
        if (!r.ok()) return;
        s.view = r.srv(s.tex.get());
        s.smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT));
        s.ps = r.ps("Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  return t.Sample(s, uv); }\n");
        r.clear(r.main, 0.1f, 0.1f, 0.15f, 1);
    };
    body.frame = [](Rig& r, S& s, unsigned f) {
        D3D10_MAPPED_TEXTURE2D m{};
        r.check(s.tex->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &m), "Map");
        if (!r.ok()) return;
        for (UINT y = 0; y < 32; ++y) {
            std::uint8_t* row = static_cast<std::uint8_t*>(m.pData) + y * m.RowPitch;
            for (UINT x = 0; x < 32; ++x) {
                const int dx = static_cast<int>(x) - 16 - static_cast<int>(f) * 2, dy = static_cast<int>(y) - 16;
                const bool disc = dx * dx + dy * dy < 100;
                row[x * 4] = disc ? 250 : static_cast<std::uint8_t>(x * 8);
                row[x * 4 + 1] = disc ? static_cast<std::uint8_t>(60 + f * 20) : static_cast<std::uint8_t>(y * 8);
                row[x * 4 + 2] = 90;
                row[x * 4 + 3] = 255;
            }
        }
        s.tex->Unmap(0);
        r.d->PSSetShader(s.ps.get());
        r.ps_srv(0, s.view.get());
        r.ps_smp(0, s.smp.get());
        const float sc = 0.45f + 0.05f * static_cast<float>(f);
        r.draw_std(Geo::quad_uv(-sc, -sc, sc, sc, 0.5f, 0, 0, 1, 1));
    };
    multi<S>(spec("copy", "map_discard_texture_stream").frames(6, {0, 2, 5}).tolerance(rr::Tolerance::blend()), body);
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
