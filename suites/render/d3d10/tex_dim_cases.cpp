#include "render/d3d10/util.hpp"

#include <cstring>

namespace lsw::suites::render::d3d10 {
namespace {

Com<ID3D10Texture1D> make_1d(Rig& r, UINT width, UINT array_size, UINT mips) {
    D3D10_TEXTURE1D_DESC desc{};
    desc.Width = width;
    desc.MipLevels = mips;
    desc.ArraySize = array_size;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    std::vector<rr::Pattern::Bytes> store;
    std::vector<D3D10_SUBRESOURCE_DATA> init;
    for (UINT a = 0; a < array_size; ++a) {
        rr::Pattern::Bytes base = tile_pattern(width, 8, a);
        base.resize(width * 4);
        for (UINT x = 0; x < width; ++x) {
            base[x * 4] = static_cast<std::uint8_t>((x * 255 / (width - 1) + a * 60) & 255);
            base[x * 4 + 1] = static_cast<std::uint8_t>(((x / 4) % 2) * 200 + 30);
            base[x * 4 + 2] = static_cast<std::uint8_t>(40 + a * 50);
            base[x * 4 + 3] = 255;
        }
        UINT w = width;
        rr::Pattern::Bytes level = base;
        for (UINT m = 0; m < mips; ++m) {
            store.push_back(level);
            D3D10_SUBRESOURCE_DATA d{};
            d.pSysMem = store.back().data();
            init.push_back(d);
            const UINT nw = std::max(1u, w / 2);
            rr::Pattern::Bytes next(nw * 4);
            for (UINT x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) next[x * 4 + c] = static_cast<std::uint8_t>((level[(x * 2) * 4 + c] + level[std::min(x * 2 + 1, w - 1) * 4 + c]) / 2);
            level = next;
            w = nw;
        }
    }
    std::vector<D3D10_SUBRESOURCE_DATA> ordered(init);
    Com<ID3D10Texture1D> out;
    r.check(r.d->CreateTexture1D(&desc, ordered.data(), out.put()), "CreateTexture1D");
    return out;
}

Com<ID3D10Texture3D> make_3d(Rig& r, UINT size, UINT mips) {
    D3D10_TEXTURE3D_DESC desc{};
    desc.Width = desc.Height = desc.Depth = size;
    desc.MipLevels = mips;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    std::vector<std::vector<std::uint8_t>> store;
    std::vector<D3D10_SUBRESOURCE_DATA> init;
    UINT s = size;
    for (UINT m = 0; m < mips; ++m) {
        std::vector<std::uint8_t> data(static_cast<std::size_t>(s) * s * s * 4);
        for (UINT z = 0; z < s; ++z)
            for (UINT y = 0; y < s; ++y)
                for (UINT x = 0; x < s; ++x) {
                    std::uint8_t* p = &data[((static_cast<std::size_t>(z) * s + y) * s + x) * 4];
                    p[0] = static_cast<std::uint8_t>(x * 255 / std::max(1u, s - 1));
                    p[1] = static_cast<std::uint8_t>(y * 255 / std::max(1u, s - 1));
                    p[2] = static_cast<std::uint8_t>(((x / 2 + y / 2 + z) % 2) ? 220 : 40 + z * 8);
                    p[3] = 255;
                }
        store.push_back(std::move(data));
        D3D10_SUBRESOURCE_DATA d{};
        d.pSysMem = store.back().data();
        d.SysMemPitch = s * 4;
        d.SysMemSlicePitch = s * s * 4;
        init.push_back(d);
        s = std::max(1u, s / 2);
    }
    Com<ID3D10Texture3D> out;
    r.check(r.d->CreateTexture3D(&desc, init.data(), out.put()), "CreateTexture3D");
    return out;
}

struct ArrayTex {
    Com<ID3D10Texture2D> tex;
    UINT size = 0;
    UINT mips = 0;
    UINT slices = 0;
};

ArrayTex make_array(Rig& r, UINT size, UINT slices, UINT mips, UINT misc = 0, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
    ArrayTex out;
    out.size = size;
    out.mips = mips;
    out.slices = slices;
    std::vector<rr::Pattern::Bytes> chains;
    std::vector<const void*> data;
    std::vector<UINT> pitch;
    for (UINT s = 0; s < slices; ++s) {
        const std::vector<rr::Pattern::Bytes> chain = rr::Pattern::mip_chain(tile_pattern(size, size, s), size, size, mips);
        for (UINT m = 0; m < mips; ++m) {
            chains.push_back(chain[m]);
        }
    }
    for (UINT s = 0; s < slices; ++s)
        for (UINT m = 0; m < mips; ++m) {
            data.push_back(chains[s * mips + m].data());
            pitch.push_back(std::max(1u, size >> m) * 4);
        }
    out.tex = r.tex2d(size, size, format, D3D10_BIND_SHADER_RESOURCE, mips, slices, data, pitch, misc);
    return out;
}

Com<ID3D10Texture2D> coded_tex(Rig& r, UINT size) {
    const UINT levels = rr::Pattern::level_count(size, size);
    std::vector<rr::Pattern::Bytes> store;
    std::vector<const void*> data;
    std::vector<UINT> pitch;
    for (UINT l = 0; l < levels; ++l) {
        const UINT w = std::max(1u, size >> l);
        const float* c = palette(l);
        store.push_back(rr::Pattern::checker(w, w, std::max(1u, w / 8), rr::Pattern::pack(static_cast<unsigned>(c[0] * 255), static_cast<unsigned>(c[1] * 255), static_cast<unsigned>(c[2] * 255)),
                                             rr::Pattern::pack(static_cast<unsigned>(c[0] * 140), static_cast<unsigned>(c[1] * 140), static_cast<unsigned>(c[2] * 140))));
    }
    for (UINT l = 0; l < levels; ++l) {
        data.push_back(store[l].data());
        pitch.push_back(std::max(1u, size >> l) * 4);
    }
    return r.tex2d(size, size, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, levels, 1, data, pitch);
}

const char* kDimNames[] = {"1d", "1d_array"};

void filter_scene(Rig& r, rr::CaptureSet& o, D3D10_FILTER filter, float aniso) {
    r.begin();
    r.use_std();
    Com<ID3D10Texture2D> tex = coded_tex(r, 64);
    Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
    auto sd = Rig::sampler_desc(filter, D3D10_TEXTURE_ADDRESS_WRAP);
    sd.MaxAnisotropy = static_cast<UINT>(aniso);
    Com<ID3D10SamplerState> smp = r.sampler(sd);
    if (!r.ok()) return;
    std::vector<Vtx> a = Geo::quad_uv(-1, 0, 0, 1, 0.5f, 0.05f, 0.05f, 0.30f, 0.30f);
    std::vector<Vtx> b = Geo::quad_uv(0, 0, 1, 1, 0.5f, 0, 0, 1.5f, 1.5f);
    std::vector<Vtx> c = Geo::quad_uv(-1, -1, 1, 0, 0.5f, 0, 0, 4.5f, 2.25f);
    a.insert(a.end(), b.begin(), b.end());
    a.insert(a.end(), c.begin(), c.end());
    paint(r, o, "Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  return t.Sample(s, uv); }\n", {view.get()}, {smp.get()}, a);
}

void aniso_scene(Rig& r, rr::CaptureSet& o, UINT level) {
    r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    Com<ID3D10Texture2D> tex = coded_tex(r, 128);
    Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
    auto sd = Rig::sampler_desc(D3D10_FILTER_ANISOTROPIC, D3D10_TEXTURE_ADDRESS_WRAP);
    sd.MaxAnisotropy = r.mutate("tex_aniso") ? 4u : level;
    Com<ID3D10SamplerState> smp = r.sampler(sd);
    ScenePipe pipe(r, "Texture2D t : register(t0); SamplerState s : register(s0);\n"
                      "float4 main(float4 p : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, float3 w : TEXCOORD1) : SV_Target { return t.Sample(s, uv * 12); }\n");
    std::vector<float> flat(9 * 9, 0.0f);
    MeshBuffers m = upload(r, Geo::grid(8, 8.0f, flat));
    auto dd = Rig::depth_desc();
    dd.DepthEnable = TRUE;
    Com<ID3D10DepthStencilState> ds = r.depth(dd);
    if (!r.ok()) return;
    r.d->OMSetDepthStencilState(ds.get(), 0);
    pipe.bind(r);
    r.ps_srv(0, view.get());
    r.ps_smp(0, smp.get());
    pipe.draw(r, m, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.3f, 60.0f) * rr::Mat4::rotate_x(0.04f) * rr::Mat4::translate(0, -0.7f, -3.0f));
    r.cap_main(o);
}

const char* kAddressNames[] = {"wrap", "mirror", "clamp", "border", "mirror_once"};
const D3D10_TEXTURE_ADDRESS_MODE kAddress[] = {D3D10_TEXTURE_ADDRESS_WRAP, D3D10_TEXTURE_ADDRESS_MIRROR, D3D10_TEXTURE_ADDRESS_CLAMP, D3D10_TEXTURE_ADDRESS_BORDER,
                                               D3D10_TEXTURE_ADDRESS_MIRROR_ONCE};

void address_scene(Rig& r, rr::CaptureSet& o, D3D10_TEXTURE_ADDRESS_MODE mode, const float border[4], D3D10_FILTER filter) {
    r.begin();
    r.use_std();
    ArrayTex t = make_array(r, 16, 1, 1);
    Com<ID3D10ShaderResourceView> view = r.srv(t.tex.get());
    auto sd = Rig::sampler_desc(filter, mode);
    std::memcpy(sd.BorderColor, border, 16);
    Com<ID3D10SamplerState> smp = r.sampler(sd);
    if (!r.ok()) return;
    paint(r, o, "Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  return t.Sample(s, uv * 3.0 - 1.0); }\n", {view.get()}, {smp.get()}, full_uv());
}

void lod_scene(Rig& r, rr::CaptureSet& o, float scale, float bias, float min_lod, float max_lod) {
    r.begin();
    r.use_std();
    Com<ID3D10Texture2D> tex = coded_tex(r, 64);
    Com<ID3D10ShaderResourceView> view = r.srv(tex.get());
    auto sd = Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_POINT, D3D10_TEXTURE_ADDRESS_WRAP);
    sd.MipLODBias = r.mutate("tex_lod") ? 0.0f : bias;
    sd.MinLOD = min_lod;
    sd.MaxLOD = r.mutate("tex_lod") ? D3D10_FLOAT32_MAX : max_lod;
    Com<ID3D10SamplerState> smp = r.sampler(sd);
    if (!r.ok()) return;
    paint(r, o, "Texture2D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  return t.Sample(s, uv * " + std::to_string(scale) + "); }\n", {view.get()}, {smp.get()}, full_uv());
}

