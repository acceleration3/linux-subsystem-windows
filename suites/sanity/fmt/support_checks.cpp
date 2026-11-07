#include "sanity/fmt/support_checks.hpp"

#include "sanity/fmt/format_data.hpp"
#include "core/text.hpp"
#include "sanity/fmt/kit.hpp"
#include "support/d3d11_format_runner.hpp"
#include "support/d3d12_format_runner.hpp"
#include "support/d3d12_tools.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/swap_rig.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::fmt {

namespace {

std::vector<int> probe_numbers() {
    std::vector<int> numbers;
    for (int number = 1; number <= 132; ++number) {
        if (number < 116 || number > 129) {
            numbers.push_back(number);
        }
    }
    numbers.push_back(116);
    numbers.push_back(120);
    return numbers;
}

bool is_defined(int number) { return number <= 115 || number >= 130; }

bool view_only_depth(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS || format == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT ||
           format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS || format == DXGI_FORMAT_X24_TYPELESS_G8_UINT;
}

class Limited {
public:
    explicit Limited(Problems& target) : problems_(target) {}

    void add(const std::string& text) {
        if (++count_ <= 40) {
            problems_.add(text);
        }
    }

private:
    Problems& problems_;
    int count_ = 0;
};

constexpr UINT kMaskBits = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_LOAD | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
                           D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_BLENDABLE | D3D11_FORMAT_SUPPORT_DEPTH_STENCIL |
                           D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET | D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;

void use_checks(FormatRunner& runner, Limited& out, const std::string& name, DXGI_FORMAT format, UINT bits) {
    const FormatSpec spec = FormatCodec::spec(format);
    const bool texture2d = (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;
    if (!spec.known || FormatCodec::is_typeless(format) || !texture2d) {
        return;
    }
    std::string error;
    if ((bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0) {
        std::vector<std::uint8_t> bytes;
        const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (!runner.clear(format, 4, 4, zero, bytes, error)) {
            out.add(name + " reports render target support but clearing a target failed: " + error);
        }
        if ((bits & D3D11_FORMAT_SUPPORT_BLENDABLE) != 0 && spec.kind != FormatKind::Uint && spec.kind != FormatKind::Sint) {
            const float dest[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            if (!runner.draw(format, 4, 4, "float4", "float4(1, 0, 0, 0.5)", dest, true, bytes, error)) {
                out.add(name + " reports blendable but a blended draw failed: " + error);
            } else {
                const double want[4] = {0.5, 0.0, 0.5, 0.5};
                std::string why;
                const std::size_t texel = static_cast<std::size_t>(spec.bytes) * 5;
                if (bytes.size() < texel + static_cast<std::size_t>(spec.bytes) || !FormatCodec::matches(spec, bytes.data() + texel, want, why)) {
                    out.add(name + " reports blendable but the blended result is wrong: " + why);
                }
            }
        }
    }
    if ((bits & Kit::kSampleBits) != 0) {
        const std::vector<std::uint8_t> data = FormatData::make(format, 7, 4, 4);
        std::vector<std::uint32_t> words;
        const char* type = spec.kind == FormatKind::Uint ? "uint4" : (spec.kind == FormatKind::Sint ? "int4" : "float4");
        if (!runner.sample(format, format, type, 4, 4, data, words, error)) {
            out.add(name + " reports shader load or sample support but sampling it failed: " + error);
        } else {
            double want[4];
            FormatData::expected_sample(spec, data.data() + 5 * static_cast<std::size_t>(spec.bytes), want);
            std::string why;
            if (!FormatData::sample_matches(spec, want, words.data() + 5 * 4, why)) {
                out.add(name + " samples wrongly: " + why);
            }
        }
    }
}

void d3d11_part(Context& context, Problems& problems) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open_d3d11(adapter, device, error)) {
        problems.add("D3D11: " + error);
        return;
    }
    D3d11FormatRunner runner(device);
    Limited out(problems);
    int exercised = 0;
    for (int number : probe_numbers()) {
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(number);
        const std::string name = "D3D11 " + Kit::number_name(number);
        UINT bits = 0;
        const bool known = is_defined(number) && runner.support(format, bits);
        const bool typeless = FormatCodec::is_typeless(format) || view_only_depth(format);
        const bool video = FormatCodec::is_video(format);
        const bool depth = FormatCodec::is_depth(format);
        auto create = [&](UINT bind, UINT samples = 1) {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(16, 16, format, bind, D3D11_USAGE_DEFAULT, 0, 1, 1, samples);
            Com<ID3D11Texture2D> texture;
            return SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, texture.put()));
        };
        if (!known) {
            if (create(D3D11_BIND_SHADER_RESOURCE) || create(0)) {
                out.add(name + " is not reported by CheckFormatSupport but a texture was created");
            }
            continue;
        }
        ++exercised;
        const bool texture2d = (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;
        struct Use {
            UINT bind;
            bool bit;
            const char* label;
            bool absent_checked;
        };
        const Use uses[] = {
            {D3D11_BIND_SHADER_RESOURCE, texture2d && (bits & (Kit::kSampleBits | D3D11_FORMAT_SUPPORT_SHADER_GATHER)) != 0, "sample", true},
            {D3D11_BIND_RENDER_TARGET, texture2d && (bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0, "render target", !video},
            {D3D11_BIND_DEPTH_STENCIL, texture2d && (bits & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) != 0, "depth stencil", !video},
            {D3D11_BIND_UNORDERED_ACCESS, texture2d && (bits & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0, "typed UAV", false}};
        for (const Use& use : uses) {
            const bool created = create(use.bind);
            if (use.bit && !created) {
                out.add(name + " reports " + use.label + " support but a texture with that bind flag cannot be created");
            } else if (!use.bit && created && use.absent_checked && !typeless) {
                out.add(name + " reports no " + use.label + " support but a texture with that bind flag was created");
            }
        }
        const bool msaa_bit = (bits & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) != 0;
        bool msaa_worked = false;
        for (UINT count : {2u, 4u, 8u}) {
            UINT levels = 0;
            device.device->CheckMultisampleQualityLevels(format, count, &levels);
            if (levels != 0 && (create(D3D11_BIND_RENDER_TARGET, count) || create(D3D11_BIND_DEPTH_STENCIL, count))) {
                msaa_worked = true;
            }
        }
        if (msaa_bit && !msaa_worked) {
            out.add(name + " reports multisample render target support but no sample count 2, 4 or 8 creates a multisampled target");
        }
        if (!msaa_bit && !typeless && !video && !depth && (bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0 && create(D3D11_BIND_RENDER_TARGET, 4)) {
            out.add(name + " reports no multisample render target support but a 4x multisampled target was created");
        }
        if (!video) {
            use_checks(runner, out, name, format, bits);
        }
    }
    context.measure("fmt.support.d3d11.formats", exercised);
}

void d3d12_part(Context& context, Problems& problems) {
    D3d12Rig rig;
    std::string error;
    if (!Kit::open_d3d12(rig, error)) {
        problems.add("D3D12: " + error);
        return;
    }
    D3d12FormatRunner runner(rig);
    Limited out(problems);
    int exercised = 0;
    for (int number : probe_numbers()) {
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(number);
        const std::string name = "D3D12 " + Kit::number_name(number);
        UINT bits = 0;
        const bool known = is_defined(number) && runner.support(format, bits);
        const bool typeless = FormatCodec::is_typeless(format) || view_only_depth(format);
        const bool video = FormatCodec::is_video(format);
        const bool depth = FormatCodec::is_depth(format);
        auto create = [&](D3D12_RESOURCE_FLAGS flags, UINT samples = 1) {
            return D3d12Tools::texture(rig, D3d12Tools::texture_desc(16, 16, format, flags, 1, 1, samples), D3D12_RESOURCE_STATE_COMMON) != nullptr;
        };
        if (!known) {
            if (create(D3D12_RESOURCE_FLAG_NONE)) {
                out.add(name + " is not reported by CheckFeatureSupport but a texture was created");
            }
            continue;
        }
        ++exercised;
        const bool texture2d = (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;
        if (texture2d && !create(D3D12_RESOURCE_FLAG_NONE)) {
            out.add(name + " reports Texture2D support but a texture cannot be created");
        }
        if (!texture2d && create(D3D12_RESOURCE_FLAG_NONE) && !typeless) {
            out.add(name + " reports no Texture2D support but a texture was created");
        }
        if (!texture2d || video) {
            continue;
        }
        const bool rt_bit = (bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
        const bool ds_bit = (bits & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) != 0;
        const bool uav_bit = (bits & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0;
        const bool rt_created = create(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        const bool ds_created = create(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        if (rt_bit && !rt_created) {
            out.add(name + " reports render target support but a render target texture cannot be created");
        }
        if (!rt_bit && rt_created && !typeless) {
            out.add(name + " reports no render target support but a render target texture was created");
        }
        if (ds_bit && !ds_created) {
            out.add(name + " reports depth stencil support but a depth stencil texture cannot be created");
        }
        if (!ds_bit && ds_created && !typeless) {
            out.add(name + " reports no depth stencil support but a depth stencil texture was created");
        }
        if (uav_bit && !create(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
            out.add(name + " reports typed UAV support but an unordered access texture cannot be created");
        }
        const bool msaa_bit = (bits & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) != 0;
        bool msaa_worked = false;
        for (UINT count : {2u, 4u, 8u}) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
            levels.Format = format;
            levels.SampleCount = count;
            if (SUCCEEDED(rig.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))) &&
                levels.NumQualityLevels != 0 &&
                (create(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, count) || create(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, count))) {
                msaa_worked = true;
            }
        }
        if (msaa_bit && !msaa_worked) {
            out.add(name + " reports multisample render target support but no sample count 2, 4 or 8 creates a multisampled target");
        }
        if (!msaa_bit && !typeless && !depth && rt_bit && create(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 4)) {
            out.add(name + " reports no multisample render target support but a 4x multisampled target was created");
        }
        use_checks(runner, out, name, format, bits);
    }
    context.measure("fmt.support.d3d12.formats", exercised);
}

struct NineFormat {
    D3DFORMAT format;
    const char* name;
};

std::vector<NineFormat> nine_formats() {
    std::vector<NineFormat> out = {
        {D3DFMT_X8R8G8B8, "X8R8G8B8"}, {D3DFMT_A8R8G8B8, "A8R8G8B8"}, {D3DFMT_R5G6B5, "R5G6B5"}, {D3DFMT_X1R5G5B5, "X1R5G5B5"},
        {D3DFMT_A1R5G5B5, "A1R5G5B5"}, {D3DFMT_A4R4G4B4, "A4R4G4B4"}, {D3DFMT_A8, "A8"}, {D3DFMT_L8, "L8"}, {D3DFMT_A8L8, "A8L8"},
        {D3DFMT_L16, "L16"}, {D3DFMT_A8B8G8R8, "A8B8G8R8"}, {D3DFMT_X8B8G8R8, "X8B8G8R8"}, {D3DFMT_A2B10G10R10, "A2B10G10R10"},
        {D3DFMT_A2R10G10B10, "A2R10G10B10"}, {D3DFMT_G16R16, "G16R16"}, {D3DFMT_A16B16G16R16, "A16B16G16R16"},
        {D3DFMT_R16F, "R16F"}, {D3DFMT_G16R16F, "G16R16F"}, {D3DFMT_A16B16G16R16F, "A16B16G16R16F"}, {D3DFMT_R32F, "R32F"},
        {D3DFMT_G32R32F, "G32R32F"}, {D3DFMT_A32B32G32R32F, "A32B32G32R32F"}, {D3DFMT_V8U8, "V8U8"}, {D3DFMT_Q8W8V8U8, "Q8W8V8U8"},
        {D3DFMT_V16U16, "V16U16"}, {D3DFMT_Q16W16V16U16, "Q16W16V16U16"}, {D3DFMT_DXT1, "DXT1"}, {D3DFMT_DXT2, "DXT2"},
        {D3DFMT_DXT3, "DXT3"}, {D3DFMT_DXT4, "DXT4"}, {D3DFMT_DXT5, "DXT5"}, {D3DFMT_D16, "D16"}, {D3DFMT_D24S8, "D24S8"},
        {D3DFMT_D24X8, "D24X8"}, {D3DFMT_D32, "D32"}, {D3DFMT_D15S1, "D15S1"}, {D3DFMT_D24X4S4, "D24X4S4"},
        {D3DFMT_D16_LOCKABLE, "D16_LOCKABLE"}, {D3DFMT_D32F_LOCKABLE, "D32F_LOCKABLE"}, {D3DFMT_D24FS8, "D24FS8"},
        {static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z')), "INTZ"}, {static_cast<D3DFORMAT>(MAKEFOURCC('N', 'U', 'L', 'L')), "NULL"},
        {D3DFMT_YUY2, "YUY2"}, {D3DFMT_UYVY, "UYVY"}, {D3DFMT_R8G8_B8G8, "R8G8_B8G8"}, {D3DFMT_G8R8_G8B8, "G8R8_G8B8"}};
    return out;
}

void d3d9_part(Context& context, Problems& problems) {
    D3d9Rig rig;
    std::string error;
    if (!rig.open(false, error) || !rig.create(D3d9Settings{}, error)) {
        problems.add("D3D9: " + error);
        return;
    }
    IDirect3D9* d3d = rig.d3d.get();
    IDirect3DDevice9* device = rig.dev();
    const D3DFORMAT adapter_format = D3DFMT_X8R8G8B8;
    Limited out(problems);
    int unreported = 0;
    int odd_query_bits = 0;
    int exercised = 0;
    for (const NineFormat& item : nine_formats()) {
        const std::string name = std::string("D3D9 ") + item.name;
        const auto reported = [&](DWORD usage, D3DRESOURCETYPE type) {
            return d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, adapter_format, usage, type, item.format) == D3D_OK;
        };
        const auto texture = [&](DWORD usage, UINT levels = 1) {
            Com<IDirect3DTexture9> out_texture;
            return SUCCEEDED(device->CreateTexture(8, 8, levels, usage, item.format, D3DPOOL_DEFAULT, out_texture.put(), nullptr));
        };
        const bool is_depth = item.format == D3DFMT_D16 || item.format == D3DFMT_D24S8 || item.format == D3DFMT_D24X8 ||
                              item.format == D3DFMT_D32 || item.format == D3DFMT_D15S1 || item.format == D3DFMT_D24X4S4 ||
                              item.format == D3DFMT_D16_LOCKABLE || item.format == D3DFMT_D32F_LOCKABLE || item.format == D3DFMT_D24FS8;
        const bool texture_reported = reported(0, D3DRTYPE_TEXTURE);
        const bool rt_texture_reported = reported(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE);
        const bool rt_surface_reported = reported(D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE);
        const bool ds_reported = reported(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE);
        ++exercised;
        if (texture_reported && !texture(0)) {
            out.add(name + " is reported as a texture format but CreateTexture fails");
        } else if (!texture_reported && !is_depth && texture(0)) {
            ++unreported;
        }
        if (rt_texture_reported && !texture(D3DUSAGE_RENDERTARGET)) {
            out.add(name + " is reported as a render target texture format but CreateTexture with D3DUSAGE_RENDERTARGET fails");
        }
        if (!rt_texture_reported && texture(D3DUSAGE_RENDERTARGET)) {
            out.add(name + " is not reported as a render target texture format but CreateTexture with D3DUSAGE_RENDERTARGET succeeded");
        }
        Com<IDirect3DSurface9> surface;
        const bool rt_created = SUCCEEDED(device->CreateRenderTarget(8, 8, item.format, D3DMULTISAMPLE_NONE, 0, FALSE, surface.put(), nullptr));
        if (rt_surface_reported && !rt_created) {
            out.add(name + " is reported as a render target format but CreateRenderTarget fails");
        }
        if (!rt_surface_reported && rt_created) {
            out.add(name + " is not reported as a render target format but CreateRenderTarget succeeded");
        }
        surface.reset();
        const bool ds_created = SUCCEEDED(device->CreateDepthStencilSurface(8, 8, item.format, D3DMULTISAMPLE_NONE, 0, FALSE, surface.put(), nullptr));
        if (ds_reported && !ds_created) {
            out.add(name + " is reported as a depth stencil format but CreateDepthStencilSurface fails");
        }
        if (!ds_reported && ds_created) {
            out.add(name + " is not reported as a depth stencil format but CreateDepthStencilSurface succeeded");
        }
        if (reported(D3DUSAGE_AUTOGENMIPMAP, D3DRTYPE_TEXTURE) && !texture(D3DUSAGE_AUTOGENMIPMAP, 0)) {
            out.add(name + " reports automatic mipmap generation but CreateTexture with D3DUSAGE_AUTOGENMIPMAP fails");
        }
        if ((reported(D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE) && !texture_reported) ||
            (reported(D3DUSAGE_QUERY_SRGBWRITE, D3DRTYPE_TEXTURE) && !rt_texture_reported && !rt_surface_reported) ||
            (reported(D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE) && !rt_texture_reported && !texture_reported)) {
            ++odd_query_bits;
            context.note(name + " reports an sRGB or blending query bit for a usage it does not support");
        }
    }
    context.measure("fmt.support.d3d9.formats", exercised);
    context.measure("fmt.support.d3d9.accepted_unreported_textures", unreported);
    context.measure("fmt.support.d3d9.odd_query_bits", odd_query_bits);
}

std::string bits_text(UINT bits) {
    return "0x" + Text::hex(bits, 8);
}

std::string clear_values_text(const FormatSpec& spec) {
    return spec.kind == FormatKind::Uint || spec.kind == FormatKind::Sint ? "integer" : "float";
}

}  // namespace

Outcome SupportChecks::cross_api_equal(Context& context) {
    if (!Kit::enabled("d3d11") || !Kit::enabled("d3d12")) {
        return Context::skip("the row compares D3D11 with D3D12 and one of them is excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    D3d11Device device;
    D3d12Rig rig;
    std::string error;
    if (!Kit::open_d3d11(adapter, device, error)) {
        return Outcome::fail("D3D11: " + error);
    }
    if (!Kit::open_d3d12(rig, error)) {
        return Outcome::fail("D3D12: " + error);
    }
    D3d11FormatRunner a(device);
    D3d12FormatRunner b(rig);
    Problems problems;
    Limited out(problems);
    int compared = 0;
    int rendered = 0;
    for (int number : probe_numbers()) {
        if (!is_defined(number)) {
            continue;
        }
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(number);
        UINT bits_a = 0;
        UINT bits_b = 0;
        const bool known_a = a.support(format, bits_a);
        const bool known_b = b.support(format, bits_b);
        const std::string name = Kit::number_name(number);
        if (known_a != known_b) {
            out.add(name + ": D3D11 " + (known_a ? "supports" : "rejects") + " the format and D3D12 " + (known_b ? "supports" : "rejects") + " it");
            continue;
        }
        if (!known_a) {
            continue;
        }
        ++compared;
        if ((bits_a & kMaskBits) != (bits_b & kMaskBits)) {
            out.add(name + ": support bits differ, D3D11 " + bits_text(bits_a & kMaskBits) + " D3D12 " + bits_text(bits_b & kMaskBits));
        }
        const FormatSpec spec = FormatCodec::spec(format);
        const UINT shared = bits_a & bits_b;
        if (!spec.known || FormatCodec::is_typeless(format) || (shared & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
            continue;
        }
        if ((shared & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0) {
            ++rendered;
            std::vector<std::uint8_t> bytes_a;
            std::vector<std::uint8_t> bytes_b;
            std::string error_a;
            std::string error_b;
            const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
            const bool integer = spec.kind == FormatKind::Uint || spec.kind == FormatKind::Sint;
            const std::string expression = integer ? "uint4(1, 2, 3, 4)" : "float4(0.25, 0.5, 0.75, 1.0)";
            const std::string type = integer ? "uint4" : "float4";
            const bool clear_a = integer || a.clear(format, 8, 8, color, bytes_a, error_a);
            const bool clear_b = integer || b.clear(format, 8, 8, color, bytes_b, error_b);
            if (!integer && (clear_a != clear_b || (clear_a && bytes_a != bytes_b))) {
                out.add(name + ": a cleared " + clear_values_text(spec) + " target differs between D3D11 and D3D12" +
                        (clear_a && clear_b ? " at byte " + std::to_string(Kit::first_difference(bytes_a, bytes_b)) : " (" + error_a + error_b + ")"));
            }
            const bool draw_a = a.draw(format, 8, 8, type, expression, nullptr, false, bytes_a, error_a);
            const bool draw_b = b.draw(format, 8, 8, type, expression, nullptr, false, bytes_b, error_b);
            if (draw_a != draw_b || (draw_a && bytes_a != bytes_b)) {
                out.add(name + ": a drawn target differs between D3D11 and D3D12" +
                        (draw_a && draw_b ? " at byte " + std::to_string(Kit::first_difference(bytes_a, bytes_b)) : " (" + error_a + error_b + ")"));
            }
        }
        if ((shared & Kit::kSampleBits) != 0) {
            const std::vector<std::uint8_t> data = FormatData::make(format, 31, 8, 8);
            const char* type = spec.kind == FormatKind::Uint ? "uint4" : (spec.kind == FormatKind::Sint ? "int4" : "float4");
            std::vector<std::uint32_t> words_a;
            std::vector<std::uint32_t> words_b;
            std::string error_a;
            std::string error_b;
            const bool up_a = a.sample(format, format, type, 8, 8, data, words_a, error_a);
            const bool up_b = b.sample(format, format, type, 8, 8, data, words_b, error_b);
            if (up_a != up_b || (up_a && words_a != words_b)) {
                out.add(name + ": the same upload and sample gives different values in D3D11 and D3D12 (" + error_a + error_b + ")");
            }
            std::vector<std::uint8_t> copy_a;
            std::vector<std::uint8_t> copy_b;
            if (a.roundtrip(format, 8, 8, data, copy_a, error_a) && b.roundtrip(format, 8, 8, data, copy_b, error_b) &&
                (copy_a != data || copy_b != data)) {
                out.add(name + ": the copy round trip is not exact (D3D11 " + std::string(copy_a == data ? "exact" : "differs") + ", D3D12 " +
                        (copy_b == data ? "exact" : "differs") + ")");
            }
        }
    }
    context.measure("fmt.cross.compared", compared);
    context.measure("fmt.cross.rendered", rendered);
    return problems.outcome();
}

Outcome SupportChecks::support_bits_match_use(Context& context) {
    Problems problems;
    bool exercised = false;
    std::vector<std::string> skips;
    if (Kit::enabled("d3d11")) {
        d3d11_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        d3d12_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    if (Kit::enabled("d3d9")) {
        d3d9_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D9 excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::fmt
