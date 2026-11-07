#include "sanity/d3d10/create_checks.hpp"

#include "sanity/d3d10/kit.hpp"
#include "support/format_codec.hpp"

#include <cstring>
#include <vector>

namespace lsw::suites::sanity::d3d10 {

namespace {

constexpr UINT kSingleThreaded = D3D10_CREATE_DEVICE_SINGLETHREADED;
constexpr UINT kDebug = D3D10_CREATE_DEVICE_DEBUG;
constexpr UINT kBgra = D3D10_CREATE_DEVICE_BGRA_SUPPORT;

D3D10_DRIVER_TYPE invalid_driver_type() {
    const int value = 99;
    D3D10_DRIVER_TYPE type{};
    std::memcpy(&type, &value, sizeof(type));
    return type;
}

bool renders(D3d10Device& device, std::string& error) {
    Com<ID3D10Texture2D> target = D3d10Tools::target(device, 8, 8);
    Com<ID3D10RenderTargetView> view = D3d10Tools::rtv(device, target.get());
    if (!view) {
        error = "no render target view";
        return false;
    }
    D3d10Tools::clear(device, view.get(), 0.2f, 0.6f, 1.0f, 1.0f);
    TexelImage image;
    if (!D3d10Tools::read_texture(device, target.get(), image, error)) {
        return false;
    }
    std::string where;
    if (!Kit::uniform(image, D3d10Tools::bgra(51, 153, 255, 255), where)) {
        error = "a clear on the new device reads back wrong: " + where;
        return false;
    }
    return true;
}

}  // namespace

Outcome CreateChecks::device_flags(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    if (!D3d10Device::library_available(false)) {
        return Outcome::fail("d3d10.dll has no D3D10CreateDevice");
    }
    Problems problems;
    IDXGIAdapter* raw_adapter = static_cast<IDXGIAdapter*>(AvAdapters::open_adapter(adapter.luid));
    if (raw_adapter == nullptr) {
        return Outcome::fail("the adapter cannot be opened by LUID");
    }
    Com<IDXGIAdapter> dxgi_adapter(raw_adapter);
    const UINT combos[] = {0, kSingleThreaded, kBgra, kSingleThreaded | kBgra, kDebug, kDebug | kBgra | kSingleThreaded};
    int created = 0;
    for (const bool with_1 : {false, true}) {
        if (with_1 && !D3d10Device::library_available(true)) {
            context.note("d3d10_1.dll is not available");
            continue;
        }
        for (const UINT flags : combos) {
            const std::string name = std::string(Kit::name(with_1)) + " with flags 0x" + Text::hex(flags, 2);
            D3d10Device device;
            HRESULT result = S_OK;
            if (with_1) {
                result = D3d10Device::raw_create1(dxgi_adapter.get(), D3D10_DRIVER_TYPE_HARDWARE, nullptr, flags, D3D10_FEATURE_LEVEL_10_1, &device.device1);
                device.device = device.device1;
                if (device.device != nullptr) {
                    device.device->AddRef();
                }
            } else {
                result = D3d10Device::raw_create(dxgi_adapter.get(), D3D10_DRIVER_TYPE_HARDWARE, nullptr, flags, &device.device);
            }
            if ((flags & kDebug) != 0 && FAILED(result)) {
                context.note(name + ": the debug layer is not installed (" + Kit::hr(result) + ")");
                continue;
            }
            if (FAILED(result)) {
                problems.add(name + ": creation failed with " + Kit::hr(result));
                continue;
            }
            ++created;
            if ((flags & kBgra) != 0 && with_1) {
                UINT support = 0;
                device.device->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &support);
                if ((support & D3D10_FORMAT_SUPPORT_RENDER_TARGET) == 0) {
                    problems.add(name + ": BGRA_SUPPORT is set but B8G8R8A8_UNORM cannot be a render target");
                }
            }
            if ((flags & kSingleThreaded) == 0) {
                Com<ID3D10Multithread> multithread = Kit::query<ID3D10Multithread>(device.device);
                if (!multithread) {
                    problems.add(name + ": a device without SINGLETHREADED does not expose ID3D10Multithread");
                }
            }
            std::string why;
            if (!renders(device, why)) {
                problems.add(name + ": " + why);
            }
            const UINT expected_flags = flags & (kSingleThreaded | (with_1 ? kBgra : 0u) | kDebug);
            if (device.device->GetCreationFlags() != expected_flags) {
                problems.add(name + ": GetCreationFlags returns 0x" + Text::hex(device.device->GetCreationFlags(), 2));
            }
        }
        {
            D3d10Device device;
            HRESULT result = S_OK;
            if (with_1) {
                result = D3d10Device::raw_create1(dxgi_adapter.get(), invalid_driver_type(), nullptr, 0, D3D10_FEATURE_LEVEL_10_1, &device.device1);
            } else {
                result = D3d10Device::raw_create(dxgi_adapter.get(), invalid_driver_type(), nullptr, 0, &device.device);
            }
            if (SUCCEEDED(result)) {
                problems.add(std::string(Kit::name(with_1)) + ": the driver type 99 is accepted");
            } else if (!Kit::documented_creation_failure(result)) {
                problems.add(std::string(Kit::name(with_1)) + ": the driver type 99 fails with the undocumented " + Kit::hr(result));
            }
            D3d10Device with_adapter;
            if (with_1) {
                result = D3d10Device::raw_create1(dxgi_adapter.get(), D3D10_DRIVER_TYPE_WARP, nullptr, 0, D3D10_FEATURE_LEVEL_10_1, &with_adapter.device1);
            } else {
                result = D3d10Device::raw_create(dxgi_adapter.get(), D3D10_DRIVER_TYPE_WARP, nullptr, 0, &with_adapter.device);
            }
            if (SUCCEEDED(result)) {
                problems.add(std::string(Kit::name(with_1)) + ": an explicit adapter together with the WARP driver type is accepted");
            } else if (!Kit::documented_creation_failure(result)) {
                problems.add(std::string(Kit::name(with_1)) + ": an explicit adapter with the WARP type fails with the undocumented " + Kit::hr(result));
            }
            D3d10Device software;
            if (with_1) {
                result = D3d10Device::raw_create1(nullptr, D3D10_DRIVER_TYPE_SOFTWARE, nullptr, 0, D3D10_FEATURE_LEVEL_10_1, &software.device1);
            } else {
                result = D3d10Device::raw_create(nullptr, D3D10_DRIVER_TYPE_SOFTWARE, nullptr, 0, &software.device);
            }
            if (SUCCEEDED(result)) {
                problems.add(std::string(Kit::name(with_1)) + ": the software driver type without a module is accepted");
            } else if (!Kit::documented_creation_failure(result)) {
                problems.add(std::string(Kit::name(with_1)) + ": the software driver type without a module fails with the undocumented " + Kit::hr(result));
            }
        }
    }
    context.measure("devices_created", created);
    return problems.outcome();
}

