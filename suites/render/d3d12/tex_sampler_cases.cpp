#include "render/common/cpu_raster.hpp"
#include "render/d3d12/textures.hpp"

#include <cfloat>
#include <cmath>

namespace lsw::suites::render::d3d12 {

namespace {

std::string ps_of(const std::string& decls, const std::string& body) {
    return std::string(kSamplePrefix) + decls + "float4 main(VsOut i) : SV_Target {\n" + body + "\n}\n";
}

const char* kTexDecl = "Texture2D<float4> tex : register(t0); SamplerState smp : register(s0);\n";
const float kTint[4] = {1, 1, 1, 1};
const float kScale4[4] = {4.0f, 4.0f, 0, 0};
const float kScale3[4] = {3.0f, 3.0f, 0, 0};

ID3D12Resource* checker_texture(Kit& kit, std::string& error, UINT size = 128, UINT mips = 8, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, unsigned content = 3) {
    TextureSpec spec;
    spec.width = size;
    spec.height = size;
    spec.mips = mips;
    spec.format = format;
    spec.content = content;
    return TextureFactory::create(kit, spec, true, error);
}

bool run_sampler(Kit& kit, const D3D12_SAMPLER_DESC& sampler, const std::string& body, const float p1[4], float tilt, rr::CaptureSet& out, std::string& error) {
    ID3D12Resource* texture = checker_texture(kit, error);
    return texture != nullptr && sample_scene(kit, texture, srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 8), &sampler, ps_of(kTexDecl, body), kTint, p1, tilt, out, error);
}

constexpr UINT kBandTexels = 128;
constexpr UINT kBandWidth = 8;

std::uint32_t band_color(UINT texel) {
    const UINT band = (texel % kBandTexels) / kBandWidth;
    const std::uint32_t r = 40 + ((band * 97) % 13) * 17;
    const std::uint32_t g = 40 + ((band * 61 + 5) % 11) * 20;
    const std::uint32_t b = 40 + ((band * 29 + 3) % 9) * 25;
    return r | (g << 8) | (b << 16) | (255u << 24);
}

bool draw_minimum_anisotropic(Kit& kit, rr::CaptureSet& out, std::string& error) {
    TextureSpec spec;
    spec.width = kBandTexels;
    spec.height = kBandTexels;
    spec.mips = 1;
    ID3D12Resource* texture = TextureFactory::create(kit, spec, false, error);
    if (texture == nullptr) {
        return false;
    }
    std::vector<std::uint32_t> texels(static_cast<std::size_t>(kBandTexels) * kBandTexels);
    for (UINT y = 0; y < kBandTexels; ++y) {
        for (UINT x = 0; x < kBandTexels; ++x) {
            texels[static_cast<std::size_t>(y) * kBandTexels + x] = band_color(x);
        }
    }
    if (!D3d12Tools::write_texture(kit.rig, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, texels.data(), error)) {
        return false;
    }
    D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(kit.broken("sampler_filter") ? D3D12_FILTER_ANISOTROPIC : D3D12_FILTER_MINIMUM_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    sampler.MaxAnisotropy = 8;
    const float scale[4] = {1.0f, 1.0f, 0, 0};
    return sample_scene(kit, texture, srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1), &sampler, ps_of(kTexDecl, "return tex.Sample(smp, i.uv * p1.xy);"), kTint, scale, -1.0f, out, error);
}

bool minimum_anisotropic_reference(Kit&, unsigned, rr::CaptureSet& out, std::string&) {
    const rr::Mat4 matrix = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -2.4f) * rr::Mat4::rotate_x(-1.0f);
    auto element = [&](int row, int column) { return static_cast<double>(matrix.m[column * 4 + row]); };
    rr::Image image = rr::Image::solid(128, 128, rr::CpuRaster::pack(0.1f, 0.12f, 0.2f));
    for (std::uint32_t py = 0; py < 128; ++py) {
        for (std::uint32_t px = 0; px < 128; ++px) {
            const double xn = (px + 0.5) / 128.0 * 2.0 - 1.0;
            const double yn = 1.0 - (py + 0.5) / 128.0 * 2.0;
            const double a1 = element(0, 0) - xn * element(3, 0);
            const double a2 = element(0, 1) - xn * element(3, 1);
            const double c1 = element(0, 3) - xn * element(3, 3);
            const double b1 = element(1, 0) - yn * element(3, 0);
            const double b2 = element(1, 1) - yn * element(3, 1);
            const double c2 = element(1, 3) - yn * element(3, 3);
            const double det = a1 * b2 - a2 * b1;
            if (std::fabs(det) < 1e-12) {
                continue;
            }
            const double x = (-c1 * b2 + c2 * a2) / det;
            const double y = (-a1 * c2 + b1 * c1) / det;
            const double w = element(3, 0) * x + element(3, 1) * y + element(3, 3);
            const double z = element(2, 0) * x + element(2, 1) * y + element(2, 3);
            if (w <= 0.0 || z / w < 0.0 || z / w > 1.0 || std::fabs(x) > 1.2 || std::fabs(y) > 1.2) {
                continue;
            }
            const double texel = (x + 1.2) / 2.4 * kBandTexels;
            const long first = static_cast<long>(std::floor(texel - 0.5));
            const std::uint32_t a = band_color(static_cast<UINT>((first % 128 + 128) % 128));
            const std::uint32_t b = band_color(static_cast<UINT>(((first + 1) % 128 + 128) % 128));
            std::uint32_t result = 0;
            for (unsigned channel = 0; channel < 4; ++channel) {
                const std::uint32_t ca = (a >> (8 * channel)) & 255u;
                const std::uint32_t cb = (b >> (8 * channel)) & 255u;
                result |= std::min(ca, cb) << (8 * channel);
            }
            image.set_rgba8(px, py, result);
        }
    }
    out.add("", std::move(image));
    return true;
}

