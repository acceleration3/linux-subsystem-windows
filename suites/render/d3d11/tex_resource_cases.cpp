#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

const char* const kHead = "SamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n";

bool show(Rig& r, Cap& out, const std::string& ps_source, const std::vector<ID3D11ShaderResourceView*>& views, D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
          UINT size = 128) {
    Target target;
    Com<ID3D11PixelShader> ps = r.ps(kHead + ps_source);
    Com<ID3D11SamplerState> sampler = r.smp(sdesc(filter, D3D11_TEXTURE_ADDRESS_WRAP));
    if (!ps || !sampler || !r.color(target, size, size)) return false;
    r.bind(target);
    ID3D11SamplerState* sp = sampler.get();
    r.ctx->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    r.ctx->PSSetSamplers(0, 1, &sp);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}

Bytes solid_rgba(unsigned w, unsigned h, unsigned seed) {
    Bytes b = rr::Pattern::checker(w, h, std::max(1u, w / 4), rr::Pattern::pack(40 + seed * 37 % 200, 200 - seed * 29 % 150, 60 + seed * 53 % 180),
                                  rr::Pattern::pack(230 - seed * 41 % 120, 70 + seed * 61 % 150, 220 - seed * 17 % 100));
    return b;
}

const Add kTex1D(spec("tex", "dimension_texture1d").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Bytes data(64 * 4);
    for (int x = 0; x < 64; ++x) {
        data[x * 4 + 0] = static_cast<std::uint8_t>(x * 4);
        data[x * 4 + 1] = static_cast<std::uint8_t>((x / 8) % 2 ? 220 : 40);
        data[x * 4 + 2] = static_cast<std::uint8_t>(255 - x * 4);
        data[x * 4 + 3] = 255;
    }
    D3D11_TEXTURE1D_DESC d{};
    d.Width = 64;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{data.data(), 256, 0};
    Com<ID3D11Texture1D> t;
    if (!r.ok(r.dev->CreateTexture1D(&d, &init, t.put()), "CreateTexture1D")) return false;
    Com<ID3D11ShaderResourceView> v = r.srv(t.get());
    if (!v) return false;
    return show(r, out, "Texture1D t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.Sample(s, i.uv.x) * (0.6 + 0.4 * i.uv.y); }\n", {v.get()});
});

const Add kTex1DArray(spec("tex", "dimension_texture1d_array").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Bytes data;
    for (int a = 0; a < 4; ++a) {
        for (int x = 0; x < 32; ++x) {
            data.insert(data.end(), {static_cast<std::uint8_t>(a * 60 + x * 2), static_cast<std::uint8_t>(255 - a * 70), static_cast<std::uint8_t>(x * 8), 255});
        }
    }
    D3D11_TEXTURE1D_DESC d{};
    d.Width = 32;
    d.MipLevels = 1;
    d.ArraySize = 4;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init[4];
    for (int a = 0; a < 4; ++a) init[a] = {data.data() + a * 128, 128, 0};
    Com<ID3D11Texture1D> t;
    if (!r.ok(r.dev->CreateTexture1D(&d, init, t.put()), "CreateTexture1D")) return false;
    Com<ID3D11ShaderResourceView> v = r.srv(t.get());
    if (!v) return false;
    return show(r, out, "Texture1DArray t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.Sample(s, float2(i.uv.x, floor(i.uv.y * 4.0))); }\n", {v.get()});
});

const Add kTex2DArray(spec("tex", "dimension_texture2d_array").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> t = r.tex_chain(32, 32, solid_rgba(32, 32, 1), DXGI_FORMAT_R8G8B8A8_UNORM, 1);
    std::vector<Com<ID3D11Texture2D>> keep;
    const unsigned levels = rr::Pattern::level_count(32, 32);
    std::vector<std::vector<Bytes>> chains;
    std::vector<D3D11_SUBRESOURCE_DATA> data;
    for (unsigned a = 0; a < 4; ++a) chains.push_back(rr::Pattern::mip_chain(solid_rgba(32, 32, a + 1), 32, 32, levels));
    for (unsigned a = 0; a < 4; ++a)
        for (unsigned l = 0; l < levels; ++l) data.push_back({chains[a][l].data(), std::max(1u, 32u >> l) * 4, 0});
    Com<ID3D11Texture2D> arr = r.tex(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, levels, 4, 1, data.data());
    if (!arr) return false;
    Com<ID3D11ShaderResourceView> v = r.srv(arr.get());
    if (!v) return false;
    return show(r, out, "Texture2DArray t : register(t0);\nfloat4 main(VO i) : SV_Target { float2 q = frac(i.uv * 2.0); return t.Sample(s, float3(q, floor(i.uv.x * 2.0) + 2.0 * floor(i.uv.y * 2.0))); }\n", {v.get()});
});

