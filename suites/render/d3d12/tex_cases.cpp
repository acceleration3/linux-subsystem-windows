#include "render/d3d12/textures.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

std::string ps_of(const std::string& decls, const std::string& body) {
    return std::string(kSamplePrefix) + decls + "float4 main(VsOut i) : SV_Target {\n" + body + "\n}\n";
}

const float kScale[4] = {2.0f, 2.0f, 0, 0};
const float kTint[4] = {1, 1, 1, 1};
const float kScaleOne[4] = {1.0f, 1.0f, 0, 0};

D3D12_SAMPLER_DESC linear_wrap() { return Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP); }

void register_dimensions() {
    Cases::single(rr::CaseSpec("tex", "dim_texture1d").tolerance(sampler_tolerance()).tag("dimension"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        spec.width = 64;
        spec.height = 1;
        spec.mips = 7;
        spec.content = 1;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
        if (texture == nullptr) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture1D.MipLevels = 7;
        const D3D12_SAMPLER_DESC sampler = linear_wrap();
        return sample_scene(kit, texture, srv, &sampler, ps_of("Texture1D<float4> tex : register(t0); SamplerState smp : register(s0);\n", "return tex.Sample(smp, i.uv.x * 3.0) * float4(1, 1, 1, 1);"),
                            kTint, kScaleOne, kFlatTilt, out, error);
    });
    Cases::single(rr::CaseSpec("tex", "dim_texture1d_array").tolerance(sampler_tolerance()).tag("dimension"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        spec.width = 64;
        spec.height = 1;
        spec.depth_or_array = 4;
        spec.content = 0;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
        if (texture == nullptr) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture1DArray.MipLevels = 1;
        srv.Texture1DArray.ArraySize = 4;
        const D3D12_SAMPLER_DESC sampler = linear_wrap();
        return sample_scene(kit, texture, srv, &sampler,
                            ps_of("Texture1DArray<float4> tex : register(t0); SamplerState smp : register(s0);\n", "return tex.Sample(smp, float2(i.uv.x * 2.0, floor(i.uv.y * 3.999)));"),
                            kTint, kScaleOne, kFlatTilt, out, error);
    });
    Cases::single(rr::CaseSpec("tex", "dim_texture2d_mips_trilinear").tolerance(sampler_tolerance()).tag("dimension"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.width = 128;
        spec.height = 128;
        spec.mips = 8;
        spec.content = 3;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
        if (texture == nullptr) {
            return false;
        }
        const D3D12_SAMPLER_DESC sampler = linear_wrap();
        return sample_scene(kit, texture, srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 8), &sampler,
                            ps_of("Texture2D<float4> tex : register(t0); SamplerState smp : register(s0);\n", "return tex.Sample(smp, i.uv * p1.xy);"), kTint, kScale, kFlatTilt, out, error);
    });
    Cases::single(rr::CaseSpec("tex", "dim_texture2d_array").tolerance(loose_sampler_tolerance()).tag("dimension"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.width = 32;
        spec.height = 32;
        spec.depth_or_array = 4;
        spec.mips = 6;
        spec.content = 0;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
        if (texture == nullptr) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2DArray.MipLevels = 6;
        srv.Texture2DArray.ArraySize = 4;
        const D3D12_SAMPLER_DESC sampler = linear_wrap();
        return sample_scene(kit, texture, srv, &sampler,
                            ps_of("Texture2DArray<float4> tex : register(t0); SamplerState smp : register(s0);\n",
                                  "uint slice = (uint)(i.uv.x * 1.999) + 2 * (uint)(i.uv.y * 1.999); return tex.Sample(smp, float3(i.uv * 4.0, slice));"),
                            kTint, kScaleOne, kFlatTilt, out, error);
    });
    Cases::single(rr::CaseSpec("tex", "dim_texture3d").tolerance(sampler_tolerance()).tag("dimension"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        spec.width = 16;
        spec.height = 16;
        spec.depth_or_array = 16;
        spec.mips = 3;
        spec.content = 0;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
        if (texture == nullptr) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture3D.MipLevels = 3;
        const D3D12_SAMPLER_DESC sampler = linear_wrap();
        return sample_scene(kit, texture, srv, &sampler,
                            ps_of("Texture3D<float4> tex : register(t0); SamplerState smp : register(s0);\n", "return tex.Sample(smp, float3(i.uv, 0.15 + 0.7 * i.uv.x));"), kTint, kScaleOne, kFlatTilt, out, error);
    });
    for (int array = 0; array < 2; ++array) {
        Cases::single(rr::CaseSpec("tex", array != 0 ? "dim_texturecube_array" : "dim_texturecube").tolerance(sampler_tolerance()).tag("dimension"),
                      [array](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          TextureSpec spec;
                          spec.width = 32;
                          spec.height = 32;
                          spec.depth_or_array = array != 0 ? 12 : 6;
                          spec.mips = 1;
                          spec.content = 0;
                          ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
                          if (texture == nullptr) {
                              return false;
                          }
                          D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                          srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                          srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                          if (array != 0) {
                              srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                              srv.TextureCubeArray.MipLevels = 1;
                              srv.TextureCubeArray.NumCubes = 2;
                          } else {
                              srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                              srv.TextureCube.MipLevels = 1;
                          }
                          const D3D12_SAMPLER_DESC sampler = linear_wrap();
                          const std::string body = array != 0 ? "return tex.Sample(smp, float4(normalize(i.nrm), i.nrm.y > 0.0 ? 1 : 0));" : "return tex.Sample(smp, normalize(i.nrm));";
                          const std::string decl = array != 0 ? "TextureCubeArray<float4> tex : register(t0); SamplerState smp : register(s0);\n"
                                                              : "TextureCube<float4> tex : register(t0); SamplerState smp : register(s0);\n";
                          return sample_scene(kit, texture, srv, &sampler, ps_of(decl, body), kTint, kScaleOne, 0.0f, out, error, nullptr, true);
                      });
    }
    struct BufferCase {
        const char* name;
        const char* decl;
        const char* body;
        DXGI_FORMAT format;
        UINT stride;
        bool raw;
    };
    const BufferCase buffers[] = {
        {"dim_buffer_typed", "Buffer<float4> buf : register(t0);\n", "return buf.Load((uint)(i.uv.x * 15.99) + 16 * (uint)(i.uv.y * 15.99));", DXGI_FORMAT_R32G32B32A32_FLOAT, 0, false},
        {"dim_buffer_typed_unorm8", "Buffer<float4> buf : register(t0);\n", "return buf.Load((uint)(i.uv.x * 15.99) + 16 * (uint)(i.uv.y * 15.99));", DXGI_FORMAT_R8G8B8A8_UNORM, 0, false},
        {"dim_buffer_structured", "struct Item { float4 color; float2 extra; };\nStructuredBuffer<Item> buf : register(t0);\n",
         "Item it = buf[(uint)(i.uv.x * 15.99) + 16 * (uint)(i.uv.y * 15.99)]; return float4(it.color.rgb * (0.5 + it.extra.x), 1);", DXGI_FORMAT_UNKNOWN, 24, false},
        {"dim_buffer_raw", "ByteAddressBuffer buf : register(t0);\n",
         "uint k = ((uint)(i.uv.x * 15.99) + 16 * (uint)(i.uv.y * 15.99)) * 16; uint4 v = buf.Load4(k); return float4(v) / 255.0;", DXGI_FORMAT_R32_TYPELESS, 0, true},
    };
    for (const BufferCase& item : buffers) {
        Cases::single(rr::CaseSpec("tex", item.name).tolerance(exact_op_tolerance()).tag("dimension"), [item](Kit& kit, rr::CaptureSet& out, std::string& error) {
            std::vector<std::uint8_t> bytes;
            UINT elements = 256;
            if (item.stride == 24) {
                std::vector<float> data;
                for (UINT k = 0; k < elements; ++k) {
                    const float f[6] = {static_cast<float>(k % 16) / 15.0f, static_cast<float>(k / 16) / 15.0f, static_cast<float>((k * 7) % 11) / 10.0f, 1.0f, static_cast<float>(k % 3) * 0.3f, 0.0f};
                    data.insert(data.end(), f, f + 6);
                }
                bytes.resize(data.size() * 4);
                std::memcpy(bytes.data(), data.data(), bytes.size());
            } else if (item.format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                for (UINT k = 0; k < elements; ++k) {
                    const std::uint8_t px[4] = {static_cast<std::uint8_t>((k % 16) * 17), static_cast<std::uint8_t>((k / 16) * 17), static_cast<std::uint8_t>((k * 13) & 255), 255};
                    bytes.insert(bytes.end(), px, px + 4);
                }
            } else if (item.raw) {
                for (UINT k = 0; k < elements; ++k) {
                    const std::uint32_t px[4] = {255 - (k % 16) * 11, (k / 16) * 17, (k * 29 + 7) & 255, 255};
                    const std::uint8_t* raw = reinterpret_cast<const std::uint8_t*>(px);
                    bytes.insert(bytes.end(), raw, raw + 16);
                }
            } else {
                for (UINT k = 0; k < elements; ++k) {
                    const float px[4] = {static_cast<float>(k % 16) / 15.0f, static_cast<float>(k / 16) / 15.0f, static_cast<float>(k % 7) / 6.0f, 1.0f};
                    const std::uint8_t* raw = reinterpret_cast<const std::uint8_t*>(px);
                    bytes.insert(bytes.end(), raw, raw + 16);
                }
            }
            ID3D12Resource* buffer = kit.make_upload(bytes.data(), bytes.size(), error);
            if (buffer == nullptr) {
                return false;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = item.format;
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if (item.raw) {
                srv.Buffer.NumElements = static_cast<UINT>(bytes.size() / 4);
                srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            } else {
                srv.Buffer.NumElements = elements;
                srv.Buffer.StructureByteStride = item.stride;
            }
            return sample_scene(kit, buffer, srv, nullptr, ps_of(item.decl, item.body), kTint, kScaleOne, -0.4f, out, error);
        });
    }
}