void register_filters() {
    Cases::single(rr::CaseSpec("tex", "sampler_filter_minimum_anisotropic").tolerance(rr::Tolerance::blend(3).with_outliers(0.03)).tag("sampler"), draw_minimum_anisotropic, {}, minimum_anisotropic_reference);
    struct Item {
        const char* name;
        D3D12_FILTER filter;
        UINT aniso;
    };
    const Item items[] = {
        {"point", D3D12_FILTER_MIN_MAG_MIP_POINT, 1},
        {"point_mip_linear", D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR, 1},
        {"min_point_mag_linear", D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT, 1},
        {"linear_mip_point", D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, 1},
        {"linear", D3D12_FILTER_MIN_MAG_MIP_LINEAR, 1},
        {"anisotropic_2", D3D12_FILTER_ANISOTROPIC, 2},
        {"anisotropic_4", D3D12_FILTER_ANISOTROPIC, 4},
        {"anisotropic_8", D3D12_FILTER_ANISOTROPIC, 8},
        {"anisotropic_16", D3D12_FILTER_ANISOTROPIC, 16},
        {"minimum_linear", D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, 1},
        {"maximum_linear", D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR, 1},
    };
    for (const Item& item : items) {
        const bool aniso = item.aniso > 1;
        const bool magnified = std::string(item.name) == "min_point_mag_linear";
        Cases::single(rr::CaseSpec("tex", std::string("sampler_filter_") + item.name).tolerance(aniso ? anisotropic_tolerance() : (std::string(item.name).find("point") != std::string::npos || std::string(item.name).find("maximum") != std::string::npos ? texel_edge_tolerance() : sampler_tolerance())).tag("sampler").lod_bracket(0.25f),
                      [item, aniso, magnified](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(item.filter, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
                          sampler.MaxAnisotropy = item.aniso;
                          const float magnify[4] = {0.25f, 0.25f, 0, 0};
                          return run_sampler(kit, sampler, "return tex.Sample(smp, i.uv * p1.xy);", magnified ? magnify : (aniso ? kScale4 : kScale3), aniso ? -1.52f : kFlatTilt, out, error);
                      });
    }
    struct Lod {
        const char* name;
        float bias;
        float min_lod;
        float max_lod;
    };
    const Lod lods[] = {{"mip_bias_plus1_5", 1.5f, 0, FLT_MAX}, {"mip_bias_minus1", -1.0f, 0, FLT_MAX}, {"min_lod_3", 0, 3.0f, FLT_MAX}, {"max_lod_1_5", 0, 0, 1.5f}};
    for (const Lod& lod : lods) {
        Cases::single(rr::CaseSpec("tex", std::string("sampler_") + lod.name).tolerance(sampler_tolerance()).tag("sampler"), [lod](Kit& kit, rr::CaptureSet& out, std::string& error) {
            D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
            sampler.MipLODBias = lod.bias;
            sampler.MinLOD = lod.min_lod;
            sampler.MaxLOD = lod.max_lod;
            return run_sampler(kit, sampler, "return tex.Sample(smp, i.uv * p1.xy);", kScale4, kFlatTilt, out, error);
        });
    }
}

void register_addressing() {
    struct Item {
        const char* name;
        D3D12_TEXTURE_ADDRESS_MODE mode;
        float border[4];
    };
    const Item items[] = {{"wrap", D3D12_TEXTURE_ADDRESS_MODE_WRAP, {0, 0, 0, 0}},
                          {"mirror", D3D12_TEXTURE_ADDRESS_MODE_MIRROR, {0, 0, 0, 0}},
                          {"clamp", D3D12_TEXTURE_ADDRESS_MODE_CLAMP, {0, 0, 0, 0}},
                          {"border_transparent_black", D3D12_TEXTURE_ADDRESS_MODE_BORDER, {0, 0, 0, 0}},
                          {"border_opaque_black", D3D12_TEXTURE_ADDRESS_MODE_BORDER, {0, 0, 0, 1}},
                          {"border_custom_color", D3D12_TEXTURE_ADDRESS_MODE_BORDER, {0.9f, 0.2f, 0.6f, 1}},
                          {"mirror_once", D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE, {0, 0, 0, 0}}};
    for (const Item& item : items) {
        Cases::single(rr::CaseSpec("tex", std::string("sampler_address_") + item.name).tolerance(sampler_tolerance()).tag("sampler"), [item](Kit& kit, rr::CaptureSet& out, std::string& error) {
            D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, item.mode);
            for (int k = 0; k < 4; ++k) {
                sampler.BorderColor[k] = item.border[k];
            }
            const float scale[4] = {2.5f, 2.5f, 0, 0};
            return run_sampler(kit, sampler, "return tex.SampleLevel(smp, i.uv * p1.xy - float2(0.75, 0.75), 0);", scale, kFlatTilt, out, error);
        });
    }
    for (int point = 0; point < 2; ++point) {
        Cases::single(rr::CaseSpec("tex", point != 0 ? "sampler_static_point_wrap" : "sampler_static_linear_clamp").tolerance(point != 0 ? texel_edge_tolerance() : sampler_tolerance()).tag("sampler"),
                      [point](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          ID3D12Resource* texture = checker_texture(kit, error);
                          if (texture == nullptr) {
                              return false;
                          }
                          const std::string decl = std::string("Texture2D<float4> tex : register(t0); SamplerState smp : register(") + (point != 0 ? "s1" : "s2") + ", space1);\n";
                          const float scale[4] = {3.0f, 3.0f, 0, 0};
                          return sample_scene(kit, texture, srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 8), nullptr, ps_of(decl, point != 0 ? "return tex.Sample(smp, i.uv * p1.xy - float2(0.75, 0.25));" : "return tex.Sample(smp, i.uv * p1.xy - float2(1.0, 0.5));"), kTint, scale, kFlatTilt, out, error);
                      });
    }
    Cases::single(rr::CaseSpec("tex", "sampler_two_heap_samplers").tolerance(texel_edge_tolerance()).tag("sampler"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* texture = checker_texture(kit, error, 16, 5);
        if (texture == nullptr) {
            return false;
        }
        const D3D12_SAMPLER_DESC point = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const D3D12_SAMPLER_DESC smooth = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const std::string decl = "Texture2D<float4> tex : register(t0); SamplerState a : register(s0); SamplerState b : register(s1);\n";
        const float scale[4] = {1, 1, 0, 0};
        return sample_scene(kit, texture, srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 5), &point, ps_of(decl, "return i.uv.x < 0.5 ? tex.Sample(a, i.uv) : tex.Sample(b, i.uv);"), kTint, scale, kFlatTilt, out, error, &smooth);
    });
}