const Add kTex3D(spec("tex", "dimension_texture3d").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Bytes data(16 * 16 * 16 * 4);
    for (int z = 0; z < 16; ++z)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                std::uint8_t* p = &data[((static_cast<std::size_t>(z) * 16 + y) * 16 + x) * 4];
                p[0] = static_cast<std::uint8_t>(x * 16);
                p[1] = static_cast<std::uint8_t>(y * 16);
                p[2] = static_cast<std::uint8_t>(z * 16);
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
    if (!r.ok(r.dev->CreateTexture3D(&d, &init, t.put()), "CreateTexture3D")) return false;
    Com<ID3D11ShaderResourceView> v = r.srv(t.get());
    if (!v) return false;
    return show(r, out, "Texture3D t : register(t0);\nfloat4 main(VO i) : SV_Target { float w = (i.uv.x > 0.5) ? i.uv.y : 1.0 - i.uv.y; return (i.uv.y > 0.8) ? t.Load(int4(i.uv.x * 16, 3, 15 - i.uv.x * 16, 0)) : t.Sample(s, float3(i.uv, w)); }\n", {v.get()});
});

bool cube_data(Rig& r, UINT cubes, Com<ID3D11ShaderResourceView>& view, bool array) {
    const unsigned levels = 1;
    std::vector<Bytes> faces;
    std::vector<D3D11_SUBRESOURCE_DATA> data;
    for (UINT c = 0; c < cubes; ++c)
        for (unsigned f = 0; f < 6; ++f) faces.push_back(solid_rgba(16, 16, f * 7 + c * 3 + 1));
    for (auto& f : faces) data.push_back({f.data(), 64, 0});
    Com<ID3D11Texture2D> t = r.tex(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, levels, 6 * cubes, 1, data.data(), D3D11_RESOURCE_MISC_TEXTURECUBE);
    if (!t) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (array) {
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        sd.TextureCubeArray.MipLevels = 1;
        sd.TextureCubeArray.NumCubes = cubes;
    } else {
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        sd.TextureCube.MipLevels = 1;
    }
    view = r.srv(t.get(), &sd);
    return static_cast<bool>(view);
}

const char* const kDir =
    "float3 dir(float2 uv) { float a = uv.x * 6.2831853; float h = uv.y * 2.0 - 1.0; float q = sqrt(1.0 - h * h); return float3(cos(a) * q, h, sin(a) * q); }\n";

const Add kCube(spec("tex", "dimension_cube").tolerance(rr::Tolerance::filter(6, 0.01).with_edge(1)), [](Rig& r, Cap& out) {
    Com<ID3D11ShaderResourceView> v;
    if (!cube_data(r, 1, v, false)) return false;
    return show(r, out, std::string(kDir) + "TextureCube t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.Sample(s, dir(i.uv)); }\n", {v.get()});
});

const Add kCubeArray(spec("tex", "dimension_cube_array").tolerance(rr::Tolerance::filter(6, 0.01).with_edge(1)).variant(1), [](Rig& r, Cap& out) {
    Com<ID3D11ShaderResourceView> v;
    if (!cube_data(r, 2, v, true)) return false;
    return show(r, out, std::string(kDir) + "TextureCubeArray t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.Sample(s, float4(dir(i.uv), i.uv.x > 0.5 ? 1.0 : 0.0)); }\n", {v.get()});
}, [](Rig& r) { return need_fl(r, D3D_FEATURE_LEVEL_10_1); });