struct FormatCase {
    const char* name;
    DXGI_FORMAT format;
    DXGI_FORMAT view;
    float tolerance;
};

std::uint32_t texel_hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

Texels::Bytes format_bytes(DXGI_FORMAT format, std::uint32_t salt, unsigned width, unsigned height) {
    Texels::Bytes bytes;
    auto push16 = [&](std::uint16_t v) { bytes.push_back(static_cast<std::uint8_t>(v)); bytes.push_back(static_cast<std::uint8_t>(v >> 8)); };
    auto push32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) { bytes.push_back(static_cast<std::uint8_t>(v >> (8 * k))); } };
    for (unsigned t = 0; t < width * height; ++t) {
        auto h = [&](unsigned c) { return texel_hash(salt * 977u + t * 4u + c); };
        switch (format) {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
                for (unsigned c = 0; c < 4; ++c) { bytes.push_back(static_cast<std::uint8_t>(8 + h(c) % 240)); }
                break;
            case DXGI_FORMAT_R8G8B8A8_SNORM:
                for (unsigned c = 0; c < 4; ++c) { bytes.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(static_cast<int>(h(c) % 253) - 126))); }
                break;
            case DXGI_FORMAT_R8_UNORM:
                bytes.push_back(static_cast<std::uint8_t>(8 + h(0) % 240));
                break;
            case DXGI_FORMAT_R8G8_UNORM:
                for (unsigned c = 0; c < 2; ++c) { bytes.push_back(static_cast<std::uint8_t>(8 + h(c) % 240)); }
                break;
            case DXGI_FORMAT_R10G10B10A2_UNORM:
                push32((h(0) & 0x3fffffffu) | ((h(1) % 4u) << 30));
                break;
            case DXGI_FORMAT_R11G11B10_FLOAT:
                push32((0x2c0u + (h(0) % 0x100u)) | ((0x2c0u + (h(1) % 0x100u)) << 11) | ((0x160u + (h(2) % 0x80u)) << 22));
                break;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                for (unsigned c = 0; c < 4; ++c) { push16(static_cast<std::uint16_t>(((12u + h(c) % 4u) << 10) | ((h(c) >> 8) & 0x3ffu))); }
                break;
            case DXGI_FORMAT_R16G16_FLOAT:
                for (unsigned c = 0; c < 2; ++c) { push16(static_cast<std::uint16_t>(((12u + h(c) % 4u) << 10) | ((h(c) >> 8) & 0x3ffu))); }
                break;
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
                for (unsigned c = 0; c < 4; ++c) { push32(0x3f000000u | (h(c) & 0x7fffffu)); }
                break;
            case DXGI_FORMAT_R32_FLOAT:
                push32(0x3f000000u | (h(0) & 0x7fffffu));
                break;
            case DXGI_FORMAT_R16G16B16A16_UNORM:
                for (unsigned c = 0; c < 4; ++c) { push16(static_cast<std::uint16_t>(300 + h(c) % 65000)); }
                break;
            case DXGI_FORMAT_R16_UNORM:
                push16(static_cast<std::uint16_t>(300 + h(0) % 65000));
                break;
            case DXGI_FORMAT_R16G16B16A16_SNORM:
                for (unsigned c = 0; c < 4; ++c) { push16(static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<int>(h(c) % 65000) - 32500))); }
                break;
            default:
                break;
        }
    }
    return bytes;
}

