#include "render/d3d12/textures.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d12 {

bool Texels::is_block(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
    }
}

Texels::Bytes Texels::content(unsigned kind, unsigned width, unsigned height) {
    Bytes bytes = base_content(kind, width, height);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            bytes[(static_cast<std::size_t>(y) * width + x) * 4 + 3] = static_cast<std::uint8_t>(160 + ((x / 2 + y / 3) % 96));
        }
    }
    return bytes;
}

Texels::Bytes Texels::base_content(unsigned kind, unsigned width, unsigned height) {
    switch (kind) {
        case 1:
            return rr::Pattern::gradient(width, height);
        case 2:
            return rr::Pattern::noise(width, height, 1234u);
        case 3:
            return rr::Pattern::checker(width, height, std::max(1u, width / 8), rr::Pattern::pack(250, 250, 250), rr::Pattern::pack(30, 90, 220));
        default:
            return rr::Pattern::checker(width, height, std::max(1u, width / 8), rr::Pattern::pack(240, 60, 40), rr::Pattern::pack(40, 200, 90));
    }
}

Texels::Bytes Texels::block_compressed(DXGI_FORMAT format, unsigned width, unsigned height, std::uint32_t seed) {
    const unsigned bw = (width + 3) / 4;
    const unsigned bh = (height + 3) / 4;
    unsigned block = 16;
    if (format == DXGI_FORMAT_BC1_UNORM || format == DXGI_FORMAT_BC1_UNORM_SRGB || format == DXGI_FORMAT_BC4_UNORM) {
        block = 8;
    }
    Bytes out(static_cast<std::size_t>(bw) * bh * block);
    std::uint32_t state = seed;
    for (std::size_t b = 0; b < static_cast<std::size_t>(bw) * bh; ++b) {
        std::uint8_t* p = out.data() + b * block;
        for (unsigned i = 0; i < block; ++i) {
            p[i] = static_cast<std::uint8_t>(rr::Pattern::unit_random(state) * 255.0f);
        }
        if (format == DXGI_FORMAT_BC7_UNORM || format == DXGI_FORMAT_BC7_UNORM_SRGB) {
            p[0] = 0x40;
        } else if (format == DXGI_FORMAT_BC6H_UF16) {
            p[0] = static_cast<std::uint8_t>((p[0] & 0xe0) | 0x03);
        } else if (format == DXGI_FORMAT_BC1_UNORM || format == DXGI_FORMAT_BC1_UNORM_SRGB) {
            const std::uint16_t c0 = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(p[2] | (p[3] << 8));
            if (c0 <= c1) {
                std::swap(p[0], p[2]);
                std::swap(p[1], p[3]);
            }
        } else if (format == DXGI_FORMAT_BC4_UNORM) {
            p[0] = 255;
            p[1] = static_cast<std::uint8_t>(p[1] % 100);
        } else if (format == DXGI_FORMAT_BC5_UNORM) {
            p[0] = 255;
            p[1] = static_cast<std::uint8_t>(p[1] % 100);
            p[8] = 255;
            p[9] = static_cast<std::uint8_t>(p[9] % 100);
        } else if (format == DXGI_FORMAT_BC3_UNORM || format == DXGI_FORMAT_BC3_UNORM_SRGB) {
            p[0] = 255;
            p[1] = static_cast<std::uint8_t>(p[1] % 100);
            const std::uint16_t c0 = static_cast<std::uint16_t>(p[8] | (p[9] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(p[10] | (p[11] << 8));
            if (c0 <= c1) {
                std::swap(p[8], p[10]);
                std::swap(p[9], p[11]);
            }
        } else if (format == DXGI_FORMAT_BC2_UNORM) {
            const std::uint16_t c0 = static_cast<std::uint16_t>(p[8] | (p[9] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(p[10] | (p[11] << 8));
            if (c0 <= c1) {
                std::swap(p[8], p[10]);
                std::swap(p[9], p[11]);
            }
        }
    }
    return out;
}

Texels::Bytes Texels::encode(DXGI_FORMAT format, const Bytes& rgba, unsigned width, unsigned height) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    Bytes out;
    auto at = [&](std::size_t i, int c) { return rgba[i * 4 + static_cast<std::size_t>(c)]; };
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return rgba;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            out = rgba;
            for (std::size_t i = 0; i < count; ++i) {
                std::swap(out[i * 4], out[i * 4 + 2]);
            }
            return out;
        case DXGI_FORMAT_R8_UNORM:
            out.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                out[i] = at(i, 0);
            }
            return out;
        case DXGI_FORMAT_R8G8_UNORM:
            out.resize(count * 2);
            for (std::size_t i = 0; i < count; ++i) {
                out[i * 2] = at(i, 0);
                out[i * 2 + 1] = at(i, 1);
            }
            return out;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            out.resize(count * 4);
            for (std::size_t i = 0; i < count; ++i) {
                const std::uint32_t v = (at(i, 0) * 1023u / 255u) | ((at(i, 1) * 1023u / 255u) << 10) | ((at(i, 2) * 1023u / 255u) << 20) | (3u << 30);
                std::memcpy(&out[i * 4], &v, 4);
            }
            return out;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            out.resize(count * 8);
            for (std::size_t i = 0; i < count; ++i) {
                for (int c = 0; c < 4; ++c) {
                    const std::uint16_t h = rr::PixelInfo::float_to_half(static_cast<float>(at(i, c)) / 255.0f);
                    std::memcpy(&out[i * 8 + static_cast<std::size_t>(c) * 2], &h, 2);
                }
            }
            return out;
        case DXGI_FORMAT_R16G16_FLOAT:
            out.resize(count * 4);
            for (std::size_t i = 0; i < count; ++i) {
                for (int c = 0; c < 2; ++c) {
                    const std::uint16_t h = rr::PixelInfo::float_to_half(static_cast<float>(at(i, c)) / 255.0f);
                    std::memcpy(&out[i * 4 + static_cast<std::size_t>(c) * 2], &h, 2);
                }
            }
            return out;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            out.resize(count * 16);
            for (std::size_t i = 0; i < count; ++i) {
                for (int c = 0; c < 4; ++c) {
                    const float f = static_cast<float>(at(i, c)) / 255.0f;
                    std::memcpy(&out[i * 16 + static_cast<std::size_t>(c) * 4], &f, 4);
                }
            }
            return out;
        case DXGI_FORMAT_R32_FLOAT:
            out.resize(count * 4);
            for (std::size_t i = 0; i < count; ++i) {
                const float f = static_cast<float>(at(i, 0)) / 255.0f;
                std::memcpy(&out[i * 4], &f, 4);
            }
            return out;
        case DXGI_FORMAT_R16_UNORM:
            out.resize(count * 2);
            for (std::size_t i = 0; i < count; ++i) {
                const std::uint16_t v = static_cast<std::uint16_t>(at(i, 0) * 257u);
                std::memcpy(&out[i * 2], &v, 2);
            }
            return out;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            out.resize(count * 8);
            for (std::size_t i = 0; i < count; ++i) {
                for (int c = 0; c < 4; ++c) {
                    const std::uint16_t v = static_cast<std::uint16_t>(at(i, c) * 257u);
                    std::memcpy(&out[i * 8 + static_cast<std::size_t>(c) * 2], &v, 2);
                }
            }
            return out;
        case DXGI_FORMAT_R8G8B8A8_SNORM:
            out = rgba;
            for (auto& b : out) {
                b = static_cast<std::uint8_t>(b >> 1);
            }
            return out;
        case DXGI_FORMAT_R8G8B8A8_UINT:
            return rgba;
        default:
            return rgba;
    }
}

ID3D12Resource* TextureFactory::create(Kit& kit, const TextureSpec& spec, bool fill, std::string& error) {
    HRESULT result = S_OK;
    D3D12_RESOURCE_DESC desc;
    if (spec.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        desc = D3d12Tools::volume_desc(spec.width, spec.height, spec.depth_or_array, spec.format, spec.flags, spec.mips);
    } else {
        desc = D3d12Tools::texture_desc(spec.width, spec.height, spec.format, spec.flags, spec.mips, spec.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ? spec.depth_or_array : spec.depth_or_array, spec.samples);
        desc.Dimension = spec.dimension;
        if (spec.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
            desc.Height = 1;
        }
    }
    D3D12_CLEAR_VALUE clear{};
    const bool target = (spec.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;
    clear.Format = spec.format;
    ID3D12Resource* resource = D3d12Tools::texture(kit.rig, desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, target ? &clear : nullptr, &result);
    if (resource == nullptr) {
        error = "texture creation: " + D3d12Tools::hr(result);
        return nullptr;
    }
    if (!fill) {
        return resource;
    }
    const UINT slices = spec.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : spec.depth_or_array;
    for (UINT slice = 0; slice < slices; ++slice) {
        for (UINT mip = 0; mip < spec.mips; ++mip) {
            const UINT mw = std::max(1u, spec.width >> mip);
            const UINT mh = spec.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ? 1 : std::max(1u, spec.height >> mip);
            const UINT md = spec.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? std::max(1u, spec.depth_or_array >> mip) : 1;
            Texels::Bytes bytes;
            if (Texels::is_block(spec.format)) {
                bytes = Texels::block_compressed(spec.format, mw, mh, 77u + slice * 131u + mip * 17u);
            } else {
                Texels::Bytes rgba;
                for (UINT z = 0; z < md; ++z) {
                    Texels::Bytes plane = Texels::content(spec.content + slice, mw, mh);
                    if (mip > 0) {
                        static const unsigned tint[8][3] = {{255, 255, 255}, {255, 60, 60}, {60, 255, 60}, {60, 60, 255}, {255, 255, 60}, {255, 60, 255}, {60, 255, 255}, {160, 160, 160}};
                        for (std::size_t i = 0; i < plane.size(); i += 4) {
                            for (int c = 0; c < 3; ++c) {
                                plane[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(plane[i + static_cast<std::size_t>(c)] * tint[mip % 8][c] / 255);
                            }
                        }
                    }
                    if (md > 1) {
                        for (std::size_t i = 0; i < plane.size(); i += 4) {
                            plane[i + 2] = static_cast<std::uint8_t>(z * 255 / (md - 1));
                        }
                    }
                    rgba.insert(rgba.end(), plane.begin(), plane.end());
                }
                bytes = Texels::encode(spec.format, rgba, mw, mh * md);
            }
            const UINT subresource = mip + slice * spec.mips;
            if (!D3d12Tools::write_texture(kit.rig, resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, subresource, bytes.data(), error)) {
                return nullptr;
            }
        }
    }
    return resource;
}

D3D12_SHADER_RESOURCE_VIEW_DESC srv_2d(DXGI_FORMAT format, UINT mips, UINT most_detailed) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MipLevels = mips;
    desc.Texture2D.MostDetailedMip = most_detailed;
    return desc;
}

bool sample_scene(Kit& kit, ID3D12Resource* texture, const D3D12_SHADER_RESOURCE_VIEW_DESC& srv, const D3D12_SAMPLER_DESC* sampler, const std::string& pixel_source,
                  const float p0[4], const float p1[4], float tilt, rr::CaptureSet& out, std::string& error, const D3D12_SAMPLER_DESC* second_sampler, bool on_sphere) {
    Stage stage;
    if (!stage.open(kit, error)) {
        return false;
    }
    stage.canvas.binder.srv(0, texture, &srv);
    if (sampler != nullptr) {
        stage.canvas.binder.sampler(0, *sampler);
    }
    if (second_sampler != nullptr) {
        stage.canvas.binder.sampler(1, *second_sampler);
    }
    Pso builder = stage.pso();
    builder.ps(Hlsl12::with_root(pixel_source.c_str()));
    ID3D12PipelineState* state = builder.build(error);
    if (state == nullptr) {
        return false;
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.1f, 0.12f, 0.2f, 1.0f};
            stage.canvas.begin(list, clear);
            list->SetPipelineState(state);
            const rr::Mat4 matrix = tilt > 500.0f ? rr::Mat4::scale(1.0f / 1.2f, 1.0f / 1.2f, 1.0f) : rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -2.4f) * rr::Mat4::rotate_x(tilt);
            set_cbv_root(list, matrix, p0, p1);
            if (on_sphere) {
                set_cbv_root(list, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -2.4f) * rr::Mat4::rotate_x(0.3f) * rr::Mat4::scale(1.2f, 1.2f, 1.2f), p0, p1);
                stage.sphere.bind(list);
                stage.sphere.draw(list);
            } else {
                stage.plane.bind(list);
                stage.plane.draw(list);
            }
        },
        error);
    return ok && stage.finish(out, error);
}