struct CompareItem {
    const char* name;
    D3D12_FILTER filter;
    const char* body;
};

void register_comparison() {
    const CompareItem items[] = {
        {"sample_cmp_linear", D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, "float v = tex.SampleCmp(smp, i.uv * 0.5 + 0.2, 0.5); return float4(v, v * 0.5 + 0.25, 1 - v, 1);"},
        {"sample_cmp_point", D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT, "float v = tex.SampleCmp(smp, i.uv * 0.5 + 0.2, 0.5); return float4(v, v * 0.5 + 0.25, 1 - v, 1);"},
        {"sample_cmp_level_zero", D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, "float v = tex.SampleCmpLevelZero(smp, i.uv, 0.35 + 0.4 * i.uv.y); return float4(v, v * 0.5 + 0.25, 1 - v, 1);"},
        {"gather_cmp", D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, "return tex.GatherCmp(smp, i.uv, 0.5 + 0.3 * sin(i.uv.x * 6.0));"},
        {"sample_cmp_gather_red_depth_values", D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT, "return tex.GatherCmpRed(smp, i.uv * 0.5 + 0.25, 0.45);"},
    };
    for (const CompareItem& item : items) {
        Cases::single(rr::CaseSpec("tex", std::string("sampler_") + item.name).tolerance(rr::Tolerance::psnr(34)).tag("sampler"), [item](Kit& kit, rr::CaptureSet& out, std::string& error) {
            TextureSpec spec;
            spec.format = DXGI_FORMAT_R32_FLOAT;
            spec.width = 32;
            spec.height = 32;
            spec.mips = 1;
            spec.content = 1;
            ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
            if (texture == nullptr) {
                return false;
            }
            D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(item.filter, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            const float scale[4] = {1, 1, 0, 0};
            return sample_scene(kit, texture, srv_2d(DXGI_FORMAT_R32_FLOAT, 1), &sampler,
                                ps_of("Texture2D<float> tex : register(t0); SamplerComparisonState smp : register(s0);\n", item.body), kTint, scale, -0.4f, out, error);
        },
        [](Kit& kit) { return needs_format(kit, DXGI_FORMAT_R32_FLOAT, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON, "comparison sampling of R32_FLOAT"); });
    }
}

void register_operations() {
    struct Op {
        const char* name;
        const char* body;
    };
    const Op ops[] = {
        {"sample_level_offset_lod0", "return tex.SampleLevel(smp, i.uv * p1.xy, 0, int2(3, -2));"},
        {"sample_level_offset_lod1", "return tex.SampleLevel(smp, i.uv * p1.xy, 1, int2(3, -2));"},
        {"sample_level_offset_lod2", "return tex.SampleLevel(smp, i.uv * p1.xy, 2, int2(3, -2));"},
        {"sample_level", "return tex.SampleLevel(smp, i.uv * p1.xy, 2.5);"},
        {"sample_grad", "return tex.SampleGrad(smp, i.uv * p1.xy, float2(0.02, 0.0), float2(0.0, 0.06));"},
        {"sample_bias", "return tex.SampleBias(smp, i.uv * p1.xy, 2.0);"},
        {"gather_red", "return tex.GatherRed(smp, i.uv * float2(8, 8));"},
        {"gather_green", "return tex.GatherGreen(smp, i.uv * float2(8, 8));"},
        {"gather_blue", "return tex.GatherBlue(smp, i.uv * float2(8, 8));"},
        {"gather_alpha", "return float4(tex.GatherAlpha(smp, i.uv * float2(8, 8)).xyz, 1);"},
        {"gather_programmable_offsets", "return tex.GatherRed(smp, i.uv * float2(8, 8), int2(1, 0), int2(5, 2), int2(-2, 3), int2(7, -3));"},
        {"load_mip", "uint w, h, l; tex.GetDimensions(2, w, h, l); return tex.Load(int3(i.uv * float2(w, h), 2));"},
        {"load_offset", "return tex.Load(int3(i.uv * 127.0, 0), int2(5, 3));"},
        {"calculate_lod", "float l = tex.CalculateLevelOfDetail(smp, i.uv * p1.xy); return float4(l / 8.0, l / 16.0, 0.2, 1);"},
        {"calculate_lod_unclamped", "float l = tex.CalculateLevelOfDetailUnclamped(smp, i.uv * p1.xy * 0.2); return float4(saturate((l + 5.0) / 12.0), saturate((l + 5.0) / 24.0), 0.2, 1);"},
        {"get_dimensions", "uint w, h, l; tex.GetDimensions(1, w, h, l); return float4(w / 128.0, h / 128.0, l / 8.0, 1) * (0.5 + 0.5 * i.uv.x);"},
    };
    for (const Op& op : ops) {
        Cases::single(rr::CaseSpec("tex", std::string("op_") + op.name).tolerance(std::string(op.name).rfind("sample_", 0) == 0 || std::string(op.name).find("lod") != std::string::npos ? sampler_tolerance() : exact_op_tolerance()).tag("sampler"), [op](Kit& kit, rr::CaptureSet& out, std::string& error) {
            const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
            const bool lod_value = std::string(op.name).find("calculate_lod") != std::string::npos;
            return run_sampler(kit, sampler, op.body, kScale4, lod_value ? -0.9f : kFlatTilt, out, error);
        });
    }
}

void register_views() {
    struct Item {
        const char* name;
        std::function<void(D3D12_SHADER_RESOURCE_VIEW_DESC&)> tweak;
    };
    const Item items[] = {
        {"srv_min_lod_clamp", [](D3D12_SHADER_RESOURCE_VIEW_DESC& d) { d.Texture2D.ResourceMinLODClamp = 3.0f; }},
        {"srv_mip_subrange", [](D3D12_SHADER_RESOURCE_VIEW_DESC& d) { d.Texture2D.MostDetailedMip = 2; d.Texture2D.MipLevels = 3; }},
        {"srv_single_mip", [](D3D12_SHADER_RESOURCE_VIEW_DESC& d) { d.Texture2D.MostDetailedMip = 4; d.Texture2D.MipLevels = 1; }},
        {"srv_swizzle_bgra", [](D3D12_SHADER_RESOURCE_VIEW_DESC& d) { d.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(2, 1, 0, 3); }},
        {"srv_swizzle_force_values", [](D3D12_SHADER_RESOURCE_VIEW_DESC& d) {
             d.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1, 1, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0, 0);
         }},
        {"srv_swizzle_replicate_green", [](D3D12_SHADER_RESOURCE_VIEW_DESC& d) { d.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(1, 1, 1, 3); }},
    };
    for (const Item& item : items) {
        Cases::single(rr::CaseSpec("tex", item.name).tolerance(sampler_tolerance()).tag("sampler"), [item](Kit& kit, rr::CaptureSet& out, std::string& error) {
            ID3D12Resource* texture = checker_texture(kit, error, 128, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
            if (texture == nullptr) {
                return false;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 8);
            item.tweak(srv);
            const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
            return sample_scene(kit, texture, srv, &sampler, ps_of(kTexDecl, "return tex.Sample(smp, i.uv * p1.xy);"), kTint, kScale4, kFlatTilt, out, error);
        });
    }
    Cases::single(rr::CaseSpec("tex", "srv_array_slice_subrange").tolerance(sampler_tolerance()).tag("sampler"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.width = 32;
        spec.height = 32;
        spec.depth_or_array = 4;
        spec.mips = 1;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
        if (texture == nullptr) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2DArray.MipLevels = 1;
        srv.Texture2DArray.FirstArraySlice = 1;
        srv.Texture2DArray.ArraySize = 2;
        const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
        const float scale[4] = {1, 1, 0, 0};
        return sample_scene(kit, texture, srv, &sampler,
                            ps_of("Texture2DArray<float4> tex : register(t0); SamplerState smp : register(s0);\n", "return tex.Sample(smp, float3(i.uv * 2.0, i.uv.x < 0.5 ? 0 : 1));"), kTint, scale, -0.4f, out, error);
    });
}

const struct Registrar {
    Registrar() {
        register_filters();
        register_addressing();
        register_comparison();
        register_operations();
        register_views();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