void register_formats() {
    struct RawFormat {
        const char* name;
        DXGI_FORMAT format;
        DXGI_FORMAT view;
        bool amplify;
    };
    const RawFormat raw_formats[] = {
        {"r8g8b8a8_unorm", DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, true},
        {"r8g8b8a8_srgb", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false},
        {"b8g8r8a8_unorm", DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, true},
        {"r8g8b8a8_snorm", DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_SNORM, true},
        {"r10g10b10a2_unorm", DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, true},
        {"r11g11b10_float", DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, true},
        {"r16g16b16a16_float", DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, true},
        {"r32g32b32a32_float", DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, true},
        {"r16g16b16a16_unorm", DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM, true},
        {"r16g16b16a16_snorm", DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_R16G16B16A16_SNORM, true},
        {"r16_unorm", DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_UNORM, true},
        {"r8_unorm", DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, true},
        {"r8g8_unorm", DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, true},
        {"r16g16_float", DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, true},
        {"r32_float", DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, true},
        {"typeless_viewed_srgb", DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false},
        {"typeless_viewed_unorm", DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, true},
    };
    int salt_index = 0;
    for (const RawFormat& item : raw_formats) {
        const std::uint32_t salt = 101u + static_cast<std::uint32_t>(salt_index++);
        Cases::single(
            rr::CaseSpec("tex", std::string("format_") + item.name).tolerance(format_tolerance()).tag("format"),
            [item, salt](Kit& kit, rr::CaptureSet& out, std::string& error) {
                TextureSpec spec;
                spec.format = item.format;
                spec.width = 64;
                spec.height = 64;
                ID3D12Resource* texture = TextureFactory::create(kit, spec, false, error);
                if (texture == nullptr) {
                    return false;
                }
                const Texels::Bytes bytes = format_bytes(item.format, salt, 64, 64);
                if (!D3d12Tools::write_texture(kit.rig, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, bytes.data(), error)) {
                    return false;
                }
                const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
                const char* body = item.amplify ? "return frac(tex.SampleLevel(smp, i.uv, 0) * 64.0);" : "return tex.SampleLevel(smp, i.uv, 0);";
                return sample_scene(kit, texture, srv_2d(item.view, 1), &sampler,
                                    ps_of("Texture2D<float4> tex : register(t0); SamplerState smp : register(s0);\n", body), kTint, kScaleOne, 0.0f, out, error);
            },
            [item](Kit& kit) { return needs_format(kit, item.view, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE, "sampling this format"); });
    }
    const FormatCase compressed[] = {
        {"bc1_unorm", DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, 4},       {"bc1_srgb", DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC1_UNORM_SRGB, 5},
        {"bc2_unorm", DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM, 4},       {"bc3_unorm", DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, 4},
        {"bc3_srgb", DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_BC3_UNORM_SRGB, 5}, {"bc4_unorm", DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_UNORM, 4},
        {"bc5_unorm", DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_UNORM, 4},       {"bc7_unorm", DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM, 4},
        {"bc7_srgb", DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_BC7_UNORM_SRGB, 5},
        {"bc6h_uf16", DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_BC6H_UF16, 4}};
    for (const FormatCase& item : compressed) {
        Cases::single(
            rr::CaseSpec("tex", std::string("format_") + item.name).tolerance(compressed_format_tolerance(item.name)).tag("format"),
            [item](Kit& kit, rr::CaptureSet& out, std::string& error) {
                TextureSpec spec;
                spec.format = item.format;
                spec.width = 64;
                spec.height = 64;
                spec.mips = 1;
                ID3D12Resource* texture = TextureFactory::create(kit, spec, true, error);
                if (texture == nullptr) {
                    return false;
                }
                const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
                return sample_scene(kit, texture, srv_2d(item.view, 1), &sampler,
                                    ps_of("Texture2D<float4> tex : register(t0); SamplerState smp : register(s0);\n", "return saturate(tex.SampleLevel(smp, i.uv, 0));"), kTint, kScaleOne, 0.0f, out, error);
            },
            [item](Kit& kit) { return needs_format(kit, item.view, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE, "sampling this block format"); });
    }
}

