#include "sanity/d3d11/fl_checks.hpp"

#include "core/text.hpp"
#include "sanity/d3d11/kit.hpp"
#include "support/d3d11_tools.hpp"
#include "support/format_codec.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::d3d11 {

namespace {

const char* level_name(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_9_1: return "9_1";
    case D3D_FEATURE_LEVEL_9_2: return "9_2";
    case D3D_FEATURE_LEVEL_9_3: return "9_3";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    default: return "?";
    }
}

bool refused_code(HRESULT result) {
    return result == E_INVALIDARG || result == DXGI_ERROR_UNSUPPORTED;
}

template <typename T>
HRESULT query(D3d11Device& device, D3D11_FEATURE feature, T& out) {
    std::memset(&out, 0, sizeof(T));
    return device.device->CheckFeatureSupport(feature, &out, sizeof(T));
}

UINT support1(D3d11Device& device, DXGI_FORMAT format) {
    UINT bits = 0;
    if (FAILED(device.device->CheckFormatSupport(format, &bits))) {
        return 0;
    }
    return bits;
}

UINT support2(D3d11Device& device, DXGI_FORMAT format) {
    D3D11_FEATURE_DATA_FORMAT_SUPPORT2 data{};
    data.InFormat = format;
    if (FAILED(device.device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &data, sizeof(data)))) {
        return 0;
    }
    return data.OutFormatSupport2;
}

bool open(AvAdapter& adapter, D3d11Device& device, std::string& error) {
    return TargetAdapter::pick(adapter, error) && device.create(adapter.luid, error);
}

}  // namespace

Outcome FlChecks::requested_levels(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    const D3D_FEATURE_LEVEL descending[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                            D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1};
    Problems problems;
    std::vector<D3D_FEATURE_LEVEL> supported;
    for (D3D_FEATURE_LEVEL level : descending) {
        D3d11Device device;
        if (device.create_ex(adapter.luid, &level, 1, 0, error)) {
            if (device.level != level) {
                problems.add(std::string("a list of only ") + level_name(level) + " returned level " +
                             level_name(device.level));
            }
            if (device.device->GetFeatureLevel() != device.level) {
                problems.add(std::string("GetFeatureLevel disagrees with the level returned for ") + level_name(level));
            }
            supported.push_back(level);
        } else if (!refused_code(device.result)) {
            problems.add(std::string("a list of only ") + level_name(level) + " failed with " +
                         D3d11Tools::hr(device.result) + " instead of E_INVALIDARG or DXGI_ERROR_UNSUPPORTED");
        }
    }
    if (supported.empty()) {
        problems.add("no feature level can be created");
        return problems.outcome();
    }
    std::string text;
    for (D3D_FEATURE_LEVEL level : supported) {
        text += std::string(level_name(level)) + " ";
    }
    context.note("supported levels: " + text);
    context.measure("highest_level", level_name(supported.front()));
    {
        D3d11Device device;
        if (!device.create_ex(adapter.luid, descending, 9, 0, error)) {
            problems.add("the full descending list failed: " + error);
        } else if (device.level != supported.front()) {
            problems.add(std::string("the descending list returned ") + level_name(device.level) + ", the highest supported is " +
                         level_name(supported.front()));
        }
    }
    {
        std::vector<D3D_FEATURE_LEVEL> ascending(supported.rbegin(), supported.rend());
        D3d11Device device;
        if (!device.create_ex(adapter.luid, ascending.data(), static_cast<UINT>(ascending.size()), 0, error)) {
            problems.add("the ascending list failed: " + error);
        } else if (device.level != ascending.front()) {
            problems.add(std::string("the ascending list returned ") + level_name(device.level) +
                         ", the first supported entry is " + level_name(ascending.front()));
        }
    }
    {
        const D3D_FEATURE_LEVEL mixed[] = {D3D_FEATURE_LEVEL_12_1, supported.back()};
        D3d11Device device;
        if (!device.create_ex(adapter.luid, mixed, 2, 0, error)) {
            problems.add("a list with an unsupported leading level failed: " + error);
        } else if (device.level != supported.front() && device.level != supported.back()) {
            problems.add(std::string("the mixed list returned ") + level_name(device.level));
        }
    }
    return problems.outcome();
}