Outcome CreateChecks::levels_101(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    if (!D3d10Device::library_available(true)) {
        return Context::skip("d3d10_1.dll is not available");
    }
    Problems problems;
    for (const D3D10_FEATURE_LEVEL1 level : {D3D10_FEATURE_LEVEL_10_0, D3D10_FEATURE_LEVEL_10_1}) {
        const bool at_101 = level == D3D10_FEATURE_LEVEL_10_1;
        const std::string name = at_101 ? "level 10_1" : "level 10_0";
        D3d10Device device;
        if (!device.create1(adapter.luid, 0, level, error)) {
            if (!Kit::documented_creation_failure(device.result)) {
                problems.add(name + ": creation fails with the undocumented " + Kit::hr(device.result));
            } else {
                context.note(name + " is refused by the adapter: " + Kit::hr(device.result));
            }
            continue;
        }
        if (device.device1->GetFeatureLevel() != level) {
            problems.add(name + ": the device reports feature level 0x" + Text::hex(static_cast<unsigned>(device.device1->GetFeatureLevel()), 4));
        }
        Com<ID3D10Texture2D> cube_array = D3d10Tools::texture(device, [] {
            D3D10_TEXTURE2D_DESC d = D3d10Tools::desc2d(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DEFAULT, 0, 1, 12);
            d.MiscFlags = D3D10_RESOURCE_MISC_TEXTURECUBE;
            return d;
        }());
        D3D10_SHADER_RESOURCE_VIEW_DESC1 view_desc{};
        view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view_desc.ViewDimension = D3D10_1_SRV_DIMENSION_TEXTURECUBEARRAY;
        view_desc.TextureCubeArray.MipLevels = 1;
        view_desc.TextureCubeArray.NumCubes = 2;
        Com<ID3D10ShaderResourceView1> cube_view;
        const HRESULT cube_result = cube_array ? device.device1->CreateShaderResourceView1(cube_array.get(), &view_desc, cube_view.put()) : E_FAIL;
        if (at_101 && FAILED(cube_result)) {
            problems.add(name + ": a cube array view fails with " + Kit::hr(cube_result));
        } else if (!at_101 && SUCCEEDED(cube_result)) {
            problems.add(name + ": a cube array view is accepted at feature level 10_0");
        }
        Com<ID3D10Texture2D> multisampled = D3d10Tools::texture(device, D3d10Tools::desc2d(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DEFAULT, 0, 1, 1, 4));
        if (at_101) {
            if (!multisampled) {
                context.note(name + ": a 4x multisampled RGBA8 texture with a shader resource bind is refused; the depth variants below judge multisampled shader resources");
            } else {
                D3D10_SHADER_RESOURCE_VIEW_DESC1 ms_desc{};
                ms_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                ms_desc.ViewDimension = D3D10_1_SRV_DIMENSION_TEXTURE2DMS;
                Com<ID3D10ShaderResourceView1> ms_view;
                const HRESULT ms_result = device.device1->CreateShaderResourceView1(multisampled.get(), &ms_desc, ms_view.put());
                Com<ID3D10RenderTargetView> ms_target = D3d10Tools::rtv(device, multisampled.get());
                Com<ID3D10Texture2D> out = D3d10Tools::target(device, 4, 4);
                Com<ID3D10RenderTargetView> out_view = D3d10Tools::rtv(device, out.get());
                const std::string vs_source = "float4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }\n";
                Com<ID3D10VertexShader> vs = D3d10Tools::vs(device, vs_source, error);
                Com<ID3D10PixelShader> ps = D3d10Tools::ps(device, "Texture2DMS<float4, 4> t : register(t0);\nfloat4 main(float4 p : SV_Position) : SV_Target { return t.Load(int2(p.xy), 2); }\n", error, "ps_4_1");
                if (FAILED(ms_result) || !ms_target || !out_view || !vs || !ps) {
                    problems.add(name + ": sampling a multisampled texture in a shader cannot be set up: " + Kit::hr(ms_result) + " " + error);
                } else {
                    D3d10Tools::clear(device, ms_target.get(), 1.0f, 0.0f, 0.0f, 1.0f);
                    ID3D10RenderTargetView* targets[1] = {out_view.get()};
                    device.device->OMSetRenderTargets(1, targets, nullptr);
                    D3d10Tools::set_viewport(device, 4, 4);
                    ID3D10ShaderResourceView* views[1] = {ms_view.get()};
                    device.device->PSSetShaderResources(0, 1, views);
                    device.device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    device.device->VSSetShader(vs.get());
                    device.device->PSSetShader(ps.get());
                    device.device->Draw(3, 0);
                    device.device->ClearState();
                    TexelImage image;
                    std::string where;
                    if (!D3d10Tools::read_texture(device, out.get(), image, error) || !Kit::uniform(image, D3d10Tools::bgra(255, 0, 0, 255), where)) {
                        problems.add(name + ": Load from a multisampled shader resource does not return the cleared colour: " + where + error);
                    }
                }
            }
        } else {
            std::vector<std::uint8_t> code;
            if (D3d10Tools::compile("float4 main() : SV_Target { return 0; }\n", "ps_4_1", code, error)) {
                Com<ID3D10PixelShader> shader;
                if (SUCCEEDED(device.device->CreatePixelShader(code.data(), code.size(), shader.put()))) {
                    problems.add(name + ": a ps_4_1 shader is accepted at feature level 10_0");
                }
            }
        }
        struct DepthCase {
            DXGI_FORMAT typeless;
            DXGI_FORMAT depth;
            const char* label;
        };
        for (const DepthCase& depth_case : {DepthCase{DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, "R24G8_TYPELESS"}, DepthCase{DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, "R32_TYPELESS"}}) {
            UINT quality = 0;
            device.device1->CheckMultisampleQualityLevels(depth_case.depth, 4, &quality);
            HRESULT created = S_OK;
            Com<ID3D10Texture2D> depth_texture = D3d10Tools::texture(device, D3d10Tools::desc2d(4, 4, depth_case.typeless, D3D10_BIND_DEPTH_STENCIL | D3D10_BIND_SHADER_RESOURCE, D3D10_USAGE_DEFAULT, 0, 1, 1, 4), nullptr, &created);
            if (at_101 && quality > 0 && !depth_texture) {
                problems.add(name + ": a 4x multisampled " + depth_case.label + " texture with depth stencil and shader resource binds fails with " + Kit::hr(created));
            } else if (!at_101 && depth_texture) {
                problems.add(name + ": a 4x multisampled " + depth_case.label + " texture with depth stencil and shader resource binds is accepted at feature level 10_0");
            }
        }
        std::string why;
        if (!renders(device, why)) {
            problems.add(name + ": " + why);
        }
    }
    {
        D3d10Device device;
        Com<IDXGIAdapter> dxgi_adapter(static_cast<IDXGIAdapter*>(AvAdapters::open_adapter(adapter.luid)));
        const HRESULT result = D3d10Device::raw_create1(dxgi_adapter.get(), D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0, static_cast<D3D10_FEATURE_LEVEL1>(0xb000), &device.device1);
        if (SUCCEEDED(result)) {
            problems.add("a feature level above 10_1 is accepted by D3D10CreateDevice1");
        } else if (!Kit::documented_creation_failure(result)) {
            problems.add("an unknown feature level fails with the undocumented " + Kit::hr(result));
        }
    }
    return problems.outcome();
}