bool msaa_source(Rig& r, UINT samples, UINT array_size, Com<ID3D11ShaderResourceView>& view) {
    Com<ID3D11Texture2D> t = r.tex(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, array_size, samples);
    if (!t) return false;
    Com<ID3D11VertexShader> vs = r.vs(
        "float4 main(uint id : SV_VertexID) : SV_Position { float2 p[3] = {float2(-0.9, -0.8), float2(0.95, -0.3), float2(-0.2, 0.9)}; return float4(p[id], 0, 1); }\n");
    Com<ID3D11PixelShader> ps = r.ps("float4 main(float4 p : SV_Position) : SV_Target { return float4(p.x / 32.0, p.y / 32.0, 0.8, 1); }\n");
    if (!vs || !ps) return false;
    for (UINT slice = 0; slice < array_size; ++slice) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (array_size > 1) {
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
            rd.Texture2DMSArray.FirstArraySlice = slice;
            rd.Texture2DMSArray.ArraySize = 1;
        } else {
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        }
        Com<ID3D11RenderTargetView> rtv = r.rtv(t.get(), &rd);
        if (!rtv) return false;
        const float clear[4] = {0.1f, 0.1f * (slice + 1), 0.3f, 1};
        ID3D11RenderTargetView* views = rtv.get();
        r.ctx->OMSetRenderTargets(1, &views, nullptr);
        r.ctx->ClearRenderTargetView(views, clear);
        r.viewport(0, 0, 32, 32);
        r.ctx->IASetInputLayout(nullptr);
        r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.ctx->VSSetShader(vs.get(), nullptr, 0);
        r.ctx->PSSetShader(ps.get(), nullptr, 0);
        r.ctx->RSSetState(r.rast(rdesc()).get());
        r.ctx->Draw(3, 0);
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (array_size > 1) {
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
        sd.Texture2DMSArray.ArraySize = array_size;
    } else {
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    }
    view = r.srv(t.get(), &sd);
    return static_cast<bool>(view);
}

