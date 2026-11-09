#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kShow =
    "Texture2D t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VO i) : SV_Target { return t.SampleLevel(s, i.uv, 0); }\n";

bool show(Rig& r, Cap& out, ID3D11Resource* tex, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc = nullptr, const char* ps_source = kShow) {
    Target target;
    Com<ID3D11PixelShader> ps = r.ps(ps_source);
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11ShaderResourceView> srv = r.srv(tex, desc);
    if (!ps || !smp || !srv || !r.color(target)) return false;
    r.bind(target);
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* s = smp.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &s);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}

Bytes pattern(unsigned seed) {
    return rr::Pattern::checker(64, 64, 8, rr::Pattern::pack(240 - seed * 30, 200, 40 + seed * 50), rr::Pattern::pack(30 + seed * 40, 60 + seed * 20, 220));
}

const Add kCopyResource(spec("copy", "copy_resource").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> a = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, pattern(0));
    Com<ID3D11Texture2D> b = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    if (!a || !b) return false;
    r.ctx->CopyResource(b.get(), a.get());
    return show(r, out, b.get());
});

const Add kCopyBox(spec("copy", "copy_subresource_region_box_offsets").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> a = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, pattern(0));
    Com<ID3D11Texture2D> b = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, pattern(2));
    if (!a || !b) return false;
    const D3D11_BOX box{5, 7, 0, 45, 31, 1};
    r.ctx->CopySubresourceRegion(b.get(), 0, 17, 29, 0, a.get(), 0, &box);
    return show(r, out, b.get());
});

const Add kCopyBc(spec("copy", "copy_subresource_region_bc1_block_box").tolerance(rr::Tolerance::filter(6, 0.004)), [](Rig& r, Cap& out) {
    Bytes b0 = BcCodec::blocks(1, 77, 64);
    Bytes b1 = BcCodec::blocks(1, 78, 64);
    D3D11_SUBRESOURCE_DATA i0{b0.data(), 8 * 8, 0};
    D3D11_SUBRESOURCE_DATA i1{b1.data(), 8 * 8, 0};
    Com<ID3D11Texture2D> a = r.tex(32, 32, DXGI_FORMAT_BC1_UNORM, D3D11_BIND_SHADER_RESOURCE, 1, 1, 1, &i0);
    Com<ID3D11Texture2D> b = r.tex(32, 32, DXGI_FORMAT_BC1_UNORM, D3D11_BIND_SHADER_RESOURCE, 1, 1, 1, &i1);
    if (!a || !b) return false;
    const D3D11_BOX box{4, 8, 0, 20, 28, 1};
    r.ctx->CopySubresourceRegion(b.get(), 0, 12, 0, 0, a.get(), 0, &box);
    return show(r, out, b.get());
});

const Add kCopy3d(spec("copy", "copy_subresource_region_3d_box").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    auto make = [&](unsigned seed) {
        Bytes data(16 * 16 * 16 * 4);
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    std::uint8_t* p = &data[((static_cast<std::size_t>(z) * 16 + y) * 16 + x) * 4];
                    p[0] = static_cast<std::uint8_t>(x * 16 + seed);
                    p[1] = static_cast<std::uint8_t>(y * 16);
                    p[2] = static_cast<std::uint8_t>(z * 16 + seed * 7);
                    p[3] = 255;
                }
        D3D11_TEXTURE3D_DESC d{};
        d.Width = d.Height = d.Depth = 16;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{data.data(), 64, 1024};
        Com<ID3D11Texture3D> t;
        r.ok(r.dev->CreateTexture3D(&d, &init, t.put()), "CreateTexture3D");
        return t;
    };
    Com<ID3D11Texture3D> a = make(0);
    Com<ID3D11Texture3D> b = make(40);
    if (!a || !b) return false;
    const D3D11_BOX box{2, 3, 4, 12, 13, 10};
    r.ctx->CopySubresourceRegion(b.get(), 0, 3, 1, 5, a.get(), 0, &box);
    return show(r, out, b.get(), nullptr,
                "Texture3D t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
                "float4 main(VO i) : SV_Target { int2 c = int2(i.uv * 4.0 * 16.0) % 16; int sl = int(i.uv.x * 4) + 4 * int(i.uv.y * 4); return t.Load(int4(c.x, c.y, sl, 0)); }\n");
});

const Add kCrossFormat(spec("copy", "copy_resource_typeless_to_unorm_srgb_views").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> a = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, pattern(1));
    Com<ID3D11Texture2D> b = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_BIND_SHADER_RESOURCE);
    if (!a || !b) return false;
    r.ctx->CopyResource(b.get(), a.get());
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipLevels = 1;
    return show(r, out, b.get(), &d);
});