Outcome CreateChecks::format_support(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                   DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R16G16_FLOAT,
                                   DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_BC1_UNORM,
                                   DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_R9G9B9E5_SHAREDEXP, DXGI_FORMAT_R32G32B32_FLOAT,
                                   DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R8_UINT};
    Problems problems;
    int compared = 0;
    for (const bool with_1 : {false, true}) {
        if (with_1 && !D3d10Device::library_available(true)) {
            continue;
        }
        D3d10Device device;
        if (!Kit::open(adapter, device, with_1, error)) {
            return Outcome::fail(error);
        }
        for (const DXGI_FORMAT format : formats) {
            const std::string name = std::string(Kit::name(with_1)) + " format " + std::to_string(static_cast<unsigned>(format));
            UINT support = 0;
            const HRESULT query = device.device->CheckFormatSupport(format, &support);
            if (FAILED(query)) {
                support = 0;
            }
            const bool texture2d = (support & D3D10_FORMAT_SUPPORT_TEXTURE2D) != 0;
            const struct {
                const char* what;
                UINT bind;
                UINT bit;
            } binds[] = {{"render target", D3D10_BIND_RENDER_TARGET, D3D10_FORMAT_SUPPORT_RENDER_TARGET},
                         {"depth-stencil", D3D10_BIND_DEPTH_STENCIL, D3D10_FORMAT_SUPPORT_DEPTH_STENCIL},
                         {"shader resource", D3D10_BIND_SHADER_RESOURCE, D3D10_FORMAT_SUPPORT_SHADER_SAMPLE | D3D10_FORMAT_SUPPORT_SHADER_LOAD}};
            for (const auto& b : binds) {
                if (FormatCodec::is_typeless(format)) {
                    continue;
                }
                HRESULT created = S_OK;
                Com<ID3D10Texture2D> t = D3d10Tools::texture(device, D3d10Tools::desc2d(64, 64, format, b.bind), nullptr, &created);
                const bool expected = texture2d && (support & b.bit) != 0;
                ++compared;
                if (expected && FAILED(created)) {
                    problems.add(name + " " + b.what + ": CheckFormatSupport says it works but creation fails with " + Kit::hr(created));
                } else if (!expected && SUCCEEDED(created)) {
                    problems.add(name + " " + b.what + ": CheckFormatSupport 0x" + Text::hex(support, 8) + " says it does not work but creation succeeds");
                }
            }
            for (const UINT samples : {2u, 4u, 8u}) {
                UINT levels = 0;
                const HRESULT quality = device.device->CheckMultisampleQualityLevels(format, samples, &levels);
                if (FAILED(quality)) {
                    levels = 0;
                }
                const bool depth = (support & D3D10_FORMAT_SUPPORT_DEPTH_STENCIL) != 0;
                const UINT bind = depth ? D3D10_BIND_DEPTH_STENCIL : D3D10_BIND_RENDER_TARGET;
                if (!texture2d || (support & (D3D10_FORMAT_SUPPORT_RENDER_TARGET | D3D10_FORMAT_SUPPORT_DEPTH_STENCIL)) == 0) {
                    continue;
                }
                D3D10_TEXTURE2D_DESC desc = D3d10Tools::desc2d(32, 32, format, bind, D3D10_USAGE_DEFAULT, 0, 1, 1, samples);
                HRESULT created = S_OK;
                if (levels > 0) {
                    desc.SampleDesc.Quality = levels - 1;
                    Com<ID3D10Texture2D> top = D3d10Tools::texture(device, desc, nullptr, &created);
                    if (FAILED(created)) {
                        problems.add(name + " with " + std::to_string(samples) + " samples: quality level " + std::to_string(levels - 1) + " of " + std::to_string(levels) + " cannot be created: " + Kit::hr(created));
                    }
                    desc.SampleDesc.Quality = levels;
                    Com<ID3D10Texture2D> beyond = D3d10Tools::texture(device, desc, nullptr, &created);
                    if (SUCCEEDED(created)) {
                        problems.add(name + " with " + std::to_string(samples) + " samples: quality level " + std::to_string(levels) + " is created although only " + std::to_string(levels) + " are reported");
                    }
                } else {
                    Com<ID3D10Texture2D> none = D3d10Tools::texture(device, desc, nullptr, &created);
                    if (SUCCEEDED(created)) {
                        problems.add(name + " with " + std::to_string(samples) + " samples: zero quality levels are reported but creation succeeds");
                    }
                }
                ++compared;
            }
        }
    }
    context.measure("comparisons", compared);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d10