void add_format_stream(const std::string& name, DXGI_FORMAT format, DXGI_FORMAT view) {
    Cases::frames(
        rr::CaseSpec("tex", "format_" + name + "_streamed_updates").frames(4, {0, 3}).tolerance(sampler_tolerance()).tag("format"),
        [format, view](Cases::Steps& steps) {
            auto rig = std::make_shared<SampleRig>();
            auto texture = std::make_shared<ID3D12Resource*>(nullptr);
            steps.on_setup = [=](Kit& kit, std::string& error) {
                TextureSpec spec;
                spec.format = format;
                spec.width = 64;
                spec.height = 64;
                *texture = TextureFactory::create(kit, spec, false, error);
                const D3D12_SAMPLER_DESC sampler = linear_wrap();
                return *texture != nullptr &&
                       rig->open(kit, *texture, srv_2d(view, 1), sampler, ps_of("Texture2D<float4> tex : register(t0); SamplerState smp : register(s0);\n", "return tex.Sample(smp, i.uv * p1.xy);"), error);
            };
            steps.on_frame = [=](Kit& kit, unsigned frame, std::string& error) {
                Texels::Bytes bytes;
                if (Texels::is_block(format)) {
                    bytes = Texels::block_compressed(format, 64, 64, 500u + frame * 31u);
                } else {
                    bytes = Texels::encode(format, Texels::content(frame, 64, 64), 64, 64);
                }
                if (!D3d12Tools::write_texture(kit.rig, *texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, bytes.data(), error)) {
                    return false;
                }
                const float tint[4] = {1, 1, 1, 1};
                const float scale[4] = {1, 1, 0, 0};
                return rig->frame(kit, tint, scale, -0.4f - 0.1f * static_cast<float>(frame), true, error);
            };
            steps.on_capture = [rig](Kit& kit, unsigned, rr::CaptureSet& out, std::string& error) { return rig->capture(kit, out, error); };
        },
        [view](Kit& kit) { return needs_format(kit, view, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE, "sampling this format"); });
}