const Add kUpdatePitch(spec("copy", "update_subresource_box_with_row_pitch").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> t = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, pattern(0));
    if (!t) return false;
    Bytes padded(40 * 256, 0);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 50; ++x) {
            padded[y * 256 + x * 4] = static_cast<std::uint8_t>(x * 5);
            padded[y * 256 + x * 4 + 1] = static_cast<std::uint8_t>(y * 6);
            padded[y * 256 + x * 4 + 2] = 120;
            padded[y * 256 + x * 4 + 3] = 255;
        }
    const D3D11_BOX box{6, 9, 0, 56, 49, 1};
    r.ctx->UpdateSubresource(t.get(), 0, &box, padded.data(), 256, 0);
    return show(r, out, t.get());
});

const Add kMapDiscard(spec("copy", "map_write_discard_dynamic_texture_twice").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    D3D11_TEXTURE2D_DESC d = D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    Com<ID3D11Texture2D> t;
    if (!r.ok(r.dev->CreateTexture2D(&d, nullptr, t.put()), "CreateTexture2D")) return false;
    Target target;
    Com<ID3D11PixelShader> ps = r.ps("Texture2D t : register(t0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return t.Load(int3(int2(frac(i.uv * float2(2, 1)) * 64), 0)); }\n");
    Com<ID3D11ShaderResourceView> srv = r.srv(t.get());
    if (!ps || !srv || !r.color(target)) return false;
    r.bind(target);
    ID3D11ShaderResourceView* v = srv.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.ScissorEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    r.ctx->RSSetState(rs.get());
    for (int pass = 0; pass < 2; ++pass) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!r.ok(r.ctx->Map(t.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map")) return false;
        const Bytes p = pattern(pass * 2);
        for (int y = 0; y < 64; ++y) std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + y * mapped.RowPitch, p.data() + y * 256, 256);
        r.ctx->Unmap(t.get(), 0);
        const D3D11_RECT half{pass * 64, 0, pass * 64 + 64, 128};
        r.ctx->RSSetScissorRects(1, &half);
        r.draw_fs(ps.get());
    }
    return r.shot(out, target);
});

const Add kStagingRmw(spec("copy", "staging_read_modify_write_copy_back").tolerance(rr::Tolerance::exact()), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> t = r.tex_data(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, pattern(1));
    D3D11_TEXTURE2D_DESC d = D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);
    Com<ID3D11Texture2D> staging;
    if (!t || !r.ok(r.dev->CreateTexture2D(&d, nullptr, staging.put()), "staging")) return false;
    r.ctx->CopyResource(staging.get(), t.get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (!r.ok(r.ctx->Map(staging.get(), 0, D3D11_MAP_READ_WRITE, 0, &m), "Map")) return false;
    for (int y = 0; y < 64; ++y) {
        std::uint8_t* row = static_cast<std::uint8_t*>(m.pData) + y * m.RowPitch;
        for (int x = 0; x < 64; ++x) {
            row[x * 4] = static_cast<std::uint8_t>(255 - row[x * 4]);
            row[x * 4 + 1] = static_cast<std::uint8_t>((row[x * 4 + 1] + x * 3) & 255);
        }
    }
    r.ctx->Unmap(staging.get(), 0);
    r.ctx->CopyResource(t.get(), staging.get());
    return show(r, out, t.get());
});

bool resolve_scene(Rig& r, Cap& out, DXGI_FORMAT format) {
    Target target;
    Basic basic;
    if (!r.color(target, 128, 128, format, 4) || !basic.make(r)) return false;
    r.bind(target);
    D3D11_RASTERIZER_DESC rd = rdesc();
    rd.MultisampleEnable = TRUE;
    Com<ID3D11RasterizerState> rs = r.rast(rd);
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(7, 0.85f, 0, 0, 0.1f));
    if (!rs || !vb) return false;
    r.ctx->RSSetState(rs.get());
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(21, 0);
    rr::Image image;
    Com<ID3D11Texture2D> resolved;
    D3D11_TEXTURE2D_DESC d = D3d11Tools::desc2d(128, 128, format, 0);
    if (!r.ok(r.dev->CreateTexture2D(&d, nullptr, resolved.put()), "resolve target")) return false;
    r.ctx->ResolveSubresource(resolved.get(), 0, target.texture.get(), 0, format);
    if (!r.read_image(resolved.get(), format, 0, image)) return false;
    out.add("", std::move(image));
    return true;
}
const Add kResRgba8(spec("copy", "resolve_subresource_r8g8b8a8_unorm").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) { return resolve_scene(r, out, DXGI_FORMAT_R8G8B8A8_UNORM); },
                    [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });
const Add kResRgba16(spec("copy", "resolve_subresource_r16g16b16a16_float").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) { return resolve_scene(r, out, DXGI_FORMAT_R16G16B16A16_FLOAT); },
                     [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R16G16B16A16_FLOAT, 4); });
const Add kResBgra(spec("copy", "resolve_subresource_b8g8r8a8_unorm").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) { return resolve_scene(r, out, DXGI_FORMAT_B8G8R8A8_UNORM); },
                   [](Rig& r) { return need_msaa(r, DXGI_FORMAT_B8G8R8A8_UNORM, 4); });