Outcome FlChecks::checkfeaturesupport_options(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    const D3D_FEATURE_LEVEL level = device.level;
    context.note(std::string("device level ") + level_name(level));
    auto acceptable = [&](const char* name, HRESULT result) {
        if (result != S_OK && result != E_INVALIDARG) {
            problems.add(std::string(name) + " returned " + D3d11Tools::hr(result));
            return false;
        }
        return result == S_OK;
    };
    auto stable = [&](const char* name, D3D11_FEATURE feature, auto& first) {
        auto second = first;
        std::memset(&second, 0, sizeof(second));
        const HRESULT again = device.device->CheckFeatureSupport(feature, &second, sizeof(second));
        if (again != S_OK || std::memcmp(&first, &second, sizeof(second)) != 0) {
            problems.add(std::string(name) + " changed between two identical queries");
        }
    };
    D3D11_FEATURE_DATA_THREADING threading{};
    if (acceptable("THREADING", query(device, D3D11_FEATURE_THREADING, threading))) {
        stable("THREADING", D3D11_FEATURE_THREADING, threading);
        if (threading.DriverCommandLists && !threading.DriverConcurrentCreates) {
            context.note("driver command lists without concurrent creates");
        }
    }
    D3D11_FEATURE_DATA_DOUBLES doubles{};
    if (acceptable("DOUBLES", query(device, D3D11_FEATURE_DOUBLES, doubles))) {
        stable("DOUBLES", D3D11_FEATURE_DOUBLES, doubles);
        if (level < D3D_FEATURE_LEVEL_11_0 && doubles.DoublePrecisionFloatShaderOps) {
            problems.add("double precision shader ops reported below feature level 11_0");
        }
    }
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS x10{};
    const bool have_x10 = acceptable("D3D10_X_HARDWARE_OPTIONS", query(device, D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, x10));
    if (have_x10) {
        stable("D3D10_X_HARDWARE_OPTIONS", D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, x10);
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    const bool have_options = acceptable("D3D11_OPTIONS", query(device, D3D11_FEATURE_D3D11_OPTIONS, options));
    if (have_options) {
        stable("D3D11_OPTIONS", D3D11_FEATURE_D3D11_OPTIONS, options);
        if (level < D3D_FEATURE_LEVEL_11_0 && options.MapNoOverwriteOnDynamicConstantBuffer != FALSE && level < D3D_FEATURE_LEVEL_10_0) {
            problems.add("D3D11_OPTIONS claims buffer map features at a 9_x level");
        }
    }
    D3D11_FEATURE_DATA_ARCHITECTURE_INFO architecture{};
    if (acceptable("ARCHITECTURE_INFO", query(device, D3D11_FEATURE_ARCHITECTURE_INFO, architecture))) {
        stable("ARCHITECTURE_INFO", D3D11_FEATURE_ARCHITECTURE_INFO, architecture);
        if (architecture.TileBasedDeferredRenderer != TRUE && architecture.TileBasedDeferredRenderer != FALSE) {
            problems.add("TileBasedDeferredRenderer is not a BOOL");
        }
    }
    D3D11_FEATURE_DATA_D3D9_OPTIONS d3d9{};
    if (acceptable("D3D9_OPTIONS", query(device, D3D11_FEATURE_D3D9_OPTIONS, d3d9))) {
        stable("D3D9_OPTIONS", D3D11_FEATURE_D3D9_OPTIONS, d3d9);
    }
    D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT precision{};
    if (acceptable("SHADER_MIN_PRECISION_SUPPORT", query(device, D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, precision))) {
        stable("SHADER_MIN_PRECISION_SUPPORT", D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, precision);
        const UINT known = D3D11_SHADER_MIN_PRECISION_10_BIT | D3D11_SHADER_MIN_PRECISION_16_BIT;
        if ((precision.PixelShaderMinPrecision & ~known) != 0 || (precision.AllOtherShaderStagesMinPrecision & ~known) != 0) {
            problems.add("SHADER_MIN_PRECISION_SUPPORT has bits outside the documented flags");
        }
    }
    D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT shadow{};
    if (acceptable("D3D9_SHADOW_SUPPORT", query(device, D3D11_FEATURE_D3D9_SHADOW_SUPPORT, shadow))) {
        stable("D3D9_SHADOW_SUPPORT", D3D11_FEATURE_D3D9_SHADOW_SUPPORT, shadow);
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 options1{};
    const bool have_options1 = acceptable("D3D11_OPTIONS1", query(device, D3D11_FEATURE_D3D11_OPTIONS1, options1));
    if (have_options1) {
        stable("D3D11_OPTIONS1", D3D11_FEATURE_D3D11_OPTIONS1, options1);
        if (options1.TiledResourcesTier > D3D11_TILED_RESOURCES_TIER_3 || options1.TiledResourcesTier < 0) {
            problems.add("TiledResourcesTier is outside the documented values");
        }
        if (options1.TiledResourcesTier != D3D11_TILED_RESOURCES_NOT_SUPPORTED && level < D3D_FEATURE_LEVEL_11_0) {
            problems.add("tiled resources reported below feature level 11_0");
        }
    }
    D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT simple{};
    if (acceptable("D3D9_SIMPLE_INSTANCING_SUPPORT", query(device, D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT, simple))) {
        stable("D3D9_SIMPLE_INSTANCING_SUPPORT", D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT, simple);
    }
    {
        BOOL marshalling = FALSE;
        const HRESULT result = device.device->CheckFeatureSupport(static_cast<D3D11_FEATURE>(D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT + 1), &marshalling, sizeof(marshalling));
        if (result != S_OK && result != E_INVALIDARG) {
            problems.add("MARSHALLING_SUPPORT returned " + D3d11Tools::hr(result));
        }
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2{};
    if (acceptable("D3D11_OPTIONS2", query(device, D3D11_FEATURE_D3D11_OPTIONS2, options2))) {
        stable("D3D11_OPTIONS2", D3D11_FEATURE_D3D11_OPTIONS2, options2);
        if (options2.ConservativeRasterizationTier > D3D11_CONSERVATIVE_RASTERIZATION_TIER_3) {
            problems.add("ConservativeRasterizationTier is outside the documented values");
        }
        if (options2.ConservativeRasterizationTier != D3D11_CONSERVATIVE_RASTERIZATION_NOT_SUPPORTED &&
            level < D3D_FEATURE_LEVEL_11_0) {
            problems.add("conservative rasterization reported below feature level 11_0");
        }
        if (options2.ROVsSupported && level < D3D_FEATURE_LEVEL_11_0) {
            problems.add("rasterizer ordered views reported below feature level 11_0");
        }
        if (options2.TypedUAVLoadAdditionalFormats && level < D3D_FEATURE_LEVEL_11_0) {
            problems.add("additional typed UAV loads reported below feature level 11_0");
        }
        if (options2.TypedUAVLoadAdditionalFormats) {
            const DXGI_FORMAT required[] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_UINT,
                                            DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM,
                                            DXGI_FORMAT_R16_FLOAT,          DXGI_FORMAT_R8_UNORM};
            for (DXGI_FORMAT format : required) {
                if ((support2(device, format) & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) == 0) {
                    problems.add("TypedUAVLoadAdditionalFormats is set but typed load is missing for format " +
                                 std::to_string(format));
                }
            }
        }
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 options3{};
    if (acceptable("D3D11_OPTIONS3", query(device, D3D11_FEATURE_D3D11_OPTIONS3, options3))) {
        stable("D3D11_OPTIONS3", D3D11_FEATURE_D3D11_OPTIONS3, options3);
    }
    D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT address{};
    if (acceptable("GPU_VIRTUAL_ADDRESS_SUPPORT", query(device, D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, address))) {
        stable("GPU_VIRTUAL_ADDRESS_SUPPORT", D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, address);
        if (address.MaxGPUVirtualAddressBitsPerResource < 20 || address.MaxGPUVirtualAddressBitsPerProcess < 20) {
            problems.add("GPU virtual address bits are implausibly small");
        }
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS4 options4{};
    if (acceptable("D3D11_OPTIONS4", query(device, D3D11_FEATURE_D3D11_OPTIONS4, options4))) {
        stable("D3D11_OPTIONS4", D3D11_FEATURE_D3D11_OPTIONS4, options4);
    }
    D3D11_FEATURE_DATA_SHADER_CACHE cache{};
    if (acceptable("SHADER_CACHE", query(device, D3D11_FEATURE_SHADER_CACHE, cache))) {
        stable("SHADER_CACHE", D3D11_FEATURE_SHADER_CACHE, cache);
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS5 options5{};
    if (acceptable("D3D11_OPTIONS5", query(device, D3D11_FEATURE_D3D11_OPTIONS5, options5))) {
        stable("D3D11_OPTIONS5", D3D11_FEATURE_D3D11_OPTIONS5, options5);
    }
    {
        D3D11_FEATURE_DATA_THREADING small{};
        const HRESULT wrong = device.device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &small, 1);
        if (wrong != E_INVALIDARG) {
            problems.add("CheckFeatureSupport with a wrong size returned " + D3d11Tools::hr(wrong));
        }
        UINT dummy = 0;
        const HRESULT unknown = device.device->CheckFeatureSupport(Kit::raw_enum<D3D11_FEATURE>(0x77), &dummy, 4);
        if (unknown != E_INVALIDARG) {
            problems.add("CheckFeatureSupport with an undefined feature returned " + D3d11Tools::hr(unknown));
        }
    }
    if (level >= D3D_FEATURE_LEVEL_11_0) {
        std::string compile_error;
        const char* source = "RWByteAddressBuffer o : register(u0); [numthreads(1,1,1)] void main() { o.Store(0, 7); }";
        if (!D3d11Tools::cs(device, source, compile_error)) {
            problems.add("feature level 11 does not create a compute shader: " + compile_error);
        }
        const DXGI_FORMAT loadable[] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT};
        for (DXGI_FORMAT format : loadable) {
            if ((support2(device, format) & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) == 0 ||
                (support2(device, format) & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0) {
                problems.add("feature level 11 lacks typed UAV load or store for format " + std::to_string(format));
            }
        }
    } else if (level >= D3D_FEATURE_LEVEL_10_0 && have_x10) {
        std::string compile_error;
        const char* source = "RWByteAddressBuffer o : register(u0); [numthreads(1,1,1)] void main() { o.Store(0, 7); }";
        const bool created = D3d11Tools::cs(device, source, compile_error, "main").get() != nullptr;
        if (created != (x10.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x != FALSE)) {
            problems.add("compute shader 4.x creation disagrees with the D3D10_X_HARDWARE_OPTIONS flag");
        }
    }
    static_cast<void>(have_options);
    static_cast<void>(have_options1);
    return problems.outcome();
}

Outcome FlChecks::format_support_vs_creation(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11Device* d = device.device;
    const bool fl11 = device.level >= D3D_FEATURE_LEVEL_11_0;
    Problems problems;
    int mismatches = 0;
    auto mismatch = [&](int format, const std::string& text) {
        ++mismatches;
        if (mismatches <= 40) {
            problems.add("format " + std::to_string(format) + ": " + text);
        }
    };
    D3d11Device warp;
    std::string warp_error;
    const bool have_warp = warp.create_warp(nullptr, 0, 0, warp_error);
    auto disagrees = [&](bool created, bool bit, auto&& on_warp) {
        if (created == bit) {
            return false;
        }
        return !(created && !bit && have_warp && on_warp());
    };
    std::vector<int> formats;
    for (int format = 0; format <= 132; ++format) {
        if (format <= 115 || format >= 130 || format == 116 || format == 120) {
            formats.push_back(format);
        }
    }
    int created_any = 0;
    for (int number : formats) {
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(number);
        const UINT s1 = support1(device, format);
        const UINT s2 = support2(device, format);
        const bool typeless = FormatCodec::is_typeless(format);
        const bool video = FormatCodec::is_video(format);
        const bool block = FormatCodec::is_block(format);
        const UINT size = 16;
        auto texture_on = [&](ID3D11Device* target, UINT bind, UINT misc, UINT mips, UINT array_size, UINT samples) {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(size, size, format, bind, D3D11_USAGE_DEFAULT, 0, mips, array_size, samples);
            desc.MiscFlags = misc;
            Com<ID3D11Texture2D> out;
            return SUCCEEDED(target->CreateTexture2D(&desc, nullptr, out.put()));
        };
        auto texture = [&](UINT bind, UINT misc = 0, UINT mips = 1, UINT array_size = 1, UINT samples = 1) {
            return texture_on(d, bind, misc, mips, array_size, samples);
        };
        auto warp_texture = [&](UINT bind, UINT misc = 0, UINT mips = 1, UINT array_size = 1, UINT samples = 1) {
            return texture_on(warp.device, bind, misc, mips, array_size, samples);
        };
        if (number == 0) {
            const UINT image_bits = D3D11_FORMAT_SUPPORT_TEXTURE1D | D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_TEXTURE3D |
                                    D3D11_FORMAT_SUPPORT_TEXTURECUBE | D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_DEPTH_STENCIL;
            if ((s1 & image_bits) != 0 || texture(D3D11_BIND_SHADER_RESOURCE) || texture(0)) {
                mismatch(number, "DXGI_FORMAT_UNKNOWN reports texture, render target or depth stencil support or creates a texture");
            }
            continue;
        }
        if (number == 116 || number == 120) {
            if (s1 != 0 || texture(D3D11_BIND_SHADER_RESOURCE)) {
                mismatch(number, "an invalid format reports support or creates a texture");
            }
            continue;
        }
        const bool has2d = (s1 & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;
        if (disagrees(texture(0), has2d, [&] { return warp_texture(0); })) {
            mismatch(number, std::string("Texture2D support bit ") + (has2d ? "set" : "clear") + " but creation " +
                                 (has2d ? "failed" : "succeeded"));
        }
        if (has2d) {
            ++created_any;
        }
        {
            D3D11_TEXTURE1D_DESC desc{};
            desc.Width = size;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = format;
            desc.Usage = D3D11_USAGE_DEFAULT;
            Com<ID3D11Texture1D> out;
            const bool created = SUCCEEDED(d->CreateTexture1D(&desc, nullptr, out.put()));
            const bool bit = (s1 & D3D11_FORMAT_SUPPORT_TEXTURE1D) != 0;
            Com<ID3D11Texture1D> warp_out;
            if (disagrees(created, bit, [&] { return SUCCEEDED(warp.device->CreateTexture1D(&desc, nullptr, warp_out.put())); })) {
                mismatch(number, std::string("Texture1D support bit ") + (bit ? "set" : "clear") + " but creation " +
                                     (created ? "succeeded" : "failed"));
            }
        }
        {
            D3D11_TEXTURE3D_DESC desc{};
            desc.Width = size;
            desc.Height = size;
            desc.Depth = 4;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.Usage = D3D11_USAGE_DEFAULT;
            Com<ID3D11Texture3D> out;
            const bool created = SUCCEEDED(d->CreateTexture3D(&desc, nullptr, out.put()));
            const bool bit = (s1 & D3D11_FORMAT_SUPPORT_TEXTURE3D) != 0;
            Com<ID3D11Texture3D> warp_out;
            if (disagrees(created, bit, [&] { return SUCCEEDED(warp.device->CreateTexture3D(&desc, nullptr, warp_out.put())); })) {
                mismatch(number, std::string("Texture3D support bit ") + (bit ? "set" : "clear") + " but creation " +
                                     (created ? "succeeded" : "failed"));
            }
        }
        {
            const bool created = texture(0, D3D11_RESOURCE_MISC_TEXTURECUBE, 1, 6);
            const bool bit = (s1 & D3D11_FORMAT_SUPPORT_TEXTURECUBE) != 0;
            if (disagrees(created, bit, [&] { return warp_texture(0, D3D11_RESOURCE_MISC_TEXTURECUBE, 1, 6); })) {
                mismatch(number, std::string("TextureCube support bit ") + (bit ? "set" : "clear") + " but creation " +
                                     (created ? "succeeded" : "failed"));
            }
        }
        if (typeless || video || !has2d) {
            continue;
        }
        const bool rt_bit = (s1 & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
        if (disagrees(texture(D3D11_BIND_RENDER_TARGET), rt_bit, [&] { return warp_texture(D3D11_BIND_RENDER_TARGET); })) {
            mismatch(number, std::string("render target bit ") + (rt_bit ? "set" : "clear") + " disagrees with creation");
        } else if (rt_bit) {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(size, size, format, D3D11_BIND_RENDER_TARGET);
            Com<ID3D11Texture2D> tex;
            Com<ID3D11RenderTargetView> view;
            if (FAILED(d->CreateTexture2D(&desc, nullptr, tex.put())) ||
                FAILED(d->CreateRenderTargetView(tex.get(), nullptr, view.put()))) {
                mismatch(number, "a render target texture or its view cannot be created");
            }
        }
        const bool ds_bit = (s1 & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) != 0;
        if (disagrees(texture(D3D11_BIND_DEPTH_STENCIL), ds_bit, [&] { return warp_texture(D3D11_BIND_DEPTH_STENCIL); })) {
            mismatch(number, std::string("depth stencil bit ") + (ds_bit ? "set" : "clear") + " disagrees with creation");
        }
        const UINT sample_bits = D3D11_FORMAT_SUPPORT_SHADER_LOAD | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
                                 D3D11_FORMAT_SUPPORT_SHADER_GATHER | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON |
                                 D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON;
        const bool srv_bit = (s1 & sample_bits) != 0;
        if (disagrees(texture(D3D11_BIND_SHADER_RESOURCE), srv_bit, [&] { return warp_texture(D3D11_BIND_SHADER_RESOURCE); })) {
            mismatch(number, std::string("shader resource bits ") + (srv_bit ? "set" : "clear") + " disagree with creation");
        }
        if (fl11) {
            const bool uav_bit = (s1 & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0;
            const bool created = texture(D3D11_BIND_UNORDERED_ACCESS);
            if (disagrees(created, uav_bit, [&] { return warp_texture(D3D11_BIND_UNORDERED_ACCESS); })) {
                mismatch(number, std::string("typed UAV bit ") + (uav_bit ? "set" : "clear") + " disagrees with creation");
            } else if (created && uav_bit) {
                D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(size, size, format, D3D11_BIND_UNORDERED_ACCESS);
                Com<ID3D11Texture2D> tex;
                Com<ID3D11UnorderedAccessView> view;
                if (FAILED(d->CreateTexture2D(&desc, nullptr, tex.put())) ||
                    FAILED(d->CreateUnorderedAccessView(tex.get(), nullptr, view.put()))) {
                    mismatch(number, "a UAV texture or its view cannot be created");
                }
            }
        }
        const bool msaa_bit = (s1 & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) != 0;
        bool any_msaa = false;
        const UINT msaa_bind = ds_bit ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
        for (UINT count : {2u, 4u, 8u}) {
            if (rt_bit || ds_bit) {
                any_msaa = texture(msaa_bind, 0, 1, 1, count) || any_msaa;
            }
        }
        if (msaa_bit && !any_msaa && (rt_bit || ds_bit)) {
            mismatch(number, "multisample render target bit set but no 2x/4x/8x texture can be created");
        }
        if (!msaa_bit && any_msaa) {
            mismatch(number, "multisample render target bit clear but a multisampled target can be created");
        }
        if ((s1 & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0 && fl11) {
            if (!texture(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_GENERATE_MIPS, 0)) {
                mismatch(number, "mip autogeneration bit set but a GENERATE_MIPS texture cannot be created");
            }
        }
        if ((s2 & D3D11_FORMAT_SUPPORT2_SHAREABLE) != 0 && (rt_bit || srv_bit)) {
            const UINT bind = rt_bit ? D3D11_BIND_RENDER_TARGET : D3D11_BIND_SHADER_RESOURCE;
            if (!texture(bind, D3D11_RESOURCE_MISC_SHARED)) {
                mismatch(number, "shareable bit set but a MISC_SHARED texture cannot be created");
            }
        }
        if (!block && rt_bit && (s1 & D3D11_FORMAT_SUPPORT_BLENDABLE) != 0) {
            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].RenderTargetWriteMask = 0xf;
            Com<ID3D11BlendState> state;
            if (FAILED(d->CreateBlendState(&blend, state.put()))) {
                mismatch(number, "a blend state cannot be created");
            }
        }
    }
    if (created_any < 20) {
        problems.add("only " + std::to_string(created_any) + " formats report Texture2D support");
    }
    context.measure("formats_checked", static_cast<long long>(formats.size()));
    context.measure("mismatches", mismatches);
    if (mismatches > 40) {
        problems.add(std::to_string(mismatches - 40) + " more mismatches");
    }
    return problems.outcome();
}

Outcome FlChecks::msaa_quality_vs_use(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11Device* d = device.device;
    Problems problems;
    int checked = 0;
    for (int number = 1; number <= 115; ++number) {
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(number);
        if (FormatCodec::is_typeless(format) || FormatCodec::is_video(format)) {
            continue;
        }
        const UINT s1 = support1(device, format);
        const bool rt = (s1 & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
        const bool ds = (s1 & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) != 0;
        if (!rt && !ds) {
            continue;
        }
        const UINT bind = ds ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
        for (UINT count = 1; count <= 16; ++count) {
            UINT levels = 0;
            const HRESULT queried = d->CheckMultisampleQualityLevels(format, count, &levels);
            const bool reported = SUCCEEDED(queried) && levels > 0;
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(32, 32, format, bind, D3D11_USAGE_DEFAULT, 0, 1, 1, count);
            Com<ID3D11Texture2D> tex;
            const bool created = SUCCEEDED(d->CreateTexture2D(&desc, nullptr, tex.put()));
            ++checked;
            if (created != reported) {
                problems.add("format " + std::to_string(number) + " count " + std::to_string(count) + ": " +
                             std::to_string(levels) + " quality levels reported but creation " +
                             (created ? "succeeded" : "failed"));
            }
        }
    }
    context.measure("combinations", checked);
    const DXGI_FORMAT render_formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                                          DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
    const double color[4] = {51.0 / 255.0, 102.0 / 255.0, 153.0 / 255.0, 1.0};
    std::string pixel_source = "float4 main() : SV_Target { return float4(51.0/255.0, 102.0/255.0, 153.0/255.0, 1.0); }";
    Com<ID3D11PixelShader> pixel = D3d11Tools::ps(device, pixel_source, error);
    if (!pixel) {
        problems.add(error);
        return problems.outcome();
    }
    for (DXGI_FORMAT format : render_formats) {
        const FormatSpec spec = FormatCodec::spec(format);
        for (UINT count = 2; count <= 16; ++count) {
            UINT levels = 0;
            if (FAILED(d->CheckMultisampleQualityLevels(format, count, &levels)) || levels == 0) {
                continue;
            }
            for (UINT quality = 0; quality < std::min<UINT>(levels, 4); ++quality) {
                const std::string label = "format " + std::to_string(format) + " count " + std::to_string(count) +
                                          " quality " + std::to_string(quality);
                D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(16, 16, format, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, count);
                desc.SampleDesc.Quality = quality;
                Com<ID3D11Texture2D> multi;
                Com<ID3D11Texture2D> resolved;
                if (FAILED(d->CreateTexture2D(&desc, nullptr, multi.put()))) {
                    problems.add(label + ": the multisampled target cannot be created");
                    continue;
                }
                resolved = D3d11Tools::texture(device, D3d11Tools::desc2d(16, 16, format, D3D11_BIND_RENDER_TARGET));
                Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, multi.get());
                if (!view || !resolved) {
                    problems.add(label + ": the target view or resolve texture cannot be created");
                    continue;
                }
                D3d11Tools::clear(device, view.get(), 0, 0, 0, 0);
                if (!D3d11Tools::draw_fullscreen(device, pixel.get(), view.get(), 16, 16, error)) {
                    problems.add(error);
                    return problems.outcome();
                }
                device.context->ResolveSubresource(resolved.get(), 0, multi.get(), 0, format);
                std::vector<std::uint8_t> bytes;
                if (!D3d11Tools::read_pixels(device, resolved.get(), bytes, error)) {
                    problems.add(label + ": " + error);
                    continue;
                }
                std::string why;
                for (UINT p = 0; p < 256; p += 37) {
                    if (!FormatCodec::matches(spec, bytes.data() + static_cast<std::size_t>(p) * static_cast<std::size_t>(spec.bytes), color, why)) {
                        problems.add(label + ": resolved pixel " + std::to_string(p) + " " + why);
                        break;
                    }
                }
            }
        }
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome FlChecks::limits_at_level(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    struct Limits {
        D3D_FEATURE_LEVEL level;
        UINT dimension;
        UINT cube;
        UINT volume;
        UINT array_size;
        UINT targets;
        UINT viewports;
        UINT uavs;
    };
    const Limits table[] = {
        {D3D_FEATURE_LEVEL_9_1, 2048, 512, 256, 1, 1, 1, 0},     {D3D_FEATURE_LEVEL_9_2, 2048, 512, 256, 1, 1, 1, 0},
        {D3D_FEATURE_LEVEL_9_3, 4096, 4096, 256, 1, 4, 1, 0},    {D3D_FEATURE_LEVEL_10_0, 8192, 8192, 2048, 512, 8, 16, 0},
        {D3D_FEATURE_LEVEL_10_1, 8192, 8192, 2048, 512, 8, 16, 0}, {D3D_FEATURE_LEVEL_11_0, 16384, 16384, 2048, 2048, 8, 16, 8},
        {D3D_FEATURE_LEVEL_11_1, 16384, 16384, 2048, 2048, 8, 16, 64},
    };
    Problems problems;
    int tested = 0;
    for (const Limits& limits : table) {
        D3d11Device device;
        D3D_FEATURE_LEVEL level = limits.level;
        if (!device.create_ex(adapter.luid, &level, 1, 0, error)) {
            if (!refused_code(device.result)) {
                problems.add(std::string("level ") + level_name(level) + ": " + error);
            } else {
                context.note(std::string("level ") + level_name(level) + " is not supported");
            }
            continue;
        }
        ++tested;
        ID3D11Device* d = device.device;
        const std::string name = std::string("level ") + level_name(level) + ": ";
        D3d11Device warp;
        std::string warp_error;
        const bool have_warp = warp.create_warp(&level, 1, 0, warp_error);
        const UINT format_bits = support1(device, DXGI_FORMAT_R8G8B8A8_UNORM);
        int refusals = 0;
        auto refusal_ok = [&](HRESULT hardware, HRESULT reference) {
            context.measure(std::string("beyond_limit.") + level_name(level) + "." + std::to_string(refusals++), D3d11Tools::hr(hardware) + " (WARP " + D3d11Tools::hr(reference) + ")");
            return SUCCEEDED(hardware) || hardware == E_INVALIDARG || hardware == E_OUTOFMEMORY;
        };
        auto accepted = [](HRESULT result) { return SUCCEEDED(result) || result == E_OUTOFMEMORY; };
        {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(limits.dimension, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
            Com<ID3D11Texture2D> tex;
            HRESULT result = d->CreateTexture2D(&desc, nullptr, tex.put());
            if (!accepted(result) && (format_bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0) {
                problems.add(name + "a " + std::to_string(limits.dimension) + "-wide 2D texture failed: " + D3d11Tools::hr(result));
            }
            desc = D3d11Tools::desc2d(1, limits.dimension, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
            result = d->CreateTexture2D(&desc, nullptr, tex.put());
            if (!accepted(result) && (format_bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0) {
                problems.add(name + "a " + std::to_string(limits.dimension) + "-high 2D texture failed: " + D3d11Tools::hr(result));
            }
            desc = D3d11Tools::desc2d(limits.dimension + 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
            result = d->CreateTexture2D(&desc, nullptr, tex.put());
            HRESULT reference = E_INVALIDARG;
            if (have_warp) {
                Com<ID3D11Texture2D> ignored;
                reference = warp.device->CreateTexture2D(&desc, nullptr, ignored.put());
            }
            if (!refusal_ok(result, reference)) {
                problems.add(name + "a 2D texture beyond the limit returned " + D3d11Tools::hr(result));
            }
            D3D11_TEXTURE1D_DESC d1{};
            d1.Width = limits.dimension;
            d1.MipLevels = 1;
            d1.ArraySize = 1;
            d1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d1.Usage = D3D11_USAGE_DEFAULT;
            d1.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            Com<ID3D11Texture1D> tex1;
            result = d->CreateTexture1D(&d1, nullptr, tex1.put());
            if (!accepted(result) && (format_bits & D3D11_FORMAT_SUPPORT_TEXTURE1D) != 0) {
                problems.add(name + "a maximum 1D texture failed: " + D3d11Tools::hr(result));
            }
            d1.Width = limits.dimension + 1;
            result = d->CreateTexture1D(&d1, nullptr, tex1.put());
            reference = E_INVALIDARG;
            if (have_warp) {
                Com<ID3D11Texture1D> ignored;
                reference = warp.device->CreateTexture1D(&d1, nullptr, ignored.put());
            }
            if (!refusal_ok(result, reference)) {
                problems.add(name + "a 1D texture beyond the limit returned " + D3d11Tools::hr(result));
            }
        }
        if (limits.array_size > 1) {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1, limits.array_size);
            Com<ID3D11Texture2D> tex;
            HRESULT result = d->CreateTexture2D(&desc, nullptr, tex.put());
            if (!accepted(result)) {
                problems.add(name + "a maximum texture array failed: " + D3d11Tools::hr(result));
            }
            desc.ArraySize = limits.array_size + 1;
            result = d->CreateTexture2D(&desc, nullptr, tex.put());
            HRESULT reference = E_INVALIDARG;
            if (have_warp) {
                Com<ID3D11Texture2D> ignored;
                reference = warp.device->CreateTexture2D(&desc, nullptr, ignored.put());
            }
            if (!refusal_ok(result, reference)) {
                problems.add(name + "a texture array beyond the limit returned " + D3d11Tools::hr(result));
            }
        }
        {
            D3D11_TEXTURE3D_DESC desc{};
            desc.Width = limits.volume;
            desc.Height = 1;
            desc.Depth = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            Com<ID3D11Texture3D> tex;
            HRESULT result = d->CreateTexture3D(&desc, nullptr, tex.put());
            if (!accepted(result) && (format_bits & D3D11_FORMAT_SUPPORT_TEXTURE3D) != 0) {
                problems.add(name + "a maximum 3D texture failed: " + D3d11Tools::hr(result));
            }
            desc.Width = limits.volume + 1;
            result = d->CreateTexture3D(&desc, nullptr, tex.put());
            HRESULT reference = E_INVALIDARG;
            if (have_warp) {
                Com<ID3D11Texture3D> ignored;
                reference = warp.device->CreateTexture3D(&desc, nullptr, ignored.put());
            }
            if (!refusal_ok(result, reference)) {
                problems.add(name + "a 3D texture beyond the limit returned " + D3d11Tools::hr(result));
            }
        }
        {
            const UINT cube = std::min<UINT>(limits.cube, 2048);
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(cube, cube, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1, 6);
            desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
            Com<ID3D11Texture2D> tex;
            HRESULT result = d->CreateTexture2D(&desc, nullptr, tex.put());
            if (!accepted(result) && (format_bits & D3D11_FORMAT_SUPPORT_TEXTURECUBE) != 0) {
                problems.add(name + "a large cube texture failed: " + D3d11Tools::hr(result));
            }
            desc.Width = desc.Height = limits.cube + 1;
            result = d->CreateTexture2D(&desc, nullptr, tex.put());
            HRESULT reference = E_INVALIDARG;
            if (have_warp) {
                Com<ID3D11Texture2D> ignored;
                reference = warp.device->CreateTexture2D(&desc, nullptr, ignored.put());
            }
            if (!refusal_ok(result, reference)) {
                problems.add(name + "a cube texture beyond the limit returned " + D3d11Tools::hr(result));
            }
        }
        if (level < D3D_FEATURE_LEVEL_10_0) {
            continue;
        }
        {
            std::vector<Com<ID3D11Texture2D>> textures(limits.targets);
            std::vector<Com<ID3D11RenderTargetView>> views(limits.targets);
            ID3D11RenderTargetView* raw[8] = {};
            std::string source = "struct O {";
            std::string body = "O main() { O o;";
            for (UINT i = 0; i < limits.targets; ++i) {
                source += " float4 c" + std::to_string(i) + " : SV_Target" + std::to_string(i) + ";";
                body += " o.c" + std::to_string(i) + " = float4(" + std::to_string(i + 1) + ".0/16.0, 0, 0, 1);";
                textures[i] = D3d11Tools::target(device, 8, 8);
                views[i] = D3d11Tools::rtv(device, textures[i].get());
                raw[i] = views[i].get();
            }
            source += " };\n" + body + " return o; }";
            Com<ID3D11PixelShader> pixel = D3d11Tools::ps(device, source, error);
            Com<ID3D11VertexShader> vertex = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
            if (!pixel || !vertex) {
                problems.add(name + error);
            } else {
                device.context->OMSetRenderTargets(limits.targets, raw, nullptr);
                D3d11Tools::set_viewport(device, 8, 8);
                device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                device.context->VSSetShader(vertex.get(), nullptr, 0);
                device.context->PSSetShader(pixel.get(), nullptr, 0);
                device.context->Draw(3, 0);
                for (UINT i = 0; i < limits.targets; ++i) {
                    std::vector<std::uint8_t> bytes;
                    if (!D3d11Tools::read_pixels(device, textures[i].get(), bytes, error)) {
                        problems.add(name + error);
                        break;
                    }
                    const int want = static_cast<int>(std::lround((i + 1) * 255.0 / 16.0));
                    if (bytes[0] != want || bytes[3] != 255) {
                        problems.add(name + "render target " + std::to_string(i) + " holds " + std::to_string(bytes[0]) +
                                     ", expected " + std::to_string(want));
                    }
                }
            }
            D3D11_VIEWPORT viewports[16];
            D3D11_RECT rects[16];
            for (UINT i = 0; i < limits.viewports; ++i) {
                viewports[i] = {static_cast<float>(i), 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
                rects[i] = {static_cast<LONG>(i), 0, static_cast<LONG>(i + 4), 4};
            }
            device.context->RSSetViewports(limits.viewports, viewports);
            device.context->RSSetScissorRects(limits.viewports, rects);
            D3D11_VIEWPORT back[16];
            D3D11_RECT back_rects[16];
            UINT count = 16;
            UINT rect_count = 16;
            device.context->RSGetViewports(&count, back);
            device.context->RSGetScissorRects(&rect_count, back_rects);
            if (count != limits.viewports || rect_count != limits.viewports ||
                back[limits.viewports - 1].TopLeftX != static_cast<float>(limits.viewports - 1) ||
                back_rects[limits.viewports - 1].left != static_cast<LONG>(limits.viewports - 1)) {
                problems.add(name + "the viewport or scissor arrays do not read back at the limit");
            }
            D3d11Tools::reset_state(device);
        }
        {
            std::vector<std::uint8_t> constants(65536, 0);
            float marker[4] = {0.25f, 0.5f, 0.75f, 1.0f};
            std::memcpy(constants.data() + 65536 - 16, marker, 16);
            Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 65536, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, constants.data());
            Com<ID3D11PixelShader> pixel = D3d11Tools::ps(device, "cbuffer C : register(b0) { float4 v[4096]; }\nfloat4 main() : SV_Target { return v[4095]; }", error);
            Com<ID3D11Texture2D> tex = D3d11Tools::target(device, 4, 4, DXGI_FORMAT_R32G32B32A32_FLOAT);
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, tex.get());
            if (!buffer || !pixel || !view) {
                problems.add(name + "a 64 KiB constant buffer or its shader cannot be created: " + error);
            } else {
                ID3D11Buffer* cb = buffer.get();
                device.context->PSSetConstantBuffers(0, 1, &cb);
                if (!D3d11Tools::draw_fullscreen(device, pixel.get(), view.get(), 4, 4, error)) {
                    problems.add(name + error);
                } else {
                    std::vector<std::uint8_t> bytes;
                    if (!D3d11Tools::read_pixels(device, tex.get(), bytes, error) || std::memcmp(bytes.data(), marker, 16) != 0) {
                        problems.add(name + "the last element of a 64 KiB constant buffer was not read back");
                    }
                }
            }
            D3d11Tools::reset_state(device);
        }
        if (limits.uavs > 0) {
            std::string source;
            std::string body = "[numthreads(1,1,1)] void main() {";
            std::vector<Com<ID3D11Buffer>> buffers(limits.uavs);
            std::vector<Com<ID3D11UnorderedAccessView>> uavs(limits.uavs);
            std::vector<ID3D11UnorderedAccessView*> raw(limits.uavs);
            for (UINT i = 0; i < limits.uavs; ++i) {
                source += "RWByteAddressBuffer b" + std::to_string(i) + " : register(u" + std::to_string(i) + ");\n";
                body += " b" + std::to_string(i) + ".Store(0, " + std::to_string(1000 + i) + ");";
                buffers[i] = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr);
                uavs[i] = buffers[i] ? D3d11Tools::raw_uav(device, buffers[i].get()) : Com<ID3D11UnorderedAccessView>();
                raw[i] = uavs[i].get();
            }
            body += " }";
            Com<ID3D11ComputeShader> compute = D3d11Tools::cs(device, source + body, error);
            if (!compute) {
                problems.add(name + error);
            } else {
                device.context->CSSetShader(compute.get(), nullptr, 0);
                device.context->CSSetUnorderedAccessViews(0, limits.uavs, raw.data(), nullptr);
                device.context->Dispatch(1, 1, 1);
                for (UINT i = 0; i < limits.uavs; ++i) {
                    const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, buffers[i].get(), error);
                    if (words.empty() || words[0] != 1000 + i) {
                        problems.add(name + "UAV slot " + std::to_string(i) + " was not written");
                        break;
                    }
                }
            }
            D3d11Tools::reset_state(device);
        }
    }
    context.measure("levels_tested", tested);
    if (tested == 0) {
        problems.add("no feature level could be created");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