const Add kMsLoad(spec("tex", "dimension_texture2dms_load_per_sample").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Com<ID3D11ShaderResourceView> v;
    if (!msaa_source(r, 4, 1, v)) return false;
    return show(r, out, "Texture2DMS<float4, 4> t : register(t0);\nfloat4 main(VO i) : SV_Target { int2 c = int2(i.uv * 32.0); uint k = (uint(i.uv.x * 128.0) & 3); return t.Load(c, k); }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_POINT);
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

const Add kMsArrayLoad(spec("tex", "dimension_texture2dms_array_load").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Com<ID3D11ShaderResourceView> v;
    if (!msaa_source(r, 4, 3, v)) return false;
    return show(r, out, "Texture2DMSArray<float4, 4> t : register(t0);\nfloat4 main(VO i) : SV_Target { int2 c = int2(i.uv * 32.0); uint k = (uint(i.uv.x * 128.0) & 3); return t.Load(int3(c, uint(i.uv.y * 3.0)), k); }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_POINT);
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

const Add kMsDimensions(spec("tex", "dimension_texture2dms_get_dimensions").tolerance(rr::Tolerance::psnr(26)), [](Rig& r, Cap& out) {
    Com<ID3D11ShaderResourceView> v;
    if (!msaa_source(r, 4, 1, v)) return false;
    return show(r, out, "Texture2DMS<float4, 4> t : register(t0);\nfloat4 main(VO i) : SV_Target { uint w, h, n; t.GetDimensions(w, h, n); float4 a = t.Load(int2(i.uv * 32.0), 0); return float4(a.rgb * 0.5, 1) + float4(w / 64.0, h / 64.0, n / 8.0, 0) * 0.5; }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_POINT);
}, [](Rig& r) { return need_msaa(r, DXGI_FORMAT_R8G8B8A8_UNORM, 4); });

struct BufferRow {
    const char* name;
    int kind;
};

const Add kTypedBuffer(spec("tex", "buffer_srv_typed").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    std::vector<float> data(256 * 4);
    for (int i = 0; i < 256; ++i) {
        data[i * 4 + 0] = i / 255.0f;
        data[i * 4 + 1] = (i * 7 % 256) / 255.0f;
        data[i * 4 + 2] = 1.0f - i / 255.0f;
        data[i * 4 + 3] = 1.0f;
    }
    Com<ID3D11Buffer> b = r.buf(static_cast<UINT>(data.size() * 4), D3D11_BIND_SHADER_RESOURCE, data.data());
    if (!b) return false;
    Com<ID3D11ShaderResourceView> v = r.srv_buffer(b.get(), DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 256);
    if (!v) return false;
    return show(r, out, "Buffer<float4> t : register(t0);\nfloat4 main(VO i) : SV_Target { return t[uint(i.uv.x * 16.0) + 16 * uint(i.uv.y * 16.0)]; }\n", {v.get()});
});

const Add kTypedBufferNarrow(spec("tex", "buffer_srv_typed_r8g8b8a8").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Bytes data(256 * 4);
    for (int i = 0; i < 256; ++i) {
        data[i * 4 + 0] = static_cast<std::uint8_t>(i);
        data[i * 4 + 1] = static_cast<std::uint8_t>(i * 7);
        data[i * 4 + 2] = static_cast<std::uint8_t>(255 - i);
        data[i * 4 + 3] = 255;
    }
    Com<ID3D11Buffer> b = r.buf(1024, D3D11_BIND_SHADER_RESOURCE, data.data());
    if (!b) return false;
    Com<ID3D11ShaderResourceView> v = r.srv_buffer(b.get(), DXGI_FORMAT_R8G8B8A8_UNORM, 16, 240);
    if (!v) return false;
    return show(r, out, "Buffer<float4> t : register(t0);\nfloat4 main(VO i) : SV_Target { return t[uint(i.uv.x * 16.0) + 15 * uint(i.uv.y * 15.0)]; }\n", {v.get()});
});

const Add kStructuredBuffer(spec("tex", "buffer_srv_structured").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    struct Item {
        float color[3];
        float weight;
        UINT tag[2];
        float pad[2];
    };
    std::vector<Item> items(128);
    for (UINT i = 0; i < items.size(); ++i) {
        items[i] = {{i / 127.0f, (i * 5 % 128) / 127.0f, 0.5f}, 0.5f + 0.5f * (i % 3) / 2.0f, {i, i * 3}, {0, 0}};
    }
    Com<ID3D11Buffer> b = r.buf(static_cast<UINT>(items.size() * sizeof(Item)), D3D11_BIND_SHADER_RESOURCE, items.data(), D3D11_USAGE_DEFAULT, 0,
                                D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Item));
    if (!b) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.FirstElement = 0;
    sd.Buffer.NumElements = 128;
    Com<ID3D11ShaderResourceView> v = r.srv(b.get(), &sd);
    if (!v) return false;
    return show(r, out,
                "struct Item { float3 color; float weight; uint2 tag; float2 pad; };\nStructuredBuffer<Item> t : register(t0);\n"
                "float4 main(VO i) : SV_Target { Item it = t[uint(i.uv.x * 16.0) + 8 * uint(i.uv.y * 16.0) % 128]; return float4(it.color * it.weight, 1) + float4((it.tag.y & 3) * 0.1, 0, 0, 0); }\n",
                {v.get()});
});

const Add kRawBuffer(spec("tex", "buffer_srv_raw_byteaddress").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    std::vector<UINT> words(512);
    for (UINT i = 0; i < words.size(); ++i) words[i] = (i * 2654435761u) ^ (i << 8);
    Com<ID3D11Buffer> b = r.buf(2048, D3D11_BIND_SHADER_RESOURCE, words.data(), D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    if (!b) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_TYPELESS;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    sd.BufferEx.NumElements = 512;
    sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    Com<ID3D11ShaderResourceView> v = r.srv(b.get(), &sd);
    if (!v) return false;
    return show(r, out,
                "ByteAddressBuffer t : register(t0);\nfloat4 main(VO i) : SV_Target { uint idx = uint(i.uv.x * 16.0) + 16 * uint(i.uv.y * 16.0); uint4 q = t.Load4((idx * 8 % 508) * 4 & ~15u); return float4((q.x & 255) / 255.0, ((q.y >> 8) & 255) / 255.0, ((q.z >> 16) & 255) / 255.0, 1); }\n",
                {v.get()}, D3D11_FILTER_MIN_MAG_MIP_POINT);
});

Com<ID3D11Texture2D> mip_texture(Rig& r, UINT size, UINT array) {
    const unsigned levels = rr::Pattern::level_count(size, size);
    std::vector<std::vector<Bytes>> chains;
    std::vector<D3D11_SUBRESOURCE_DATA> data;
    for (UINT a = 0; a < array; ++a) {
        std::vector<Bytes> chain;
        for (unsigned l = 0; l < levels; ++l) chain.push_back(solid_rgba(std::max(1u, size >> l), std::max(1u, size >> l), l * 5 + a * 3 + 1));
        chains.push_back(std::move(chain));
    }
    for (UINT a = 0; a < array; ++a)
        for (unsigned l = 0; l < levels; ++l) data.push_back({chains[a][l].data(), std::max(1u, size >> l) * 4, 0});
    return r.tex(size, size, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, levels, array, 1, data.data());
}

const Add kViewMipRange(spec("tex", "view_srv_mip_subrange").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> t = mip_texture(r, 64, 1);
    if (!t) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 2;
    sd.Texture2D.MipLevels = 2;
    Com<ID3D11ShaderResourceView> v = r.srv(t.get(), &sd);
    if (!v) return false;
    return show(r, out, "Texture2D t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.SampleLevel(s, i.uv * 2.0, i.uv.y * 4.0 - 1.0); }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_LINEAR);
});

const Add kViewArraySlices(spec("tex", "view_srv_array_slice_subrange").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> t = mip_texture(r, 32, 6);
    if (!t) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sd.Texture2DArray.MostDetailedMip = 1;
    sd.Texture2DArray.MipLevels = 3;
    sd.Texture2DArray.FirstArraySlice = 2;
    sd.Texture2DArray.ArraySize = 3;
    Com<ID3D11ShaderResourceView> v = r.srv(t.get(), &sd);
    if (!v) return false;
    return show(r, out, "Texture2DArray t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.SampleLevel(s, float3(frac(i.uv * float2(3, 1)), floor(i.uv.x * 3.0)), i.uv.y * 2.0); }\n", {v.get()});
});