bool SampleRig::open(Kit& kit, ID3D12Resource* texture, const D3D12_SHADER_RESOURCE_VIEW_DESC& srv, const D3D12_SAMPLER_DESC& sampler, const std::string& pixel_source, std::string& error) {
    if (!stage.open(kit, error)) {
        return false;
    }
    stage.canvas.binder.srv(0, texture, &srv);
    stage.canvas.binder.sampler(0, sampler);
    Pso builder = stage.pso();
    builder.ps(Hlsl12::with_root(pixel_source.c_str()));
    state = builder.build(error);
    return state != nullptr;
}

bool SampleRig::frame(Kit& kit, const float p0[4], const float p1[4], float tilt, bool clear, std::string& error) {
    return kit.submit(
        [&](ID3D12GraphicsCommandList* list) {
            const float background[4] = {0.1f, 0.12f, 0.2f, 1.0f};
            if (clear) {
                stage.canvas.begin(list, background);
            } else {
                stage.canvas.begin_keep(list);
            }
            list->SetPipelineState(state);
            const rr::Mat4 matrix = tilt > 500.0f ? rr::Mat4::scale(1.0f / 1.2f, 1.0f / 1.2f, 1.0f) : rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -2.4f) * rr::Mat4::rotate_x(tilt);
            set_cbv_root(list, matrix, p0, p1);
            stage.plane.bind(list);
            stage.plane.draw(list);
        },
        error);
}