void register_format_streams() {
    const FormatCase formats[] = {
        {"r8g8b8a8_unorm", DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, 4},       {"r8g8b8a8_srgb", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 5},
        {"b8g8r8a8_unorm", DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, 4},       {"r10g10b10a2_unorm", DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM, 4},
        {"r16g16b16a16_float", DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, 4}, {"r32g32b32a32_float", DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 4},
        {"r16g16b16a16_unorm", DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM, 4}, {"r16_unorm", DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_UNORM, 4},
        {"r8_unorm", DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, 4},                         {"r8g8_unorm", DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, 4},
        {"r16g16_float", DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, 4},             {"r32_float", DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, 4},
        {"bc1_unorm", DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, 4},                      {"bc1_srgb", DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC1_UNORM_SRGB, 4},
        {"bc2_unorm", DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM, 4},                      {"bc3_unorm", DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, 4},
        {"bc4_unorm", DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_UNORM, 4},                      {"bc5_unorm", DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_UNORM, 4},
        {"bc7_unorm", DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM, 4},                      {"bc7_srgb", DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_BC7_UNORM_SRGB, 4}};
    for (const FormatCase& item : formats) {
        const std::string name = item.name;
        if (name == "r8g8b8a8_srgb" || name == "bc7_unorm") {
            add_format_stream(item.name, item.format, item.view);
        }
    }
}