const Add kClearView(spec("copy", "clear_view_with_rects").variant(1).tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    Target target;
    if (!r.color(target)) return false;
    ID3D11DeviceContext1* raw = nullptr;
    r.ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&raw));
    Com<ID3D11DeviceContext1> c1(raw);
    if (!c1) return r.fail("ID3D11DeviceContext1 is unavailable");
    r.bind(target);
    const float bg[4] = {0.1f, 0.3f, 0.5f, 1};
    c1->ClearView(target.view.get(), bg, nullptr, 0);
    const D3D11_RECT rects[3] = {{4, 4, 60, 40}, {70, 10, 120, 100}, {10, 80, 50, 124}};
    const float color[4] = {0.9f, 0.4f, 0.1f, 1};
    c1->ClearView(target.view.get(), color, rects, 3);
    return r.shot(out, target);
}, [](Rig& r) -> std::string {
    D3D11_FEATURE_DATA_D3D11_OPTIONS o{};
    return SUCCEEDED(r.dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof o)) && o.ClearView ? std::string() : std::string("ClearView");
});

const Add kDiscardView(spec("copy", "discard_view_then_redraw").variant(1).tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    if (!r.color(target) || !basic.make(r)) return false;
    ID3D11DeviceContext1* raw = nullptr;
    r.ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&raw));
    Com<ID3D11DeviceContext1> c1(raw);
    if (!c1) return r.fail("ID3D11DeviceContext1 is unavailable");
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(6, 0.8f));
    if (!vb) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(18, 0);
    c1->DiscardView(target.view.get());
    const float clear[4] = {0.2f, 0.1f, 0.3f, 1};
    r.ctx->ClearRenderTargetView(target.view.get(), clear);
    r.ctx->Draw(18, 0);
    return r.shot(out, target);
});

const Add kUpdate1(spec("copy", "update_subresource1_copy_discard_constant_buffer").variant(1).tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Target target;
    CubePipe cubes;
    Depth depth;
    ID3D11DeviceContext1* raw = nullptr;
    r.ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&raw));
    Com<ID3D11DeviceContext1> c1(raw);
    if (!c1 || !r.color(target) || !r.depth(depth) || !cubes.make(r)) return c1 ? false : r.fail("ID3D11DeviceContext1 is unavailable");
    r.bind(target, &depth);
    cubes.bind(r);
    Com<ID3D11DepthStencilState> ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
    r.ctx->OMSetDepthStencilState(ds.get(), 0);
    const rr::Mat4 a = CubePipe::camera() * rr::Mat4::translate(-0.8f, 0, 0) * rr::Mat4::rotate_y(0.5f) * rr::Mat4::rotate_x(0.4f);
    const rr::Mat4 b = CubePipe::camera() * rr::Mat4::translate(0.8f, 0.2f, 0.4f) * rr::Mat4::rotate_y(-0.9f) * rr::Mat4::rotate_z(0.5f);
    c1->UpdateSubresource1(cubes.cb_.get(), 0, nullptr, a.m, 0, 0, D3D11_COPY_DISCARD);
    r.ctx->DrawIndexed(cubes.count_, 0, 0);
    c1->UpdateSubresource1(cubes.cb_.get(), 0, nullptr, b.m, 0, 0, D3D11_COPY_DISCARD);
    r.ctx->DrawIndexed(cubes.count_, 0, 0);
    return r.shot(out, target);
});

const Add kArraySliceCopy(spec("copy", "copy_array_slice_and_mip_subresources").tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) {
    const unsigned levels = rr::Pattern::level_count(32, 32);
    std::vector<std::vector<Bytes>> chains;
    std::vector<D3D11_SUBRESOURCE_DATA> data;
    for (unsigned a = 0; a < 3; ++a) chains.push_back(rr::Pattern::mip_chain(rr::Pattern::checker(32, 32, 4, rr::Pattern::pack(250 - a * 90, 40 + a * 90, 90), rr::Pattern::pack(20, 30 + a * 60, 200 - a * 60)), 32, 32, levels));
    for (unsigned a = 0; a < 3; ++a)
        for (unsigned l = 0; l < levels; ++l) data.push_back({chains[a][l].data(), std::max(1u, 32u >> l) * 4, 0});
    Com<ID3D11Texture2D> t = r.tex(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, levels, 3, 1, data.data());
    if (!t) return false;
    r.ctx->CopySubresourceRegion(t.get(), D3D11CalcSubresource(0, 0, levels), 0, 0, 0, t.get(), D3D11CalcSubresource(0, 2, levels), nullptr);
    r.ctx->CopySubresourceRegion(t.get(), D3D11CalcSubresource(1, 1, levels), 0, 0, 0, t.get(), D3D11CalcSubresource(1, 2, levels), nullptr);
    return show(r, out, t.get(), nullptr,
                "Texture2DArray t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
                "float4 main(VO i) : SV_Target { float2 q = frac(i.uv * float2(3, 2)); uint sl = uint(i.uv.x * 3); float lod = floor(i.uv.y * 2); return t.SampleLevel(s, float3(q, sl), lod); }\n");
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