const char* const kSamplePrefix = "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n";

std::string needs_format(Kit& kit, DXGI_FORMAT format, D3D12_FORMAT_SUPPORT1 support, const char* what) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT data{};
    data.Format = format;
    if (FAILED(kit.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &data, sizeof data)) || (data.Support1 & support) != support) {
        return what;
    }
    return {};
}

}  // namespace lsw::suites::render::d3d12

namespace lsw::suites::render::d3d12 {

rr::Tolerance sampler_tolerance() {
    rr::Tolerance t = rr::Tolerance::filter(6, 0.004).with_edge(1);
    t.min_psnr = 40.0;
    t.outlier_max_delta = 40.0;
    t.max_outliers = 80;
    t.name = "sampler(6,edge1,80,psnr40)";
    return t;
}

rr::Tolerance loose_sampler_tolerance() {
    rr::Tolerance t = rr::Tolerance::filter(16, 0.05).with_edge(1);
    t.min_psnr = 33.0;
    t.outlier_max_delta = 255.0;
    t.max_outliers = 3300;
    t.name = "noise-minified(16,edge1,3300,psnr33)";
    return t;
}

rr::Tolerance format_tolerance() {
    rr::Tolerance t = rr::Tolerance::filter(4, 0.01).with_edge(1);
    t.min_psnr = 40.0;
    t.outlier_max_delta = 255.0;
    t.max_outliers = 16;
    t.name = "texel-point(4,edge1,16,psnr40)";
    return t;
}

rr::Tolerance compressed_format_tolerance(const std::string& name) {
    const bool legacy = name.rfind("bc1", 0) == 0 || name.rfind("bc2", 0) == 0 || name.rfind("bc3", 0) == 0 || name.rfind("bc4", 0) == 0 || name.rfind("bc5", 0) == 0;
    if (!legacy) {
        return format_tolerance();
    }
    rr::Tolerance t = rr::Tolerance::block_compressed().with_edge(1);
    t.name = "block-compressed(edge1)";
    return t;
}

rr::Tolerance texel_edge_tolerance() {
    rr::Tolerance t = rr::Tolerance::filter(4, 0.02).with_edge(1);
    t.min_psnr = 26.0;
    t.outlier_max_delta = 255.0;
    t.max_outliers = 200;
    t.name = "texel-edge(4,edge1,200,psnr26)";
    return t;
}

rr::Tolerance exact_op_tolerance() {
    rr::Tolerance t = rr::Tolerance::filter(2, 0.0005).with_edge(1);
    t.outlier_max_delta = 255.0;
    t.max_outliers = 8;
    t.name = "op(2,edge1,8)";
    return t;
}

rr::Tolerance anisotropic_tolerance() {
    rr::Tolerance t = rr::Tolerance::psnr(34);
    t.name = "aniso(psnr34)";
    return t;
}

}  // namespace lsw::suites::render::d3d12
