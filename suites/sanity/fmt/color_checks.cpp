#include "sanity/fmt/color_checks.hpp"

#include "core/text.hpp"
#include "sanity/fmt/format_data.hpp"
#include "sanity/fmt/kit.hpp"
#include "support/d3d11_format_runner.hpp"
#include "support/d3d12_format_runner.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::fmt {

namespace {

using Bytes = std::vector<std::uint8_t>;

class Limited {
public:
    explicit Limited(Problems& target) : problems_(target) {}

    void add(const std::string& text) {
        if (++count_ <= 30) {
            problems_.add(text);
        }
    }

private:
    Problems& problems_;
    int count_ = 0;
};

std::string linear_table() {
    std::string text = "static const float kLinear[256] = {";
    char buffer[40];
    for (int i = 0; i < 256; ++i) {
        std::snprintf(buffer, sizeof(buffer), "%.9g", FormatCodec::srgb_to_linear(i / 255.0));
        std::string value = buffer;
        if (value.find('.') == std::string::npos && value.find('e') == std::string::npos) {
            value += ".0";
        }
        text += value + (i < 255 ? ", " : "");
    }
    return text + "};\n";
}

int encode(double linear) {
    return static_cast<int>(std::lround(FormatCodec::linear_to_srgb(std::min(std::max(linear, 0.0), 1.0)) * 255.0));
}

double step_limit(int code) {
    const double high = FormatCodec::srgb_to_linear(std::min(code + 1, 255) / 255.0);
    const double low = FormatCodec::srgb_to_linear(std::max(code - 1, 0) / 255.0);
    return high - low;
}

struct Table {
    Bytes encoded;
    std::vector<float> decoded;
};

void runner_srgb(FormatRunner& runner, Limited& out, Table& table, int& tested) {
    UINT bits = 0;
    if (!runner.support(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, bits) || (bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0) {
        out.add(std::string(runner.label()) + " R8G8B8A8_UNORM_SRGB is not a render target format");
        return;
    }
    ++tested;
    const std::string name = std::string(runner.label()) + " R8G8B8A8_UNORM_SRGB";
    std::string error;
    Bytes back;
    const std::string expression = "float4(kLinear[uint(pos.x)], kLinear[uint(pos.x)], kLinear[uint(pos.x)], 1)";
    if (!runner.draw(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 256, 1, "float4", expression, nullptr, false, back, error, linear_table())) {
        out.add(name + " draw failed: " + error);
    } else {
        table.encoded = back;
        int worst = 0;
        int reported = 0;
        for (int i = 0; i < 256; ++i) {
            for (int c = 0; c < 3; ++c) {
                const int got = back[static_cast<std::size_t>(i) * 4 + static_cast<std::size_t>(c)];
                worst = std::max(worst, std::abs(got - i));
                if (std::abs(got - i) > 1 && reported < 5) {
                    out.add(name + " wrote the linear value of code " + std::to_string(i) + " as " + std::to_string(got) + " in channel " + std::to_string(c));
                    ++reported;
                }
            }
            if (back[static_cast<std::size_t>(i) * 4 + 3] != 255) {
                out.add(name + " wrote alpha 1.0 as " + std::to_string(back[static_cast<std::size_t>(i) * 4 + 3]));
                break;
            }
        }
    }
    Bytes codes(256 * 4);
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 4; ++c) {
            codes[static_cast<std::size_t>(i) * 4 + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(i);
        }
    }
    std::vector<std::uint32_t> words;
    if (!runner.sample(DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "float4", 256, 1, codes, words, error)) {
        out.add(name + " sampling through an sRGB view failed: " + error);
    } else {
        table.decoded.resize(256);
        int reported = 0;
        for (int i = 0; i < 256; ++i) {
            float values[4];
            std::memcpy(values, &words[static_cast<std::size_t>(i) * 4], 16);
            table.decoded[static_cast<std::size_t>(i)] = values[0];
            const double want = FormatCodec::srgb_to_linear(i / 255.0);
            for (int c = 0; c < 3; ++c) {
                if (!(std::fabs(static_cast<double>(values[c]) - want) <= step_limit(i)) && reported < 5) {
                    out.add(name + " sampled code " + std::to_string(i) + " as " + std::to_string(values[c]) + " in channel " + std::to_string(c) +
                            ", the sRGB transfer function gives " + std::to_string(want));
                    ++reported;
                }
            }
            if (std::fabs(static_cast<double>(values[3]) - i / 255.0) > 1e-5 && reported < 5) {
                out.add(name + " converted the alpha code " + std::to_string(i) + " to " + std::to_string(values[3]) + ", alpha must stay linear");
                ++reported;
            }
        }
    }
    const double sources[] = {0.2, 0.8};
    const double destinations[] = {0.6, 0.1};
    for (double s : sources) {
        for (double d : destinations) {
            const float clear[4] = {static_cast<float>(d), static_cast<float>(d), static_cast<float>(d), 1.0f};
            const std::string expr = "float4(" + std::to_string(s) + ", " + std::to_string(s) + ", " + std::to_string(s) + ", 0.5)";
            if (!runner.draw(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 4, "float4", expr, clear, true, back, error)) {
                out.add(name + " blended draw failed: " + error);
                continue;
            }
            const int want = encode(0.5 * s + 0.5 * d);
            const int got = back[5 * 4];
            if (std::abs(got - want) > 1) {
                out.add(name + " blended source " + std::to_string(s) + " over destination " + std::to_string(d) + " into code " + std::to_string(got) +
                        ", blending in linear space gives " + std::to_string(want) + " (blending in encoded space would give " +
                        std::to_string(encode(s) / 2 + encode(d) / 2) + ")");
            }
            if (std::abs(static_cast<int>(back[5 * 4 + 3]) - 128) > 1) {
                out.add(name + " blended alpha 0.5 into " + std::to_string(back[5 * 4 + 3]));
            }
        }
    }
    const float half[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    if (runner.clear(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 4, half, back, error)) {
        if (std::abs(static_cast<int>(back[5 * 4]) - encode(0.5)) > 1) {
            out.add(name + " cleared linear 0.5 to code " + std::to_string(back[5 * 4]) + ", expected " + std::to_string(encode(0.5)));
        }
        if (std::abs(static_cast<int>(back[5 * 4 + 3]) - 128) > 1) {
            out.add(name + " cleared alpha 0.5 to " + std::to_string(back[5 * 4 + 3]));
        }
    } else {
        out.add(name + " clear failed: " + error);
    }
}

void nine_srgb(Limited& out, Table& table, bool& ran) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.width = 256;
    settings.height = 16;
    settings.depth = false;
    if (!rig.open(false, error) || !rig.create(settings, error)) {
        out.add("D3D9: " + error);
        return;
    }
    IDirect3D9* d3d = rig.d3d.get();
    IDirect3DDevice9* device = rig.dev();
    const bool write_ok = d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_SRGBWRITE,
                                                 D3DRTYPE_SURFACE, D3DFMT_A8R8G8B8) == D3D_OK;
    const bool read_ok = d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE,
                                                D3DFMT_A8R8G8B8) == D3D_OK;
    if (!write_ok && !read_ok) {
        return;
    }
    ran = true;
    Com<IDirect3DSurface9> saved;
    device->GetRenderTarget(0, saved.put());
    if (write_ok) {
        HRESULT result = S_OK;
        Com<IDirect3DSurface9> rt = D3d9Tools::target(device, 256, 1, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, false, &result);
        if (!rt) {
            out.add("D3D9: the sRGB render target cannot be created: " + D3d9Tools::hr(result));
        } else {
            device->SetRenderTarget(0, rt.get());
            D3d9Tools::prepare(device, false);
            device->SetRenderState(D3DRS_SRGBWRITEENABLE, TRUE);
            D3d9Tools::clear(device, 0);
            bool drawn = true;
            for (unsigned i = 0; i < 256 && drawn; ++i) {
                drawn = D3d9Tools::draw_quad(device, static_cast<float>(i), 0, static_cast<float>(i + 1), 1, D3DCOLOR_ARGB(255, i, i, i));
            }
            SceneImage image;
            device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            if (!drawn || !D3d9Tools::read_surface(device, rt.get(), image, error)) {
                out.add("D3D9: drawing with D3DRS_SRGBWRITEENABLE failed: " + error);
            } else {
                int reported = 0;
                for (int i = 0; i < 256; ++i) {
                    const std::uint32_t pixel = D3d9Tools::pixel(image, static_cast<UINT>(i), 0);
                    const int channels[3] = {static_cast<int>((pixel >> 16) & 255), static_cast<int>((pixel >> 8) & 255), static_cast<int>(pixel & 255)};
                    const int want = encode(i / 255.0);
                    for (int c = 0; c < 3; ++c) {
                        if (std::abs(channels[c] - want) > 1 && reported < 5) {
                            out.add("D3D9 SRGBWRITEENABLE wrote the linear value " + std::to_string(i) + "/255 as code " + std::to_string(channels[c]) + ", expected " +
                                    std::to_string(want));
                            ++reported;
                        }
                    }
                }
            }
            device->SetRenderTarget(0, saved.get());
        }
    }
    if (read_ok) {
        HRESULT result = S_OK;
        Com<IDirect3DTexture9> texture = D3d9Tools::texture(device, 256, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &result);
        Com<IDirect3DSurface9> rt = D3d9Tools::target(device, 256, 1, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, false, &result);
        D3DLOCKED_RECT locked{};
        if (!texture || !rt || FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
            out.add("D3D9: the sRGB read resources cannot be created: " + D3d9Tools::hr(result));
        } else {
            auto* row = static_cast<std::uint32_t*>(locked.pBits);
            for (std::uint32_t i = 0; i < 256; ++i) {
                row[i] = 0xFF000000u | (i << 16) | (i << 8) | i;
            }
            texture->UnlockRect(0);
            device->SetRenderTarget(0, rt.get());
            D3d9Tools::prepare(device, true);
            device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, TRUE);
            device->SetTexture(0, texture.get());
            const bool drawn = D3d9Tools::draw_textured(device, 0, 0, 256, 1, 0, 0, 1, 1);
            device->SetTexture(0, nullptr);
            device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
            SceneImage image;
            if (!drawn || !D3d9Tools::read_surface(device, rt.get(), image, error)) {
                out.add("D3D9: sampling with D3DSAMP_SRGBTEXTURE failed: " + error);
            } else {
                int reported = 0;
                for (int i = 0; i < 256; ++i) {
                    const std::uint32_t pixel = D3d9Tools::pixel(image, static_cast<UINT>(i), 0);
                    const int got = static_cast<int>(pixel & 255);
                    const int want = static_cast<int>(std::lround(FormatCodec::srgb_to_linear(i / 255.0) * 255.0));
                    if (std::abs(got - want) > 1 && reported < 5) {
                        out.add("D3D9 SRGBTEXTURE sampled code " + std::to_string(i) + " as " + std::to_string(got) + " in an 8 bit target, expected " + std::to_string(want));
                        ++reported;
                    }
                }
            }
            device->SetRenderTarget(0, saved.get());
        }
    }
    static_cast<void>(table);
}

