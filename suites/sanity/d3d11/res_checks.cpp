#include "sanity/d3d11/res_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/dxgi_budget.hpp"
#include "support/format_codec.hpp"
#include "support/oracle_rig.hpp"
#include "support/pattern.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>

namespace lsw::suites::sanity::d3d11 {

namespace {

struct MatrixCase {
    int dim;
    D3D11_USAGE usage;
    UINT bind;
    UINT cpu;
    UINT misc;

    [[nodiscard]] std::string text() const {
        static const char* dims[] = {"buffer", "tex1d", "tex1d_array", "tex2d", "tex2d_array", "cube", "tex3d"};
        static const char* usages[] = {"default", "immutable", "dynamic", "staging"};
        return std::string(dims[dim]) + " " + usages[usage] + " bind=0x" + Text::hex(bind, 3) + " cpu=0x" +
               Text::hex(cpu, 2) + " misc=0x" + Text::hex(misc, 4);
    }
};

HRESULT create_case(D3d11Device& device, const MatrixCase& c) {
    static std::vector<std::uint8_t> pool(1 << 16, 0x11);
    const DXGI_FORMAT format = (c.bind & D3D11_BIND_DEPTH_STENCIL) != 0 ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
    const bool immutable = c.usage == D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA data[8];
    for (auto& entry : data) {
        entry.pSysMem = pool.data();
        entry.SysMemPitch = 16 * 4;
        entry.SysMemSlicePitch = 16 * 16 * 4;
    }
    const UINT mips = (c.misc & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0 ? 0 : 1;
    switch (c.dim) {
    case 0: {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 256;
        desc.Usage = c.usage;
        desc.BindFlags = c.bind;
        desc.CPUAccessFlags = c.cpu;
        desc.MiscFlags = c.misc;
        desc.StructureByteStride = (c.misc & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0 ? 16 : 0;
        Com<ID3D11Buffer> out;
        return device.device->CreateBuffer(&desc, immutable ? data : nullptr, out.put());
    }
    case 1:
    case 2: {
        D3D11_TEXTURE1D_DESC desc{};
        desc.Width = 16;
        desc.MipLevels = mips;
        desc.ArraySize = c.dim == 2 ? 4 : 1;
        desc.Format = format;
        desc.Usage = c.usage;
        desc.BindFlags = c.bind;
        desc.CPUAccessFlags = c.cpu;
        desc.MiscFlags = c.misc;
        Com<ID3D11Texture1D> out;
        return device.device->CreateTexture1D(&desc, immutable ? data : nullptr, out.put());
    }
    case 3:
    case 4:
    case 5: {
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(16, 16, format, c.bind, c.usage, c.cpu, mips, c.dim == 3 ? 1 : (c.dim == 4 ? 4 : 6));
        desc.MiscFlags = c.misc | (c.dim == 5 ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0);
        Com<ID3D11Texture2D> out;
        return device.device->CreateTexture2D(&desc, immutable ? data : nullptr, out.put());
    }
    default: {
        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = 16;
        desc.Height = 16;
        desc.Depth = 4;
        desc.MipLevels = mips;
        desc.Format = format;
        desc.Usage = c.usage;
        desc.BindFlags = c.bind;
        desc.CPUAccessFlags = c.cpu;
        desc.MiscFlags = c.misc;
        Com<ID3D11Texture3D> out;
        return device.device->CreateTexture3D(&desc, immutable ? data : nullptr, out.put());
    }
    }
}

std::string describe_pair(const std::map<std::string, bool>& av, const std::map<std::string, bool>& warp, int& total) {
    std::string text;
    int differing = 0;
    for (const auto& [key, accepted] : av) {
        auto other = warp.find(key);
        if (other != warp.end() && other->second != accepted) {
            if (++differing <= 12) {
                text += "[" + key + ": accel " + (accepted ? "accepts" : "rejects") + ", WARP " + (accepted ? "rejects" : "accepts") + "] ";
            }
        }
    }
    total = differing;
    return text;
}

}  // namespace

Outcome ResChecks::usage_bind_cpu_matrix(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    const D3D11_USAGE usages[] = {D3D11_USAGE_DEFAULT, D3D11_USAGE_IMMUTABLE, D3D11_USAGE_DYNAMIC, D3D11_USAGE_STAGING};
    const UINT binds[] = {0,
                          D3D11_BIND_VERTEX_BUFFER,
                          D3D11_BIND_INDEX_BUFFER,
                          D3D11_BIND_CONSTANT_BUFFER,
                          D3D11_BIND_SHADER_RESOURCE,
                          D3D11_BIND_STREAM_OUTPUT,
                          D3D11_BIND_RENDER_TARGET,
                          D3D11_BIND_DEPTH_STENCIL,
                          D3D11_BIND_UNORDERED_ACCESS,
                          D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                          D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
                          D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                          D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS};
    const UINT cpus[] = {0, D3D11_CPU_ACCESS_WRITE, D3D11_CPU_ACCESS_READ, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ};
    const UINT buffer_misc[] = {0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
                                D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, D3D11_RESOURCE_MISC_SHARED};
    const UINT texture_misc[] = {0, D3D11_RESOURCE_MISC_GENERATE_MIPS, D3D11_RESOURCE_MISC_SHARED, D3D11_RESOURCE_MISC_RESOURCE_CLAMP,
                                 D3D11_RESOURCE_MISC_GDI_COMPATIBLE};
    std::map<std::string, bool> av;
    std::map<std::string, bool> warp;
    int accepted_av = 0;
    for (int dim = 0; dim < 7; ++dim) {
        for (D3D11_USAGE usage : usages) {
            for (UINT bind : binds) {
                for (UINT cpu : cpus) {
                    for (UINT misc : (dim == 0 ? buffer_misc : texture_misc)) {
                        const MatrixCase c{dim, usage, bind, cpu, misc};
                        const bool a = SUCCEEDED(create_case(rig.av, c));
                        const bool w = SUCCEEDED(create_case(rig.warp, c));
                        av[c.text()] = a;
                        warp[c.text()] = w;
                        accepted_av += a ? 1 : 0;
                    }
                }
            }
        }
    }
    int differing = 0;
    const std::string text = describe_pair(av, warp, differing);
    context.measure("combinations", static_cast<long long>(av.size()));
    context.measure("accepted", accepted_av);
    context.measure("differing", differing);
    Problems problems;
    if (differing > 0) {
        problems.add(std::to_string(differing) + " of " + std::to_string(av.size()) + " combinations differ from WARP: " + text);
    }
    if (accepted_av < 50) {
        problems.add("only " + std::to_string(accepted_av) + " combinations were accepted; the matrix did not run");
    }
    return problems.outcome();
}

Outcome ResChecks::immutable_initial(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    constexpr UINT kWords = 1024;
    std::vector<std::uint32_t> words(kWords);
    for (UINT i = 0; i < kWords; ++i) {
        words[i] = static_cast<std::uint32_t>(Pattern::pixel(77, i, 3));
    }
    Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, kWords * 4, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE,
                                                  0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, words.data());
    if (!buffer) {
        return Outcome::fail("an immutable raw buffer with initial data cannot be created");
    }
    {
        HRESULT result = S_OK;
        Com<ID3D11Buffer> none = D3d11Tools::buffer(device, kWords * 4, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr, &result);
        if (none || result != E_INVALIDARG) {
            problems.add("an immutable buffer without initial data returned " + D3d11Tools::hr(result));
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 256;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA empty{};
        Com<ID3D11Buffer> hollow;
        result = device.device->CreateBuffer(&desc, &empty, hollow.put());
        if (hollow || result != E_INVALIDARG) {
            problems.add("an immutable buffer with a null initial pointer returned " + D3d11Tools::hr(result));
        }
        D3D11_TEXTURE2D_DESC td = D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R32_UINT, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);
        Com<ID3D11Texture2D> texture;
        result = device.device->CreateTexture2D(&td, nullptr, texture.put());
        if (texture || result != E_INVALIDARG) {
            problems.add("an immutable texture without initial data returned " + D3d11Tools::hr(result));
        }
        result = device.device->CreateTexture2D(&td, &empty, texture.put());
        if (texture || result != E_INVALIDARG) {
            problems.add("an immutable texture with a null initial pointer returned " + D3d11Tools::hr(result));
        }
    }
    {
        D3D11_TEXTURE2D_DESC td = D3d11Tools::desc2d(32, 32, DXGI_FORMAT_R32_UINT, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = words.data();
        data.SysMemPitch = 32 * 4;
        Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, td, &data);
        if (!texture) {
            return Outcome::fail("an immutable texture with initial data cannot be created");
        }
        const char* source =
            "ByteAddressBuffer raw : register(t0); Texture2D<uint> tex : register(t1);\n"
            "RWByteAddressBuffer o : register(u0);\n"
            "[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
            "  o.Store(id.x * 4, raw.Load(id.x * 4));\n"
            "  o.Store((1024 + id.x) * 4, tex.Load(int3(id.x % 32, id.x / 32, 0)));\n"
            "}\n";
        Com<ID3D11ComputeShader> compute = D3d11Tools::cs(device, source, error);
        if (!compute) {
            return Outcome::fail(error);
        }
        Com<ID3D11Buffer> out = D3d11Tools::buffer(device, 2 * kWords * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS,
                                                   0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr);
        Com<ID3D11ShaderResourceView> raw_view = D3d11Tools::raw_srv(device, buffer.get());
        Com<ID3D11ShaderResourceView> tex_view = D3d11Tools::srv(device, texture.get());
        Com<ID3D11UnorderedAccessView> uav = out ? D3d11Tools::raw_uav(device, out.get()) : Com<ID3D11UnorderedAccessView>();
        if (!out || !raw_view || !tex_view || !uav) {
            return Outcome::fail("views over the immutable resources cannot be created");
        }
        ID3D11ShaderResourceView* views[2] = {raw_view.get(), tex_view.get()};
        ID3D11UnorderedAccessView* uavs[1] = {uav.get()};
        device.context->CSSetShader(compute.get(), nullptr, 0);
        device.context->CSSetShaderResources(0, 2, views);
        device.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        device.context->Dispatch(kWords / 64, 1, 1);
        const std::vector<std::uint32_t> shaded = D3d11Tools::read_words(device, out.get(), error);
        if (shaded.size() != 2 * kWords) {
            problems.add("shader readback: " + error);
        } else {
            for (UINT i = 0; i < kWords; ++i) {
                if (shaded[i] != words[i] || shaded[kWords + i] != words[i]) {
                    problems.add("shader read of the immutable data differs at word " + std::to_string(i));
                    break;
                }
            }
        }
        std::vector<std::uint8_t> copied;
        if (!D3d11Tools::read_buffer(device, buffer.get(), copied, error) || copied.size() != kWords * 4 ||
            std::memcmp(copied.data(), words.data(), copied.size()) != 0) {
            problems.add("a copy of the immutable buffer differs from the initial data " + error);
        }
        TexelImage image;
        if (!D3d11Tools::read_texture(device, texture.get(), 0, image, error) || image.data.size() != 32 * 32 * 4 ||
            std::memcmp(image.data.data(), words.data(), image.data.size()) != 0) {
            problems.add("a copy of the immutable texture differs from the initial data " + error);
        }
        D3d11Tools::reset_state(device);
    }
    return problems.outcome();
}

Outcome ResChecks::views_matrix(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    struct Case {
        std::string name;
        std::function<HRESULT(D3d11Device&)> run;
    };
    std::vector<Case> cases;
    auto tex = [](D3d11Device& d, DXGI_FORMAT f, UINT bind, UINT array_size = 1, UINT mips = 1, UINT samples = 1, UINT misc = 0) {
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(16, 16, f, bind, D3D11_USAGE_DEFAULT, 0, mips, array_size, samples);
        desc.MiscFlags = misc;
        return D3d11Tools::texture(d, desc);
    };
    auto srv = [](D3d11Device& d, ID3D11Resource* r, DXGI_FORMAT f, D3D11_SRV_DIMENSION dim, auto fill) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = f;
        desc.ViewDimension = dim;
        fill(desc);
        if (r == nullptr) {
            return E_INVALIDARG;
        }
        Com<ID3D11ShaderResourceView> view;
        return d.device->CreateShaderResourceView(r, &desc, view.put());
    };
    auto rtv = [](D3d11Device& d, ID3D11Resource* r, DXGI_FORMAT f, D3D11_RTV_DIMENSION dim, auto fill) {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        desc.Format = f;
        desc.ViewDimension = dim;
        fill(desc);
        if (r == nullptr) {
            return E_INVALIDARG;
        }
        Com<ID3D11RenderTargetView> view;
        return d.device->CreateRenderTargetView(r, &desc, view.put());
    };
    auto dsv = [](D3d11Device& d, ID3D11Resource* r, DXGI_FORMAT f, D3D11_DSV_DIMENSION dim, UINT flags, auto fill) {
        D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
        desc.Format = f;
        desc.ViewDimension = dim;
        desc.Flags = flags;
        fill(desc);
        if (r == nullptr) {
            return E_INVALIDARG;
        }
        Com<ID3D11DepthStencilView> view;
        return d.device->CreateDepthStencilView(r, &desc, view.put());
    };
    auto uav = [](D3d11Device& d, ID3D11Resource* r, DXGI_FORMAT f, D3D11_UAV_DIMENSION dim, auto fill) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = f;
        desc.ViewDimension = dim;
        fill(desc);
        if (r == nullptr) {
            return E_INVALIDARG;
        }
        Com<ID3D11UnorderedAccessView> view;
        return d.device->CreateUnorderedAccessView(r, &desc, view.put());
    };
    auto none = [](auto&) {};
    constexpr UINT kSrvRt = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    constexpr UINT kSrvUav = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UINT,
                                   DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
    for (DXGI_FORMAT f : formats) {
        const std::string n = std::to_string(f);
        cases.push_back({"typeless rgba8 srv as " + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_TYPELESS, kSrvRt); return srv(d, t.get(), f, D3D11_SRV_DIMENSION_TEXTURE2D, [](auto& v) { v.Texture2D.MipLevels = 1; }); }});
        cases.push_back({"typeless rgba8 rtv as " + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_TYPELESS, kSrvRt); return rtv(d, t.get(), f, D3D11_RTV_DIMENSION_TEXTURE2D, [](auto&) {}); }});
        cases.push_back({"typed rgba8 srv as " + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt); return srv(d, t.get(), f, D3D11_SRV_DIMENSION_TEXTURE2D, [](auto& v) { v.Texture2D.MipLevels = 1; }); }});
        cases.push_back({"typeless r32 uav as " + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R32_TYPELESS, kSrvUav); return uav(d, t.get(), f, D3D11_UAV_DIMENSION_TEXTURE2D, [](auto&) {}); }});
    }
    cases.push_back({"srv without bind", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET); return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2D, [](auto& v) { v.Texture2D.MipLevels = 1; }); }});
    cases.push_back({"rtv without bind", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE); return rtv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2D, [](auto&) {}); }});
    for (UINT first : {0u, 1u, 2u, 3u}) {
        for (UINT count : {1u, 2u, 4u}) {
            const std::string n = " slices " + std::to_string(first) + "+" + std::to_string(count);
            cases.push_back({"array srv" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 4); return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2DARRAY, [=](auto& v) { v.Texture2DArray.MipLevels = 1; v.Texture2DArray.FirstArraySlice = first; v.Texture2DArray.ArraySize = count; }); }});
            cases.push_back({"array rtv" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 4); return rtv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2DARRAY, [=](auto& v) { v.Texture2DArray.FirstArraySlice = first; v.Texture2DArray.ArraySize = count; }); }});
            cases.push_back({"array uav" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R32_UINT, kSrvUav, 4); return uav(d, t.get(), DXGI_FORMAT_R32_UINT, D3D11_UAV_DIMENSION_TEXTURE2DARRAY, [=](auto& v) { v.Texture2DArray.FirstArraySlice = first; v.Texture2DArray.ArraySize = count; }); }});
            cases.push_back({"array dsv" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL, 4); return dsv(d, t.get(), DXGI_FORMAT_D32_FLOAT, D3D11_DSV_DIMENSION_TEXTURE2DARRAY, 0, [=](auto& v) { v.Texture2DArray.FirstArraySlice = first; v.Texture2DArray.ArraySize = count; }); }});
        }
    }
    for (UINT mip : {0u, 1u, 3u, 4u, 5u}) {
        const std::string n = " mip " + std::to_string(mip);
        cases.push_back({"mip srv" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 1, 5); return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2D, [=](auto& v) { v.Texture2D.MostDetailedMip = mip; v.Texture2D.MipLevels = 1; }); }});
        cases.push_back({"mip rtv" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 1, 5); return rtv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2D, [=](auto& v) { v.Texture2D.MipSlice = mip; }); }});
        cases.push_back({"mip uav" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R32_UINT, kSrvUav, 1, 5); return uav(d, t.get(), DXGI_FORMAT_R32_UINT, D3D11_UAV_DIMENSION_TEXTURE2D, [=](auto& v) { v.Texture2D.MipSlice = mip; }); }});
    }
    for (UINT samples : {2u, 4u, 8u}) {
        const std::string n = " x" + std::to_string(samples);
        cases.push_back({"msaa srv ms" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 1, 1, samples); return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2DMS, none); }});
        cases.push_back({"msaa srv 2d" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 1, 1, samples); return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2D, [](auto& v) { v.Texture2D.MipLevels = 1; }); }});
        cases.push_back({"msaa rtv ms" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 1, 1, samples); return rtv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2DMS, none); }});
        cases.push_back({"msaa rtv 2d" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 1, 1, samples); return rtv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2D, [](auto&) {}); }});
        cases.push_back({"msaa dsv ms" + n, [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL, 1, 1, samples); return dsv(d, t.get(), DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_DSV_DIMENSION_TEXTURE2DMS, 0, none); }});
    }
    cases.push_back({"dsv read-only depth", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL); return dsv(d, t.get(), DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_DSV_DIMENSION_TEXTURE2D, D3D11_DSV_READ_ONLY_DEPTH, [](auto&) {}); }});
    cases.push_back({"dsv read-only both", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL); return dsv(d, t.get(), DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_DSV_DIMENSION_TEXTURE2D, D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL, [](auto&) {}); }});
    cases.push_back({"dsv from typeless", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R24G8_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE); return dsv(d, t.get(), DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_DSV_DIMENSION_TEXTURE2D, 0, [](auto&) {}); }});
    cases.push_back({"srv depth of typeless", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R24G8_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE); return srv(d, t.get(), DXGI_FORMAT_R24_UNORM_X8_TYPELESS, D3D11_SRV_DIMENSION_TEXTURE2D, [](auto& v) { v.Texture2D.MipLevels = 1; }); }});
    cases.push_back({"srv stencil of typeless", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R24G8_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE); return srv(d, t.get(), DXGI_FORMAT_X24_TYPELESS_G8_UINT, D3D11_SRV_DIMENSION_TEXTURE2D, [](auto& v) { v.Texture2D.MipLevels = 1; }); }});
    cases.push_back({"cube srv cube", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 6, 1, 1, D3D11_RESOURCE_MISC_TEXTURECUBE); return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURECUBE, [](auto& v) { v.TextureCube.MipLevels = 1; }); }});
    cases.push_back({"cube srv cube array", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 12, 1, 1, D3D11_RESOURCE_MISC_TEXTURECUBE); return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURECUBEARRAY, [](auto& v) { v.TextureCubeArray.MipLevels = 1; v.TextureCubeArray.NumCubes = 2; }); }});
    cases.push_back({"cube rtv face", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 6, 1, 1, D3D11_RESOURCE_MISC_TEXTURECUBE); return rtv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2DARRAY, [](auto& v) { v.Texture2DArray.FirstArraySlice = 2; v.Texture2DArray.ArraySize = 1; }); }});
    cases.push_back({"cube srv on non-cube", [=](D3d11Device& d) { auto t = tex(d, DXGI_FORMAT_R8G8B8A8_UNORM, kSrvRt, 6); return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURECUBE, [](auto& v) { v.TextureCube.MipLevels = 1; }); }});
    for (UINT w : {1u, 2u, 4u, 5u}) {
        auto volume = [](D3d11Device& d, UINT bind) {
            D3D11_TEXTURE3D_DESC desc{};
            desc.Width = desc.Height = 16;
            desc.Depth = 4;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = bind;
            Com<ID3D11Texture3D> out;
            d.device->CreateTexture3D(&desc, nullptr, out.put());
            return out;
        };
        cases.push_back({"3d rtv wsize " + std::to_string(w), [=](D3d11Device& d) { auto t = volume(d, kSrvRt); return rtv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE3D, [=](auto& v) { v.Texture3D.WSize = w; }); }});
        cases.push_back({"3d uav wsize " + std::to_string(w), [=](D3d11Device& d) { auto t = volume(d, kSrvUav); return uav(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_UAV_DIMENSION_TEXTURE3D, [=](auto& v) { v.Texture3D.WSize = w; }); }});
    }
    cases.push_back({"3d srv", [=](D3d11Device& d) {
        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = desc.Height = 16;
        desc.Depth = 4;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture3D> t;
        d.device->CreateTexture3D(&desc, nullptr, t.put());
        return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE3D, [](auto& v) { v.Texture3D.MipLevels = 1; });
    }});
    struct BufferKind {
        const char* name;
        UINT misc;
        UINT stride;
        UINT bind;
    };
    const BufferKind kinds[] = {{"plain", 0, 0, kSrvUav},
                                {"raw", D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, kSrvUav},
                                {"structured", D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16, kSrvUav}};
    for (const BufferKind& k : kinds) {
        auto make = [k](D3d11Device& d) { return D3d11Tools::buffer(d, 256, D3D11_USAGE_DEFAULT, k.bind, 0, k.misc, k.stride, nullptr); };
        const std::string n = std::string("buffer ") + k.name;
        cases.push_back({n + " srv typed r32", [=](D3d11Device& d) { auto b = make(d); return srv(d, b.get(), DXGI_FORMAT_R32_UINT, D3D11_SRV_DIMENSION_BUFFER, [](auto& v) { v.Buffer.NumElements = 16; }); }});
        cases.push_back({"" + n + " srv raw", [=](D3d11Device& d) { auto b = make(d); return srv(d, b.get(), DXGI_FORMAT_R32_TYPELESS, D3D11_SRV_DIMENSION_BUFFEREX, [](auto& v) { v.BufferEx.NumElements = 64; v.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW; }); }});
        cases.push_back({n + " srv structured", [=](D3d11Device& d) { auto b = make(d); return srv(d, b.get(), DXGI_FORMAT_UNKNOWN, D3D11_SRV_DIMENSION_BUFFER, [](auto& v) { v.Buffer.NumElements = 16; }); }});
        cases.push_back({n + " srv first 8 count 8", [=](D3d11Device& d) { auto b = make(d); return srv(d, b.get(), DXGI_FORMAT_R32_UINT, D3D11_SRV_DIMENSION_BUFFER, [](auto& v) { v.Buffer.FirstElement = 8; v.Buffer.NumElements = 8; }); }});
        cases.push_back({n + " srv out of range", [=](D3d11Device& d) { auto b = make(d); return srv(d, b.get(), DXGI_FORMAT_R32_UINT, D3D11_SRV_DIMENSION_BUFFER, [](auto& v) { v.Buffer.FirstElement = 60; v.Buffer.NumElements = 8; }); }});
        for (UINT flags : {0u, (UINT)D3D11_BUFFER_UAV_FLAG_RAW, (UINT)D3D11_BUFFER_UAV_FLAG_APPEND, (UINT)D3D11_BUFFER_UAV_FLAG_COUNTER}) {
            for (DXGI_FORMAT f : {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_UINT}) {
                cases.push_back({n + " uav flags " + std::to_string(flags) + " fmt " + std::to_string(f), [=](D3d11Device& d) { auto b = make(d); return uav(d, b.get(), f, D3D11_UAV_DIMENSION_BUFFER, [=](auto& v) { v.Buffer.NumElements = 16; v.Buffer.Flags = flags; }); }});
            }
        }
    }
    cases.push_back({"1d srv", [=](D3d11Device& d) {
        D3D11_TEXTURE1D_DESC desc{};
        desc.Width = 16;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        Com<ID3D11Texture1D> t;
        d.device->CreateTexture1D(&desc, nullptr, t.put());
        return srv(d, t.get(), DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE1D, [](auto& v) { v.Texture1D.MipLevels = 1; });
    }});
    int differing = 0;
    std::string text;
    int accepted = 0;
    for (const Case& c : cases) {
        const bool a = SUCCEEDED(c.run(rig.av));
        const bool w = SUCCEEDED(c.run(rig.warp));
        accepted += a ? 1 : 0;
        if (a != w && ++differing <= 15) {
            text += "[" + c.name + ": accel " + (a ? "accepts" : "rejects") + ", WARP " + (w ? "accepts" : "rejects") + "] ";
        }
    }
    context.measure("cases", static_cast<long long>(cases.size()));
    context.measure("accepted", accepted);
    Problems problems;
    if (differing > 0) {
        problems.add(std::to_string(differing) + " of " + std::to_string(cases.size()) + " view cases differ from WARP: " + text);
    }
    D3d11Device& device = rig.av;
    const UINT raw_misc = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    Com<ID3D11Buffer> source = D3d11Tools::buffer(device, 64 * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, raw_misc, 0, nullptr);
    std::vector<std::uint32_t> input(64);
    for (UINT i = 0; i < 64; ++i) {
        input[i] = i * 3 + 1;
    }
    Com<ID3D11Buffer> target = D3d11Tools::buffer(device, 64 * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, raw_misc, 0, nullptr);
    Com<ID3D11ShaderResourceView> sub_srv;
    if (source) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_TYPELESS;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        sd.BufferEx.FirstElement = 8;
        sd.BufferEx.NumElements = 16;
        sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        device.device->CreateShaderResourceView(source.get(), &sd, sub_srv.put());
    }
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.FirstElement = 16;
    ud.Buffer.NumElements = 16;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    Com<ID3D11UnorderedAccessView> sub_uav;
    if (target) {
        device.device->CreateUnorderedAccessView(target.get(), &ud, sub_uav.put());
    }
    Com<ID3D11ComputeShader> compute = D3d11Tools::cs(device,
        "ByteAddressBuffer src : register(t0); RWByteAddressBuffer dst : register(u0);\n"
        "[numthreads(16,1,1)] void main(uint3 id : SV_DispatchThreadID) { uint v = src.Load(id.x * 4); dst.Store(id.x * 4, v + 1000); }\n", error);
    if (!sub_srv || !sub_uav || !compute) {
        problems.add("the sub-range views or shader cannot be created: " + error);
        return problems.outcome();
    }
    device.context->UpdateSubresource(source.get(), 0, nullptr, input.data(), 0, 0);
    const std::vector<std::uint32_t> zeros(64, 0xEEEEEEEE);
    device.context->UpdateSubresource(target.get(), 0, nullptr, zeros.data(), 0, 0);
    ID3D11ShaderResourceView* sv = sub_srv.get();
    ID3D11UnorderedAccessView* uavs[1] = {sub_uav.get()};
    device.context->CSSetShader(compute.get(), nullptr, 0);
    device.context->CSSetShaderResources(0, 1, &sv);
    device.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    device.context->Dispatch(1, 1, 1);
    const std::vector<std::uint32_t> written = D3d11Tools::read_words(device, target.get(), error);
    if (written.size() != 64) {
        problems.add("sub-range readback failed: " + error);
    } else {
        for (UINT i = 0; i < 64; ++i) {
            const std::uint32_t want = (i >= 16 && i < 32) ? input[8 + (i - 16)] + 1000 : 0xEEEEEEEE;
            if (written[i] != want) {
                problems.add("a sub-range unordered access view wrote element " + std::to_string(i) + " as " + std::to_string(written[i]));
                break;
            }
        }
    }
    {
        Com<ID3D11Texture2D> mipped = D3d11Tools::texture(device, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 3));
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rd.Texture2D.MipSlice = 1;
        Com<ID3D11RenderTargetView> view;
        if (!mipped || FAILED(device.device->CreateRenderTargetView(mipped.get(), &rd, view.put()))) {
            problems.add("a mip slice render target view cannot be created");
        } else {
            for (UINT m = 0; m < 3; ++m) {
                D3D11_RENDER_TARGET_VIEW_DESC all = rd;
                all.Texture2D.MipSlice = m;
                Com<ID3D11RenderTargetView> clear_view;
                device.device->CreateRenderTargetView(mipped.get(), &all, clear_view.put());
                D3d11Tools::clear(device, clear_view.get(), 0, 0, 0, 0);
            }
            D3d11Tools::clear(device, view.get(), 1, 0, 0, 1);
            for (UINT m = 0; m < 3; ++m) {
                TexelImage image;
                if (!D3d11Tools::read_texture(device, mipped.get(), m, image, error)) {
                    problems.add(error);
                    break;
                }
                const std::uint32_t want = m == 1 ? 0xFF0000FFu : 0u;
                for (UINT p = 0; p < image.width * image.height; ++p) {
                    if (image.pixel(p % image.width, p / image.width) != want) {
                        problems.add("mip " + std::to_string(m) + " holds the wrong data after a clear of mip 1 only");
                        break;
                    }
                }
            }
        }
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome ResChecks::sizes_extremes(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    ID3D11Device* d = device.device;
    Problems problems;
    const bool fl11 = device.level >= D3D_FEATURE_LEVEL_11_0;
    const UINT max_dimension = fl11 ? 16384 : (device.level >= D3D_FEATURE_LEVEL_10_0 ? 8192 : 4096);
    auto expect = [&](const std::string& what, HRESULT result, bool may_oom) {
        const bool good = SUCCEEDED(result) || (may_oom && result == E_OUTOFMEMORY);
        if (!good) {
            problems.add(what + " returned " + D3d11Tools::hr(result));
        }
    };
    auto refuse = [&](const std::string& what, HRESULT result) {
        if (result != E_INVALIDARG && result != E_OUTOFMEMORY) {
            problems.add(what + " returned " + D3d11Tools::hr(result) + " instead of E_INVALIDARG or E_OUTOFMEMORY");
        }
    };
    for (UINT size : {1u, 2u, 3u, 5u, 7u, 17u, 33u, 255u, 257u, 1023u}) {
        for (UINT h : {1u, 3u, 129u}) {
            HRESULT result = S_OK;
            Com<ID3D11Texture2D> t = D3d11Tools::texture(device, D3d11Tools::desc2d(size, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET), nullptr, &result);
            expect("a " + std::to_string(size) + "x" + std::to_string(h) + " texture", result, false);
        }
    }
    {
        HRESULT result = S_OK;
        Com<ID3D11Texture2D> t = D3d11Tools::texture(device, D3d11Tools::desc2d(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET), nullptr, &result);
        expect("a 1x1 texture", result, false);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, t.get());
        if (view) {
            D3d11Tools::clear(device, view.get(), 1, 0, 0, 1);
            std::vector<std::uint8_t> px;
            if (!D3d11Tools::read_pixels(device, t.get(), px, error) || px.size() != 4 || px[0] != 255 || px[1] != 0) {
                problems.add("a 1x1 target does not render");
            }
        }
    }
    {
        HRESULT result = S_OK;
        const D3D11_TEXTURE2D_DESC zero_width = D3d11Tools::desc2d(0, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
        Com<ID3D11Texture2D> t = D3d11Tools::texture(device, zero_width, nullptr, &result);
        if (t || result != E_INVALIDARG) {
            problems.add("a zero-width texture returned " + D3d11Tools::hr(result));
        }
        const D3D11_TEXTURE2D_DESC zero_height = D3d11Tools::desc2d(16, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
        t = D3d11Tools::texture(device, zero_height, nullptr, &result);
        if (t || result != E_INVALIDARG) {
            problems.add("a zero-height texture returned " + D3d11Tools::hr(result));
        }
        Com<ID3D11Buffer> b = D3d11Tools::buffer(device, 0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr, &result);
        if (b || result != E_INVALIDARG) {
            problems.add("a zero-sized buffer returned " + D3d11Tools::hr(result));
        }
        D3D11_TEXTURE3D_DESC vd{};
        vd.Width = 16;
        vd.Height = 16;
        vd.Depth = 0;
        vd.MipLevels = 1;
        vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        vd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture3D> volume;
        result = d->CreateTexture3D(&vd, nullptr, volume.put());
        if (volume || result != E_INVALIDARG) {
            problems.add("a zero-depth volume returned " + D3d11Tools::hr(result));
        }
    }
    {
        HRESULT result = S_OK;
        Com<ID3D11Texture2D> wide = D3d11Tools::texture(device, D3d11Tools::desc2d(max_dimension, 1, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
        expect("a maximum-width texture", result, true);
        Com<ID3D11Texture2D> tall = D3d11Tools::texture(device, D3d11Tools::desc2d(1, max_dimension, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
        expect("a maximum-height texture", result, true);
        Com<ID3D11Texture2D> square = D3d11Tools::texture(device, D3d11Tools::desc2d(4096, 4096, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET), nullptr, &result);
        expect("a 4096x4096 render target", result, true);
        Com<ID3D11Texture2D> beyond = D3d11Tools::texture(device, D3d11Tools::desc2d(max_dimension + 1, 1, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
        refuse("a texture beyond the maximum", result);
        if (beyond) {
            problems.add("a texture beyond the maximum dimension was created");
        }
        Com<ID3D11Texture2D> huge = D3d11Tools::texture(device, D3d11Tools::desc2d(max_dimension, max_dimension, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
        if (huge) {
            context.note("a 16384x16384 float4 texture (4 GiB) was created");
        } else {
            refuse("a 4 GiB texture", result);
        }
        huge.reset();
        Com<ID3D11Buffer> big = D3d11Tools::buffer(device, 0xFFFFFFF0u, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr, &result);
        if (!big) {
            refuse("a buffer of almost 4 GiB", result);
        }
        big.reset();
        Com<ID3D11Buffer> limit = D3d11Tools::buffer(device, 128u << 20, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr, &result);
        expect("a 128 MiB buffer", result, true);
    }
    {
        Com<ID3D11Texture2D> t = D3d11Tools::target(device, 64, 64);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, t.get());
        if (!view) {
            problems.add("the device cannot create a render target after the extreme requests");
        } else {
            D3d11Tools::clear(device, view.get(), 0, 1, 0, 1);
            std::vector<std::uint8_t> px;
            if (!D3d11Tools::read_pixels(device, t.get(), px, error) || px.size() != 64 * 64 * 4 || px[1] != 255 || px[0] != 0) {
                problems.add("the device does not render after the extreme requests: " + error);
            }
        }
    }
    context.measure("max_dimension", max_dimension);
    return problems.outcome();
}

Outcome ResChecks::exhaustion_and_reclaim(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    VideoMemory video;
    if (!DxgiBudget::query(adapter.luid, true, video, error)) {
        return Outcome::fail(error);
    }
    const std::uint64_t chunk = 256ull << 20;
    const std::uint64_t cap = std::min<std::uint64_t>(std::max<std::uint64_t>(video.budget + video.budget / 2, 2ull << 30), sizeof(void*) == 4 ? (1ull << 30) : (32ull << 30));
    Problems problems;
    std::vector<Com<ID3D11Texture2D>> held;
    std::vector<Com<ID3D11RenderTargetView>> views;
    HRESULT stop = S_OK;
    std::uint64_t total = 0;
    while (total < cap) {
        HRESULT result = S_OK;
        Com<ID3D11Texture2D> t = D3d11Tools::texture(device, D3d11Tools::desc2d(8192, 8192, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_RENDER_TARGET), nullptr, &result);
        if (!t) {
            stop = result;
            break;
        }
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, t.get());
        if (view) {
            D3d11Tools::clear(device, view.get(), 1, 0, 0, 0);
        }
        total += chunk;
        held.push_back(std::move(t));
        views.push_back(std::move(view));
        if (held.size() % 8 == 0) {
            device.context->Flush();
        }
    }
    context.measure("allocated_mib", total >> 20);
    context.measure("budget_mib", video.budget >> 20);
    if (stop == S_OK) {
        context.note("no failure was reached before the cap of " + std::to_string(cap >> 20) + " MiB");
    } else if (stop != E_OUTOFMEMORY) {
        problems.add("allocation ended with " + D3d11Tools::hr(stop) + " instead of E_OUTOFMEMORY");
    }
    const std::size_t count = held.size();
    {
        Com<ID3D11Texture2D> t = D3d11Tools::target(device, 64, 64);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, t.get());
        if (view) {
            D3d11Tools::clear(device, view.get(), 0, 0, 1, 1);
            std::vector<std::uint8_t> px;
            if (D3d11Tools::read_pixels(device, t.get(), px, error)) {
                if (px[2] != 255) {
                    problems.add("a small target holds wrong data while memory is exhausted");
                }
            } else {
                context.note("a small target could not be read while memory was exhausted: " + error);
            }
        } else {
            context.note("a small target could not be created while memory was exhausted");
        }
    }
    views.clear();
    held.clear();
    device.context->ClearState();
    device.context->Flush();
    static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
    ::Sleep(300);
    std::size_t again = 0;
    for (; again < count; ++again) {
        HRESULT result = S_OK;
        Com<ID3D11Texture2D> t = D3d11Tools::texture(device, D3d11Tools::desc2d(8192, 8192, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_RENDER_TARGET), nullptr, &result);
        if (!t) {
            break;
        }
        held.push_back(std::move(t));
    }
    context.measure("allocated_again", static_cast<long long>(again));
    if (again < count) {
        problems.add("only " + std::to_string(again) + " of the " + std::to_string(count) + " allocations succeed again after releasing them");
    }
    held.clear();
    return problems.outcome();
}

Outcome ResChecks::exit_reclaim(Context& context) {
    const std::string mode = ChildMode::variable(L"LSW_SUITES_EXIT_MODE");
    AvAdapter adapter;
    std::string error;
    if (ChildMode::active() && !mode.empty()) {
        D3d11Device& device = *new D3d11Device();
        if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
            return Outcome::fail(error);
        }
        VideoMemory video;
        static_cast<void>(DxgiBudget::query(adapter.luid, true, video, error));
        const std::uint64_t want = std::min<std::uint64_t>(std::max<std::uint64_t>(video.budget / 4, 256ull << 20), sizeof(void*) == 4 ? (512ull << 20) : (2ull << 30));
        std::uint64_t total = 0;
        while (total < want) {
            Com<ID3D11Texture2D> t = D3d11Tools::target(device, 4096, 4096, DXGI_FORMAT_R32_FLOAT);
            if (!t) {
                break;
            }
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, t.get());
            D3d11Tools::clear(device, view.get(), 1, 1, 1, 1);
            total += 64ull << 20;
            static_cast<void>(t.detach());
            static_cast<void>(view.detach());
        }
        static_cast<void>(D3d11Tools::wait_event(device, device.context, error));
        context.measure("held_mib", total >> 20);
        if (mode == "kill") {
            ::TerminateProcess(::GetCurrentProcess(), 0);
        }
        return Outcome::ok();
    }
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    D3d11Device device;
    if (!device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (const char* how : {"clean", "kill"}) {
        VideoMemory before;
        VideoMemory after;
        if (!DxgiBudget::query(adapter.luid, true, before, error)) {
            return Outcome::fail(error);
        }
        ChildRunOptions options;
        options.timeout_seconds = 120;
        options.env = {{"LSW_SUITES_EXIT_MODE", how}};
        const CheckResult child = ChildRuns::run_self("sanity.d3d11.res.exit_reclaim", options);
        if (std::string(how) == "clean" && child.status != Status::Ok) {
            problems.add(std::string(how) + " exit: the allocating process failed: " + child.reason);
            continue;
        }
        const std::uint64_t free_before = before.budget > before.usage ? before.budget - before.usage : 0;
        bool restored = false;
        std::uint64_t free_after = 0;
        for (int poll = 0; poll < 150 && !restored; ++poll) {
            if (DxgiBudget::query(adapter.luid, true, after, error)) {
                free_after = after.budget > after.usage ? after.budget - after.usage : 0;
                restored = free_after + (256ull << 20) >= free_before;
            }
            if (!restored) {
                ::Sleep(100);
            }
        }
        context.measure(std::string(how) + ".free_before", free_before);
        context.measure(std::string(how) + ".free_after", free_after);
        if (!restored) {
            problems.add(std::string(how) + " exit: the memory of the exited process was not returned within 15 s");
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