const Add kViewTypeless(spec("tex", "view_srv_typeless_reinterpret").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    const Bytes px = rr::Pattern::gradient(32, 32);
    D3D11_SUBRESOURCE_DATA init{px.data(), 128, 0};
    Com<ID3D11Texture2D> t = r.tex(32, 32, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_BIND_SHADER_RESOURCE, 1, 1, 1, &init);
    if (!t) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC a{};
    a.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    a.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    a.Texture2D.MipLevels = 1;
    D3D11_SHADER_RESOURCE_VIEW_DESC b = a;
    b.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    D3D11_SHADER_RESOURCE_VIEW_DESC c = a;
    c.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    Com<ID3D11ShaderResourceView> va = r.srv(t.get(), &a);
    Com<ID3D11ShaderResourceView> vb = r.srv(t.get(), &b);
    Com<ID3D11ShaderResourceView> vc = r.srv(t.get(), &c);
    if (!va || !vb || !vc) return false;
    return show(r, out,
                "Texture2D<float4> a : register(t0);\nTexture2D<float4> b : register(t1);\nTexture2D<uint4> c : register(t2);\n"
                "float4 main(VO i) : SV_Target { float3 u = a.Sample(s, i.uv).rgb; float3 g = b.Sample(s, i.uv).rgb; float3 n = c.Load(int3(i.uv * 32.0, 0)).rgb / 255.0;\n"
                "  return float4(i.uv.x < 0.33 ? u : (i.uv.x < 0.66 ? g : n), 1); }\n",
                {va.get(), vb.get(), vc.get()});
});

bool render_into(Rig& r, ID3D11RenderTargetView* rtv, UINT w, UINT h, float hue_shift) {
    Com<ID3D11PixelShader> ps = r.ps("cbuffer C : register(b0) { float4 k; };\nfloat4 main(float4 p : SV_Position) : SV_Target { return float4(frac(p.x / 16.0 + k.x), frac(p.y / 16.0), k.x, 1); }\n");
    Com<ID3D11Buffer> cb = r.buf(16, D3D11_BIND_CONSTANT_BUFFER);
    if (!ps || !cb) return false;
    const float k[4] = {hue_shift, 0, 0, 0};
    r.ctx->UpdateSubresource(cb.get(), 0, nullptr, k, 0, 0);
    ID3D11Buffer* b = cb.get();
    r.ctx->PSSetConstantBuffers(0, 1, &b);
    r.ctx->OMSetRenderTargets(1, &rtv, nullptr);
    const float clear[4] = {0, 0, 0, 1};
    r.ctx->ClearRenderTargetView(rtv, clear);
    r.viewport(0, 0, static_cast<float>(w), static_cast<float>(h));
    r.ctx->RSSetState(r.rast(rdesc()).get());
    r.draw_fs(ps.get());
    return true;
}

const Add kRtvMip(spec("tex", "view_rtv_mip_level").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> t = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 4);
    if (!t) return false;
    for (UINT m = 0; m < 4; ++m) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rd.Texture2D.MipSlice = m;
        Com<ID3D11RenderTargetView> rtv = r.rtv(t.get(), &rd);
        if (!rtv || !render_into(r, rtv.get(), 64 >> m, 64 >> m, 0.25f * m)) return false;
    }
    Com<ID3D11ShaderResourceView> v = r.srv(t.get());
    if (!v) return false;
    return show(r, out, "Texture2D t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.SampleLevel(s, i.uv, floor(i.uv.x * 4.0)); }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_POINT);
});