void compare_tables(Limited& out, const char* a_name, const Table& a, const char* b_name, const Table& b) {
    if (a.encoded.size() != b.encoded.size() || a.encoded.empty()) {
        return;
    }
    int worst = 0;
    for (std::size_t i = 0; i < a.encoded.size(); ++i) {
        worst = std::max(worst, std::abs(static_cast<int>(a.encoded[i]) - static_cast<int>(b.encoded[i])));
    }
    if (worst > 1) {
        out.add(std::string(a_name) + " and " + b_name + " encode linear values differently by up to " + std::to_string(worst) + " codes");
    }
}

struct CastFamily {
    DXGI_FORMAT typeless;
    std::vector<DXGI_FORMAT> views;
};

std::vector<CastFamily> families() {
    return {{DXGI_FORMAT_R8G8B8A8_TYPELESS,
             {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}},
            {DXGI_FORMAT_B8G8R8A8_TYPELESS, {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}},
            {DXGI_FORMAT_R16G16_TYPELESS,
             {DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16_FLOAT}},
            {DXGI_FORMAT_R16_TYPELESS,
             {DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_SNORM, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16_FLOAT}},
            {DXGI_FORMAT_R8G8_TYPELESS, {DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_SNORM, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_SINT}},
            {DXGI_FORMAT_R8_TYPELESS, {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_SNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT}},
            {DXGI_FORMAT_R32_TYPELESS, {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT}},
            {DXGI_FORMAT_R32G32_TYPELESS, {DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_SINT}},
            {DXGI_FORMAT_R32G32B32A32_TYPELESS, {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_SINT}},
            {DXGI_FORMAT_R16G16B16A16_TYPELESS,
             {DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT,
              DXGI_FORMAT_R16G16B16A16_FLOAT}},
            {DXGI_FORMAT_R10G10B10A2_TYPELESS, {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UINT}}};
}