void sample_fn_scene(Rig& r, rr::CaptureSet& o, const char* body, int variant) {
    r.begin();
    r.use_std();
    ArrayTex t = make_array(r, 64, 1, rr::Pattern::level_count(64, 64));
    Com<ID3D10ShaderResourceView> view = r.srv(t.tex.get());
    Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_WRAP));
    (void)variant;
    if (!r.ok()) return;
    paint(r, o, std::string("Texture2D t : register(t0); SamplerState s : register(s0);\n") + PS_HEAD + body + "}\n", {view.get()}, {smp.get()}, full_uv());
}

const Registrar kDims{[] {
    single(spec("tex", "dim_1d_mipped").tolerance(rr::Tolerance::filter(6, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture1D> t = make_1d(r, 64, 1, 7);
        Com<ID3D10ShaderResourceView> view = r.srv(t.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_WRAP));
        if (!r.ok()) return;
        paint(r, o, "Texture1D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  float u = uv.x * (1 + floor(uv.y * 4) * 1.5); return t.Sample(s, u); }\n", {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "dim_1d_array").tolerance(rr::Tolerance::filter(6, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture1D> t = make_1d(r, 64, 4, 7);
        Com<ID3D10ShaderResourceView> view = r.srv(t.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_WRAP));
        if (!r.ok()) return;
        paint(r, o, "Texture1DArray t : register(t0); SamplerState s : register(s0);\n" PS_HEAD "  return t.Sample(s, float2(uv.x * 2, floor(uv.y * 4))); }\n", {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "dim_2d_array_slices_with_mips").tolerance(rr::Tolerance::filter(6, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        ArrayTex t = make_array(r, 32, 6, 6);
        Com<ID3D10ShaderResourceView> view = r.srv(t.tex.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_WRAP));
        if (!r.ok()) return;
        paint(r, o, "Texture2DArray t : register(t0); SamplerState s : register(s0);\n" PS_HEAD
                    "  float2 cell = floor(uv * float2(3, 2)); float2 f = frac(uv * float2(3, 2)); return t.Sample(s, float3(f * (1 + cell.x), cell.y * 3 + cell.x)); }\n",
              {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "dim_3d_volume_slices").tolerance(rr::Tolerance::filter(6, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture3D> t = make_3d(r, 16, 5);
        Com<ID3D10ShaderResourceView> view = r.srv(t.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        if (!r.ok()) return;
        paint(r, o, "Texture3D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD
                    "  float2 cell = floor(uv * float2(4, 2)); float2 f = frac(uv * float2(4, 2)); return t.Sample(s, float3(f, (cell.y * 4 + cell.x + 0.5) / 8.0)); }\n",
              {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "dim_3d_volume_mip_levels").tolerance(rr::Tolerance::filter(6, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Texture3D> t = make_3d(r, 16, 5);
        Com<ID3D10ShaderResourceView> view = r.srv(t.get());
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        if (!r.ok()) return;
        paint(r, o, "Texture3D t : register(t0); SamplerState s : register(s0);\n" PS_HEAD
                    "  float2 cell = floor(uv * float2(4, 1)); float2 f = frac(uv * float2(4, 1)); return t.SampleLevel(s, float3(f, 0.4), cell.x + 0.5); }\n",
              {view.get()}, {smp.get()}, full_uv());
    });
    const char* cube_ps =
        "TextureCube t : register(t0); SamplerState s : register(s0);\n" PS_HEAD
        "  float2 q = uv * 2 - 1; float3 d = float3(q.x * 1.0, -q.y, 1.0); float a = 0.9; float3 e = float3(d.x * cos(a) + d.z * sin(a), d.y, -d.x * sin(a) + d.z * cos(a));\n"
        "  return t.Sample(s, e); }\n";
    single(spec("tex", "dim_cube_mipped").tolerance(rr::Tolerance::filter(6, 0.01)), [cube_ps](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        ArrayTex t = make_array(r, 32, 6, 6, D3D10_RESOURCE_MISC_TEXTURECUBE);
        D3D10_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        vd.ViewDimension = D3D10_SRV_DIMENSION_TEXTURECUBE;
        vd.TextureCube.MipLevels = 6;
        Com<ID3D10ShaderResourceView> view = r.srv(t.tex.get(), &vd);
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        if (!r.ok()) return;
        paint(r, o, cube_ps, {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "dim_cube_array", 1).tolerance(rr::Tolerance::filter(6, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        ArrayTex t = make_array(r, 32, 12, 4, D3D10_RESOURCE_MISC_TEXTURECUBE);
        D3D10_SHADER_RESOURCE_VIEW_DESC1 vd{};
        vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        vd.ViewDimension = D3D10_1_SRV_DIMENSION_TEXTURECUBEARRAY;
        vd.TextureCubeArray.MipLevels = 4;
        vd.TextureCubeArray.NumCubes = 2;
        Com<ID3D10ShaderResourceView1> view;
        r.check(r.kit.dev.device1->CreateShaderResourceView1(t.tex.get(), &vd, view.put()), "CreateShaderResourceView1");
        Com<ID3D10SamplerState> smp = r.sampler(Rig::sampler_desc(D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D10_TEXTURE_ADDRESS_CLAMP));
        if (!r.ok()) return;
        paint(r, o, "TextureCubeArray t : register(t0); SamplerState s : register(s0);\n" PS_HEAD
                    "  float2 q = uv * 2 - 1; float3 d = float3(q.x, -q.y, 1.0); float cube = uv.x < 0.5 ? 0 : 1; return t.Sample(s, float4(d, cube)); }\n",
              {view.get()}, {smp.get()}, full_uv());
    });
    single(spec("tex", "dim_buffer_srv_float4_and_uint").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        std::vector<float> data(256 * 4);
        for (unsigned i = 0; i < 256; ++i) {
            data[i * 4] = (i % 16) / 15.0f;
            data[i * 4 + 1] = (i / 16) / 15.0f;
            data[i * 4 + 2] = ((i * 7) % 13) / 12.0f;
            data[i * 4 + 3] = 1.0f;
        }
        Com<ID3D10Buffer> buffer = r.buf(static_cast<UINT>(data.size() * 4), D3D10_USAGE_DEFAULT, D3D10_BIND_SHADER_RESOURCE, 0, data.data());
        D3D10_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        vd.ViewDimension = D3D10_SRV_DIMENSION_BUFFER;
        vd.Buffer.FirstElement = 16;
        vd.Buffer.NumElements = 128;
        Com<ID3D10ShaderResourceView> view = r.srv(buffer.get(), &vd);
        D3D10_SHADER_RESOURCE_VIEW_DESC ud = vd;
        ud.Format = DXGI_FORMAT_R32_UINT;
        ud.Buffer.FirstElement = 0;
        ud.Buffer.NumElements = 1024;
        Com<ID3D10ShaderResourceView> uview = r.srv(buffer.get(), &ud);
        if (!r.ok()) return;
        paint(r, o, "Buffer<float4> b : register(t0); Buffer<uint> u : register(t1);\n" PS_HEAD
                    "  int2 xy = int2(uv * float2(16, 8)); float4 v = b.Load(xy.y * 16 + xy.x); uint raw = u.Load(xy.x * 4 + xy.y * 64 + 1); return float4(v.rgb * 0.7 + 0.3 * float((raw >> 20) & 255) / 255.0, 1); }\n",
              {view.get(), uview.get()}, {}, full_uv());
    });
}};

const Registrar kFilters{[] {
    struct F {
        const char* name;
        D3D10_FILTER filter;
    };
    const F filters[] = {{"point_point_point", D3D10_FILTER_MIN_MAG_MIP_POINT},
                         {"point_point_linear", D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR},
                         {"point_linear_point", D3D10_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT},
                         {"point_linear_linear", D3D10_FILTER_MIN_POINT_MAG_MIP_LINEAR},
                         {"linear_point_point", D3D10_FILTER_MIN_LINEAR_MAG_MIP_POINT},
                         {"linear_point_linear", D3D10_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR},
                         {"linear_linear_point", D3D10_FILTER_MIN_MAG_LINEAR_MIP_POINT},
                         {"linear_linear_linear", D3D10_FILTER_MIN_MAG_MIP_LINEAR}};
    for (const F& f : filters) {
        const D3D10_FILTER filter = f.filter;
        single(spec("tex", std::string("filter_") + f.name).tolerance(rr::Tolerance::filter(10, 0.01).with_edge(1)), [filter](Rig& r, rr::CaptureSet& o) { filter_scene(r, o, filter, 1); });
    }
    for (UINT level : {4u, 16u}) {
        single(spec("tex", "filter_anisotropic_" + std::to_string(level) + "x").tolerance(rr::Tolerance::psnr(25.0)), [level](Rig& r, rr::CaptureSet& o) { aniso_scene(r, o, level); });
    }
    const float black[4] = {0, 0, 0, 1};
    const float transparent[4] = {0, 0, 0, 0};
    const float custom[4] = {0.9f, 0.2f, 0.6f, 1};
    for (int i = 0; i < 5; ++i) {
        const int idx = i;
        single(spec("tex", std::string("address_") + kAddressNames[i]).tolerance(rr::Tolerance::filter(6, 0.01)),
               [idx, black](Rig& r, rr::CaptureSet& o) { address_scene(r, o, kAddress[idx], black, D3D10_FILTER_MIN_MAG_MIP_POINT); });
    }
    single(spec("tex", "address_border_custom_color_linear").tolerance(rr::Tolerance::filter(6, 0.01)),
           [custom](Rig& r, rr::CaptureSet& o) { address_scene(r, o, D3D10_TEXTURE_ADDRESS_BORDER, custom, D3D10_FILTER_MIN_MAG_MIP_LINEAR); });
    single(spec("tex", "address_border_transparent_linear").tolerance(rr::Tolerance::filter(6, 0.01)),
           [transparent](Rig& r, rr::CaptureSet& o) { address_scene(r, o, D3D10_TEXTURE_ADDRESS_BORDER, transparent, D3D10_FILTER_MIN_MAG_MIP_LINEAR); });
    single(spec("tex", "address_mirror_once_linear").tolerance(rr::Tolerance::filter(6, 0.01)),
           [black](Rig& r, rr::CaptureSet& o) { address_scene(r, o, D3D10_TEXTURE_ADDRESS_MIRROR_ONCE, black, D3D10_FILTER_MIN_MAG_MIP_LINEAR); });
    single(spec("tex", "lod_bias_positive").tolerance(rr::Tolerance::filter(8, 0.01).with_edge(1)), [](Rig& r, rr::CaptureSet& o) { lod_scene(r, o, 6.0f, 1.5f, 0, D3D10_FLOAT32_MAX); });
    single(spec("tex", "lod_bias_negative").tolerance(rr::Tolerance::filter(8, 0.01).with_edge(1)), [](Rig& r, rr::CaptureSet& o) { lod_scene(r, o, 12.0f, -1.5f, 0, D3D10_FLOAT32_MAX); });
    single(spec("tex", "lod_min_clamp").tolerance(rr::Tolerance::filter(8, 0.01)), [](Rig& r, rr::CaptureSet& o) { lod_scene(r, o, 2.0f, 0, 2.5f, D3D10_FLOAT32_MAX); });
    single(spec("tex", "lod_max_clamp").tolerance(rr::Tolerance::filter(8, 0.01)), [](Rig& r, rr::CaptureSet& o) { lod_scene(r, o, 12.0f, 0, 0, 2.0f); });
    single(spec("tex", "sample_level_explicit").tolerance(rr::Tolerance::filter(8, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        sample_fn_scene(r, o, "  return t.SampleLevel(s, uv * 2, floor(uv.x * 4) + frac(uv.y * 3)); ", 0);
    });
    single(spec("tex", "sample_grad_explicit").tolerance(rr::Tolerance::filter(8, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        sample_fn_scene(r, o, "  float k = 0.004 + uv.x * 0.04; return t.SampleGrad(s, uv * 2, float2(k, 0), float2(0, k * (1 + uv.y))); ", 0);
    });
    single(spec("tex", "sample_bias_explicit").tolerance(rr::Tolerance::filter(8, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        sample_fn_scene(r, o, "  return t.SampleBias(s, uv * 2, uv.x * 4 - 1); ", 0);
    });
    single(spec("tex", "sample_texel_offset").tolerance(rr::Tolerance::filter(8, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        sample_fn_scene(r, o, "  return t.Sample(s, uv, int2(3, -2)) * 0.5 + t.Sample(s, uv, int2(-4, 5)) * 0.5; ", 0);
    });
    single(spec("tex", "load_with_offset").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        sample_fn_scene(r, o, "  int2 xy = int2(uv * 56) + 3; return t.Load(int3(xy, 1), int2(1, -1)) + t.Load(int3(xy, 0), int2(-2, 2)); ", 0);
    });
    single(spec("tex", "gather_all_channels", 1).tolerance(rr::Tolerance::filter(6, 0.01)), [](Rig& r, rr::CaptureSet& o) {
        sample_fn_scene(r, o, "  float2 q = uv * 1.3; float4 a = t.Gather(s, q); float4 b = t.Gather(s, q + 0.02); return float4(a.x + a.z, b.y + b.w, a.x * b.w + a.y * b.z, 1) * 0.5; ", 0);
    });
    single(spec("tex", "calculate_level_of_detail", 1).tolerance(rr::Tolerance::filter(8, 0.02)), [](Rig& r, rr::CaptureSet& o) {
        sample_fn_scene(r, o, "  float2 q = 0.3 + uv * uv * 7; return float4(t.CalculateLevelOfDetail(s, q) / 6.0, t.CalculateLevelOfDetailUnclamped(s, q) / 6.0 + 0.5, 0, 1); ", 0);
    });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