const Add kRtvSlice(spec("tex", "view_rtv_array_slice").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> t = r.tex(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, 4);
    if (!t) return false;
    for (UINT a = 0; a < 4; ++a) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rd.Texture2DArray.FirstArraySlice = a;
        rd.Texture2DArray.ArraySize = 1;
        Com<ID3D11RenderTargetView> rtv = r.rtv(t.get(), &rd);
        if (!rtv || !render_into(r, rtv.get(), 32, 32, 0.3f * a)) return false;
    }
    Com<ID3D11ShaderResourceView> v = r.srv(t.get());
    if (!v) return false;
    return show(r, out, "Texture2DArray t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.Sample(s, float3(frac(i.uv * 2.0), floor(i.uv.x * 2.0) + 2.0 * floor(i.uv.y * 2.0))); }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_POINT);
});

const Add kRtv3d(spec("tex", "view_rtv_3d_w_slice").tolerance(rr::Tolerance::blend(3)), [](Rig& r, Cap& out) {
    D3D11_TEXTURE3D_DESC d{};
    d.Width = d.Height = 32;
    d.Depth = 8;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture3D> t;
    if (!r.ok(r.dev->CreateTexture3D(&d, nullptr, t.put()), "CreateTexture3D")) return false;
    for (UINT w = 0; w < 8; ++w) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
        rd.Texture3D.FirstWSlice = w;
        rd.Texture3D.WSize = 1;
        Com<ID3D11RenderTargetView> rtv = r.rtv(t.get(), &rd);
        if (!rtv || !render_into(r, rtv.get(), 32, 32, 0.125f * w)) return false;
    }
    Com<ID3D11ShaderResourceView> v = r.srv(t.get());
    if (!v) return false;
    return show(r, out, "Texture3D t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.Sample(s, float3(i.uv.x, i.uv.y, 0.0625 + 0.875 * (i.uv.x * i.uv.y))); }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_LINEAR);
});

const Add kGenerateMips(spec("tex", "view_generate_mips").tolerance(rr::Tolerance::psnr(28)), [](Rig& r, Cap& out) {
    const unsigned levels = rr::Pattern::level_count(64, 64);
    Com<ID3D11Texture2D> t = r.tex(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, levels, 1, 1, nullptr, D3D11_RESOURCE_MISC_GENERATE_MIPS);
    if (!t) return false;
    Com<ID3D11ShaderResourceView> v = r.srv(t.get());
    Com<ID3D11RenderTargetView> rtv = r.rtv(t.get());
    if (!v || !rtv) return false;
    Com<ID3D11PixelShader> ps = r.ps("float4 main(float4 p : SV_Position) : SV_Target { float c = (uint(p.x / 4) + uint(p.y / 4)) & 1; return float4(c, 0.3 + 0.7 * (p.x / 64.0), 1.0 - c * 0.7, 1); }\n");
    if (!ps) return false;
    ID3D11RenderTargetView* views = rtv.get();
    r.ctx->OMSetRenderTargets(1, &views, nullptr);
    r.viewport(0, 0, 64, 64);
    r.ctx->RSSetState(r.rast(rdesc()).get());
    r.draw_fs(ps.get());
    r.ctx->GenerateMips(v.get());
    return show(r, out, "Texture2D t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.SampleLevel(s, i.uv, floor(i.uv.x * 5.0)); }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_POINT);
}, [](Rig& r) { return need_format(r, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_FORMAT_SUPPORT_MIP_AUTOGEN, "mip generation"); });

const Add kResourceMinLod(spec("tex", "view_set_resource_min_lod").tolerance(rr::Tolerance::psnr(28)), [](Rig& r, Cap& out) {
    Com<ID3D11Texture2D> t = mip_texture(r, 64, 1);
    if (!t) return false;
    Com<ID3D11ShaderResourceView> v = r.srv(t.get());
    if (!v) return false;
    r.ctx->SetResourceMinLOD(t.get(), 3.0f);
    return show(r, out, "Texture2D t : register(t0);\nfloat4 main(VO i) : SV_Target { return t.Sample(s, i.uv); }\n", {v.get()}, D3D11_FILTER_MIN_MAG_MIP_POINT);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