void runner_casts(FormatRunner& runner, Limited& out, int& tested) {
    for (const CastFamily& family : families()) {
        UINT typeless_bits = 0;
        if (!runner.support(family.typeless, typeless_bits)) {
            continue;
        }
        for (DXGI_FORMAT view : family.views) {
            UINT bits = 0;
            if (!runner.support(view, bits) || (bits & Kit::kSampleBits) == 0) {
                continue;
            }
            ++tested;
            const FormatSpec spec = FormatCodec::spec(view);
            const std::string name = std::string(runner.label()) + " " + Kit::number_name(static_cast<int>(family.typeless)) + " viewed as " + Kit::number_name(static_cast<int>(view));
            const Bytes data = FormatData::make(view, 0x900 + static_cast<std::uint64_t>(view), 8, 4);
            const char* type = spec.kind == FormatKind::Uint ? "uint4" : (spec.kind == FormatKind::Sint ? "int4" : "float4");
            std::vector<std::uint32_t> words;
            std::string error;
            if (!runner.sample(family.typeless, view, type, 8, 4, data, words, error)) {
                out.add(name + " cannot be sampled: " + error);
                continue;
            }
            for (unsigned t = 0; t < 32; ++t) {
                double want[4];
                FormatData::expected_sample(spec, data.data() + static_cast<std::size_t>(t) * static_cast<std::size_t>(spec.bytes), want);
                std::string why;
                if (!FormatData::sample_matches(spec, want, words.data() + static_cast<std::size_t>(t) * 4, why)) {
                    out.add(name + " texel " + std::to_string(t) + ": " + why);
                    break;
                }
            }
        }
    }
}