bool depth_sample(Kit& kit, DXGI_FORMAT depth_format, float angle, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, depth_format)) {
        return false;
    }
    const DepthFormats formats = DepthFormats::of(depth_format);
    Surface visual;
    if (!visual.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
        return false;
    }
    Pso writer = stage.pso();
    if (formats.has_stencil) {
        writer.desc.DepthStencilState.StencilEnable = TRUE;
        writer.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_REPLACE, D3D12_COMPARISON_FUNC_ALWAYS};
        writer.desc.DepthStencilState.BackFace = writer.desc.DepthStencilState.FrontFace;
    }
    ID3D12PipelineState* write_state = writer.build(error);
    std::string ps = std::string(kSamplePrefix) + "Texture2D<float> depth : register(t0);\n";
    if (formats.has_stencil) {
        ps += "Texture2D<uint2> stencil : register(t1);\n";
    }
    ps += "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { int3 p = int3(pos.xy, 0); float d = depth.Load(p);\n";
    ps += formats.has_stencil ? "  float s = (float)stencil.Load(p).g / 255.0; return float4(pow(d, 12.0), s, d, 1); }\n" : "  return float4(pow(d, 12.0), d, 1 - d, 1); }\n";
    Pso reader(kit, stage.canvas.binder.signature);
    reader.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(ps.c_str())).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
    ID3D12PipelineState* read_state = reader.build(error);
    if (write_state == nullptr || read_state == nullptr) {
        return false;
    }
    stage.canvas.binder.srv_texture(0, stage.canvas.depth.resource, formats.depth_srv);
    if (formats.has_stencil) {
        D3D12_SHADER_RESOURCE_VIEW_DESC plane = srv_2d(formats.stencil_srv, 1);
        plane.Texture2D.PlaneSlice = 1;
        stage.canvas.binder.srv(1, stage.canvas.depth.resource, &plane);
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0, 0, 0, 1};
            stage.canvas.begin(list, clear);
            list->SetPipelineState(write_state);
            list->OMSetStencilRef(0x80);
            stage.mesh(list, stage.torus, rr::Mat4::rotate_x(angle), 1, 1, 1);
            list->OMSetStencilRef(0x40);
            stage.mesh(list, stage.sphere, rr::Mat4::translate(0.1f, 0.0f, -0.4f), 1, 1, 1);
            stage.canvas.depth.to(list, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            visual.bind_target(list);
            list->ClearRenderTargetView(visual.view, clear, 0, nullptr);
            list->SetPipelineState(read_state);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3, 1, 0, 0);
        },
        error);
    if (!ok) {
        return false;
    }
    rr::Image image;
    if (!kit.read_image(visual.resource, visual.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
        return false;
    }
    out.add("", std::move(image));
    return true;
}

void register_depth_formats() {
    struct Item {
        const char* name;
        DXGI_FORMAT format;
    };
    const Item items[] = {{"d32_float", DXGI_FORMAT_D32_FLOAT}, {"d16_unorm", DXGI_FORMAT_D16_UNORM}, {"d24_unorm_s8_uint", DXGI_FORMAT_D24_UNORM_S8_UINT},
                          {"d32_float_s8x24_uint", DXGI_FORMAT_D32_FLOAT_S8X24_UINT}};
    for (const Item& item : items) {
        Cases::single(rr::CaseSpec("tex", std::string("depth_stencil_sample_") + item.name).tolerance(rr::Tolerance::blend(3)).tag("format"),
                      [item, angle = 0.8f + 0.25f * static_cast<float>(&item - items)](Kit& kit, rr::CaptureSet& out, std::string& error) { return depth_sample(kit, item.format, angle, out, error); });
    }
}

const struct Registrar {
    Registrar() {
        register_dimensions();
        register_formats();
        register_depth_formats();
        register_format_streams();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