void d3d11_refusals(D3d11Device& device, Limited& out) {
    struct Refusal {
        DXGI_FORMAT typeless;
        DXGI_FORMAT view;
    };
    const Refusal refusals[] = {{DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R16G16_UNORM},
                                {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R32_FLOAT},
                                {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM},
                                {DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM},
                                {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R16G16_UNORM},
                                {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R8G8_UNORM},
                                {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R8_UNORM},
                                {DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32_FLOAT}};
    for (const Refusal& refusal : refusals) {
        HRESULT result = S_OK;
        Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, D3d11Tools::desc2d(8, 8, refusal.typeless, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
        if (!texture) {
            continue;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = refusal.view;
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1;
        Com<ID3D11ShaderResourceView> view;
        result = device.device->CreateShaderResourceView(texture.get(), &desc, view.put());
        if (SUCCEEDED(result)) {
            out.add(std::string("D3D11 a ") + Kit::number_name(static_cast<int>(refusal.typeless)) + " texture accepted a shader resource view as " +
                    Kit::number_name(static_cast<int>(refusal.view)) + ", which is not in its format family");
        } else if (result != E_INVALIDARG) {
            out.add(std::string("D3D11 a ") + Kit::number_name(static_cast<int>(refusal.typeless)) + " texture refused a view as " + Kit::number_name(static_cast<int>(refusal.view)) +
                    " with " + D3d11Tools::hr(result) + ", expected E_INVALIDARG");
        }
        if (FormatCodec::spec(refusal.view).known) {
            D3D11_TEXTURE2D_DESC target_desc = D3d11Tools::desc2d(8, 8, refusal.typeless, D3D11_BIND_RENDER_TARGET);
            Com<ID3D11Texture2D> target;
            if (SUCCEEDED(device.device->CreateTexture2D(&target_desc, nullptr, target.put()))) {
                D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
                rtv_desc.Format = refusal.view;
                rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                Com<ID3D11RenderTargetView> rtv;
                if (SUCCEEDED(device.device->CreateRenderTargetView(target.get(), &rtv_desc, rtv.put()))) {
                    out.add(std::string("D3D11 a ") + Kit::number_name(static_cast<int>(refusal.typeless)) + " texture accepted a render target view as " +
                            Kit::number_name(static_cast<int>(refusal.view)) + ", which is not in its format family");
                }
            }
        }
    }
}

}  // namespace

Outcome ColorChecks::srgb_conversion(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    Limited out(problems);
    Table d3d11_table;
    Table d3d12_table;
    Table d3d9_table;
    if (Kit::enabled("d3d11")) {
        AvAdapter adapter;
        D3d11Device device;
        std::string error;
        if (!Kit::open_d3d11(adapter, device, error)) {
            problems.add("D3D11: " + error);
        } else {
            D3d11FormatRunner runner(device);
            int tested = 0;
            runner_srgb(runner, out, d3d11_table, tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        D3d12Rig rig;
        std::string error;
        if (!Kit::open_d3d12(rig, error)) {
            problems.add("D3D12: " + error);
        } else {
            D3d12FormatRunner runner(rig);
            int tested = 0;
            runner_srgb(runner, out, d3d12_table, tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    if (Kit::enabled("d3d9")) {
        bool ran = false;
        nine_srgb(out, d3d9_table, ran);
        if (!ran) {
            skips.push_back("D3D9 reports neither sRGB reads nor sRGB writes for A8R8G8B8");
        } else {
            exercised = true;
        }
    } else {
        skips.push_back("D3D9 excluded");
    }
    compare_tables(out, "D3D11", d3d11_table, "D3D12", d3d12_table);
    return Kit::finish(skips, problems, exercised);
}

Outcome ColorChecks::typeless_casts(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    Limited out(problems);
    if (Kit::enabled("d3d11")) {
        AvAdapter adapter;
        D3d11Device device;
        std::string error;
        if (!Kit::open_d3d11(adapter, device, error)) {
            problems.add("D3D11: " + error);
        } else {
            D3d11FormatRunner runner(device);
            int tested = 0;
            runner_casts(runner, out, tested);
            d3d11_refusals(device, out);
            context.measure("fmt.typeless.d3d11.casts", tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        D3d12Rig rig;
        std::string error;
        if (!Kit::open_d3d12(rig, error)) {
            problems.add("D3D12: " + error);
        } else {
            D3d12FormatRunner runner(rig);
            int tested = 0;
            runner_casts(runner, out, tested);
            context.measure("fmt.typeless.d3d12.casts", tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::fmt
