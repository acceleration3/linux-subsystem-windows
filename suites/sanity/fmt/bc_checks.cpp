#include "sanity/fmt/bc_checks.hpp"

#include "core/text.hpp"
#include "sanity/fmt/format_data.hpp"
#include "sanity/fmt/gl_format_io.hpp"
#include "sanity/fmt/kit.hpp"
#include "sanity/fmt/vulkan_format_io.hpp"
#include "sanity/gl/kit.hpp"
#include "sanity/vk/kit.hpp"
#include "support/bc_codec.hpp"
#include "support/d3d11_format_runner.hpp"
#include "support/d3d12_format_runner.hpp"
#include "support/d3d12_tools.hpp"
#include "support/pattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace lsw::suites::sanity::fmt {

namespace {

constexpr unsigned kEdge = 16;
constexpr unsigned kBlocksAcross = kEdge / 4;

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

Bytes family_blocks(int family, std::uint64_t seed, int count) {
    if (family >= 1 && family <= 5) {
        return BcCodec::blocks(family, seed, count);
    }
    Bytes data(static_cast<std::size_t>(BcCodec::block_bytes(family)) * static_cast<std::size_t>(count));
    Pattern::fill(seed, 0, data.data(), data.size());
    return data;
}

unsigned color_index(int family, const std::uint8_t* block, int texel) {
    const std::uint8_t* indices = family == 1 ? block + 4 : block + 12;
    std::uint32_t word = 0;
    std::memcpy(&word, indices, 4);
    return (word >> (2 * texel)) & 3u;
}

unsigned ramp_index(const std::uint8_t* block, int texel) {
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
    }
    return static_cast<unsigned>((bits >> (3 * texel)) & 7u);
}

double channel_tolerance(int family, const std::uint8_t* block, int texel, int channel) {
    bool endpoint = false;
    switch (family) {
    case 1:
        endpoint = channel == 3 || color_index(family, block, texel) <= 1;
        break;
    case 2:
        endpoint = channel == 3 || color_index(family, block, texel) <= 1;
        break;
    case 3:
        endpoint = channel == 3 ? ramp_index(block, texel) <= 1 : color_index(family, block, texel) <= 1;
        break;
    case 4:
        endpoint = ramp_index(block, texel) <= 1;
        break;
    case 5:
        endpoint = ramp_index(channel == 0 ? block : block + 8, texel) <= 1;
        break;
    default:
        break;
    }
    return BcCodec::tolerance(family, endpoint);
}

void sample_against_decoder(FormatRunner& runner, Limited& out, int family, DXGI_FORMAT format, const Bytes& data, unsigned width, unsigned height,
                            const std::string& name) {
    std::vector<std::uint32_t> words;
    std::string error;
    if (!runner.sample(format, format, "float4", width, height, data, words, error)) {
        out.add(name + " sampling failed: " + error);
        return;
    }
    const unsigned blocks_across = BcCodec::blocks_across(width);
    const int block_size = BcCodec::block_bytes(family);
    int reported = 0;
    for (unsigned y = 0; y < height && reported < 3; ++y) {
        for (unsigned x = 0; x < width && reported < 3; ++x) {
            const std::uint8_t* block = data.data() + (static_cast<std::size_t>(y / 4) * blocks_across + x / 4) * static_cast<std::size_t>(block_size);
            float texels[16][4];
            BcCodec::decode(family, false, block, texels);
            const int texel = static_cast<int>((y % 4) * 4 + (x % 4));
            for (int c = 0; c < 4; ++c) {
                float got = 0.0f;
                std::memcpy(&got, &words[(static_cast<std::size_t>(y) * width + x) * 4 + static_cast<std::size_t>(c)], 4);
                const double limit = channel_tolerance(family, block, texel, c);
                if (!(std::fabs(static_cast<double>(got) - static_cast<double>(texels[texel][c])) <= limit)) {
                    out.add(name + " texel (" + std::to_string(x) + "," + std::to_string(y) + ") channel " + std::to_string(c) + " samples " +
                            std::to_string(got) + ", the reference decoder gives " + std::to_string(texels[texel][c]) + " within " + std::to_string(limit));
                    ++reported;
                    break;
                }
            }
        }
    }
}

void replace_blocks(Bytes& model, const Bytes& source, unsigned bb, unsigned dst_bx, unsigned dst_by, unsigned src_bx, unsigned src_by, unsigned src_across,
                    unsigned count_x, unsigned count_y) {
    for (unsigned y = 0; y < count_y; ++y) {
        for (unsigned x = 0; x < count_x; ++x) {
            std::memcpy(model.data() + (static_cast<std::size_t>(dst_by + y) * kBlocksAcross + dst_bx + x) * bb,
                        source.data() + (static_cast<std::size_t>(src_by + y) * src_across + src_bx + x) * bb, bb);
        }
    }
}

Bytes region_blocks(const Bytes& source, unsigned bb, unsigned bx, unsigned by, unsigned across, unsigned count_x, unsigned count_y) {
    Bytes out;
    for (unsigned y = 0; y < count_y; ++y) {
        const std::uint8_t* row = source.data() + (static_cast<std::size_t>(by + y) * across + bx) * bb;
        out.insert(out.end(), row, row + static_cast<std::size_t>(count_x) * bb);
    }
    return out;
}

void d3d11_copies(D3d11Device& device, Limited& out, DXGI_FORMAT format, int family, const std::string& name) {
    const unsigned bb = static_cast<unsigned>(BcCodec::block_bytes(family));
    const Bytes a = family_blocks(family, 0xA0 + static_cast<std::uint64_t>(family), static_cast<int>(kBlocksAcross * kBlocksAcross));
    const Bytes b = family_blocks(family, 0xB0 + static_cast<std::uint64_t>(family), static_cast<int>(kBlocksAcross * kBlocksAcross));
    D3D11_SUBRESOURCE_DATA init_a{a.data(), kBlocksAcross * bb, 0};
    D3D11_SUBRESOURCE_DATA init_b{b.data(), kBlocksAcross * bb, 0};
    HRESULT result = S_OK;
    Com<ID3D11Texture2D> ta = D3d11Tools::texture(device, D3d11Tools::desc2d(kEdge, kEdge, format, D3D11_BIND_SHADER_RESOURCE), &init_a, &result);
    Com<ID3D11Texture2D> tb = D3d11Tools::texture(device, D3d11Tools::desc2d(kEdge, kEdge, format, D3D11_BIND_SHADER_RESOURCE), &init_b, &result);
    Com<ID3D11Texture2D> tc = D3d11Tools::texture(device, D3d11Tools::desc2d(kEdge, kEdge, format, D3D11_BIND_SHADER_RESOURCE), nullptr, &result);
    if (!ta || !tb || !tc) {
        out.add(name + " textures cannot be created: " + D3d11Tools::hr(result));
        return;
    }
    TexelImage image;
    std::string error;
    device.context->CopyResource(tc.get(), ta.get());
    if (!D3d11Tools::read_texture(device, tc.get(), 0, image, error) || image.data != a) {
        out.add(name + " CopyResource of blocks is not bit exact" + (error.empty() ? std::string() : ": " + error));
    }
    const Bytes patch = region_blocks(b, bb, 1, 1, kBlocksAcross, 2, 2);
    const D3D11_BOX box{4, 4, 0, 12, 12, 1};
    device.context->UpdateSubresource(tc.get(), 0, &box, patch.data(), 2 * bb, 0);
    Bytes model = a;
    replace_blocks(model, b, bb, 1, 1, 1, 1, kBlocksAcross, 2, 2);
    if (!D3d11Tools::read_texture(device, tc.get(), 0, image, error) || image.data != model) {
        out.add(name + " a block aligned UpdateSubresource differs from the model at byte " + std::to_string(Kit::first_difference(model, image.data)));
    }
    const D3D11_BOX source_box{8, 0, 0, 16, 8, 1};
    device.context->CopySubresourceRegion(tc.get(), 0, 0, 8, 0, tb.get(), 0, &source_box);
    replace_blocks(model, b, bb, 0, 2, 2, 0, kBlocksAcross, 2, 2);
    if (!D3d11Tools::read_texture(device, tc.get(), 0, image, error) || image.data != model) {
        out.add(name + " a block aligned CopySubresourceRegion differs from the model at byte " + std::to_string(Kit::first_difference(model, image.data)));
    }
}

void d3d12_partial(D3d12Rig& rig, Limited& out, DXGI_FORMAT format, int family, const std::string& name) {
    const unsigned bb = static_cast<unsigned>(BcCodec::block_bytes(family));
    const Bytes a = family_blocks(family, 0xA0 + static_cast<std::uint64_t>(family), static_cast<int>(kBlocksAcross * kBlocksAcross));
    const Bytes b = family_blocks(family, 0xB0 + static_cast<std::uint64_t>(family), 4);
    HRESULT result = S_OK;
    ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(kEdge, kEdge, format), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &result);
    ID3D12Resource* upload = D3d12Tools::upload(rig, 1024, &result);
    if (texture == nullptr || upload == nullptr) {
        out.add(name + " resources cannot be created: " + D3d12Tools::hr(result));
        return;
    }
    std::string error;
    if (!D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, a.data(), error)) {
        out.add(name + " upload failed: " + error);
        return;
    }
    void* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    if (FAILED(upload->Map(0, &none, &mapped))) {
        out.add(name + " mapping the upload buffer failed");
        return;
    }
    std::memset(mapped, 0, 1024);
    for (unsigned row = 0; row < 2; ++row) {
        std::memcpy(static_cast<std::uint8_t*>(mapped) + row * 256, b.data() + static_cast<std::size_t>(row) * 2 * bb, 2 * bb);
    }
    upload->Unmap(0, nullptr);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    footprint.Offset = 0;
    footprint.Footprint = {format, 8, 8, 1, 256};
    const bool copied = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload;
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprint;
            list->CopyTextureRegion(&destination, 4, 4, 0, &source, nullptr);
        },
        error);
    TexelImage image;
    if (!copied || !D3d12Tools::read_texture(rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, image, error)) {
        out.add(name + " partial copy failed: " + error);
        return;
    }
    Bytes model = a;
    replace_blocks(model, b, bb, 1, 1, 0, 0, 2, 2, 2);
    if (image.data != model) {
        out.add(name + " a block aligned CopyTextureRegion differs from the model at byte " + std::to_string(Kit::first_difference(model, image.data)));
    }
}

void runner_blocks(FormatRunner& runner, Limited& out, int& tested, const std::function<void(DXGI_FORMAT, int, const std::string&)>& extra) {
    for (int family = 1; family <= 7; ++family) {
        const DXGI_FORMAT format = BcCodec::unorm_format(family);
        UINT bits = 0;
        if (!runner.support(format, bits) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
            continue;
        }
        ++tested;
        const std::string name = std::string(runner.label()) + " " + FormatTable::name(format);
        const Bytes data = family_blocks(family, 0x77 + static_cast<std::uint64_t>(family), static_cast<int>(kBlocksAcross * kBlocksAcross));
        Bytes back;
        std::string error;
        if (!runner.roundtrip(format, kEdge, kEdge, data, back, error)) {
            out.add(name + " upload and readback failed: " + error);
        } else if (back != data) {
            out.add(name + " blocks changed in an upload and readback at byte " + std::to_string(Kit::first_difference(data, back)));
        }
        if (BcCodec::has_reference(format) && (bits & Kit::kSampleBits) != 0) {
            sample_against_decoder(runner, out, family, format, data, kEdge, kEdge, name);
        }
        extra(format, family, name);
    }
    const DXGI_FORMAT signed_formats[] = {DXGI_FORMAT_BC4_SNORM, DXGI_FORMAT_BC5_SNORM, DXGI_FORMAT_BC6H_SF16};
    for (DXGI_FORMAT format : signed_formats) {
        UINT bits = 0;
        if (!runner.support(format, bits) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
            continue;
        }
        const int family = BcCodec::family(format);
        const Bytes data = family_blocks(family, 0x99, static_cast<int>(kBlocksAcross * kBlocksAcross));
        Bytes back;
        std::string error;
        const std::string name = std::string(runner.label()) + " " + FormatTable::name(format);
        if (!runner.roundtrip(format, kEdge, kEdge, data, back, error) || back != data) {
            out.add(name + " blocks changed in an upload and readback" + (error.empty() ? std::string() : ": " + error));
        }
    }
}

Outcome vulkan_blocks(Context& context) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        if (!rig.features.textureCompressionBC) {
            return "textureCompressionBC is not supported";
        }
        Limited out(problems);
        for (int family = 1; family <= 7; ++family) {
            const FormatEntry* entry = FormatTable::find(BcCodec::unorm_format(family));
            const VkFormat format = static_cast<VkFormat>(entry->vulkan);
            const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            const std::string name = std::string("Vulkan ") + entry->name;
            if (!VulkanFormatIo::image_ok(rig, format, usage, kEdge, kEdge)) {
                out.add(name + " is not supported for transfers although textureCompressionBC is reported");
                continue;
            }
            const unsigned bb = static_cast<unsigned>(BcCodec::block_bytes(family));
            const Bytes a = family_blocks(family, 0xA0 + static_cast<std::uint64_t>(family), static_cast<int>(kBlocksAcross * kBlocksAcross));
            const Bytes b = family_blocks(family, 0xB0 + static_cast<std::uint64_t>(family), 4);
            VulkanImage image;
            Bytes back;
            VkResult result = VulkanFormatIo::make(rig, image, format, kEdge, kEdge, usage);
            if (result == VK_SUCCESS) {
                result = VulkanFormatIo::upload(rig, image, a, kEdge, kEdge);
            }
            if (result == VK_SUCCESS) {
                result = VulkanFormatIo::download(rig, image, a.size(), kEdge, kEdge, back);
            }
            if (result != VK_SUCCESS) {
                out.add(name + " transfer failed: " + vk::Kit::name(result));
                continue;
            }
            if (back != a) {
                out.add(name + " blocks changed in an upload and readback at byte " + std::to_string(Kit::first_difference(a, back)));
            }
            ImageRegion region;
            region.offset = {4, 4, 0};
            region.extent = {8, 8, 1};
            result = VulkanImageIo::write(rig, image, region, b);
            if (result == VK_SUCCESS) {
                result = VulkanFormatIo::download(rig, image, a.size(), kEdge, kEdge, back);
            }
            Bytes model = a;
            replace_blocks(model, b, bb, 1, 1, 0, 0, 2, 2, 2);
            if (result != VK_SUCCESS) {
                out.add(name + " partial update failed: " + vk::Kit::name(result));
            } else if (back != model) {
                out.add(name + " a block aligned partial update differs from the model at byte " + std::to_string(Kit::first_difference(model, back)));
            }
        }
        return {};
    });
}

void gl_blocks(Context& context, Problems& problems) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        problems.add("OpenGL: " + error);
        return;
    }
    const GlApi& api = rig.gl;
    Limited out(problems);
    for (int family = 1; family <= 7; ++family) {
        const FormatEntry* entry = FormatTable::find(BcCodec::unorm_format(family));
        const bool available = family <= 3   ? api.has_extension("GL_EXT_texture_compression_s3tc")
                               : family <= 5 ? api.supports(3, 0, "GL_ARB_texture_compression_rgtc")
                                             : api.supports(4, 2, "GL_ARB_texture_compression_bptc");
        if (!available || api.glCompressedTexImage2D == nullptr || api.glGetCompressedTexImage == nullptr) {
            continue;
        }
        const unsigned bb = static_cast<unsigned>(BcCodec::block_bytes(family));
        const Bytes a = family_blocks(family, 0xA0 + static_cast<std::uint64_t>(family), static_cast<int>(kBlocksAcross * kBlocksAcross));
        const std::string name = std::string("OpenGL ") + entry->name;
        api.drain_errors();
        unsigned texture = 0;
        api.glGenTextures(1, &texture);
        api.glBindTexture(glc::TEXTURE_2D, texture);
        api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
        api.glCompressedTexImage2D(glc::TEXTURE_2D, 0, entry->gl_internal, kEdge, kEdge, 0, static_cast<int>(a.size()), a.data());
        unsigned failure = api.glGetError();
        if (failure != 0) {
            out.add(name + " glCompressedTexImage2D failed with " + GlApi::error_name(failure));
        } else {
            Bytes back(a.size(), 0xCD);
            api.glGetCompressedTexImage(glc::TEXTURE_2D, 0, back.data());
            failure = api.glGetError();
            if (failure != 0) {
                out.add(name + " glGetCompressedTexImage failed with " + GlApi::error_name(failure));
            } else if (back != a) {
                out.add(name + " blocks changed in an upload and readback at byte " + std::to_string(Kit::first_difference(a, back)));
            }
            if (api.glCompressedTexSubImage2D != nullptr) {
                const Bytes b = family_blocks(family, 0xB0 + static_cast<std::uint64_t>(family), 4);
                api.glCompressedTexSubImage2D(glc::TEXTURE_2D, 0, 4, 4, 8, 8, entry->gl_internal, static_cast<int>(b.size()), b.data());
                failure = api.glGetError();
                api.glGetCompressedTexImage(glc::TEXTURE_2D, 0, back.data());
                Bytes model = a;
                replace_blocks(model, b, bb, 1, 1, 0, 0, 2, 2, 2);
                if (failure != 0) {
                    out.add(name + " glCompressedTexSubImage2D failed with " + GlApi::error_name(failure));
                } else if (back != model) {
                    out.add(name + " a block aligned partial update differs from the model at byte " + std::to_string(Kit::first_difference(model, back)));
                }
            }
        }
        api.glFinish();
        api.glBindTexture(glc::TEXTURE_2D, 0);
        api.glDeleteTextures(1, &texture);
    }
}

void merge(const char* api, const Outcome& outcome, Problems& problems, std::vector<std::string>& skips, bool& exercised) {
    if (outcome.status() == Status::Fail) {
        problems.add(std::string(api) + ": " + outcome.reason());
    } else if (outcome.status() == Status::Skip) {
        skips.push_back(std::string(api) + ": " + outcome.reason());
    } else {
        exercised = true;
    }
}


struct Size {
    unsigned width;
    unsigned height;
};

const Size kSizes[] = {{5, 7}, {1, 1}, {2, 2}, {3, 9}, {13, 6}, {4, 4}, {17, 5}, {64, 3}};

unsigned mip_count(const Size& size) {
    unsigned largest = std::max(size.width, size.height);
    unsigned levels = 1;
    while (largest > 1) {
        largest >>= 1;
        ++levels;
    }
    return levels;
}

unsigned mip_dim(unsigned value, unsigned mip) { return std::max(1u, value >> mip); }

std::size_t level_blocks(const Size& size, unsigned mip) {
    return static_cast<std::size_t>(BcCodec::blocks_across(mip_dim(size.width, mip))) * BcCodec::blocks_across(mip_dim(size.height, mip));
}

std::string size_text(const Size& size) { return std::to_string(size.width) + "x" + std::to_string(size.height); }

void d3d11_dimensions(D3d11Device& device, Limited& out, int& tested) {
    D3d11FormatRunner runner(device);
    const int families[] = {1, 3, 4, 7};
    for (int family : families) {
        const DXGI_FORMAT format = BcCodec::unorm_format(family);
        UINT bits = 0;
        if (!runner.support(format, bits) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
            continue;
        }
        const unsigned bb = static_cast<unsigned>(BcCodec::block_bytes(family));
        for (const Size& size : kSizes) {
            ++tested;
            const std::string name = "D3D11 " + std::string(FormatTable::name(format)) + " " + size_text(size);
            const unsigned mips = mip_count(size);
            std::vector<Bytes> levels;
            std::vector<D3D11_SUBRESOURCE_DATA> init;
            for (unsigned m = 0; m < mips; ++m) {
                levels.push_back(family_blocks(family, 0x500 + m + static_cast<std::uint64_t>(family) * 16, static_cast<int>(level_blocks(size, m))));
            }
            for (unsigned m = 0; m < mips; ++m) {
                init.push_back({levels[m].data(), BcCodec::blocks_across(mip_dim(size.width, m)) * bb, 0});
            }
            HRESULT result = S_OK;
            Com<ID3D11Texture2D> texture =
                D3d11Tools::texture(device, D3d11Tools::desc2d(size.width, size.height, format, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, mips), init.data(), &result);
            if (!texture) {
                out.add(name + " cannot be created with " + std::to_string(mips) + " mips: " + D3d11Tools::hr(result));
                continue;
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Com<ID3D11Texture2D> staging;
            if (FAILED(device.device->CreateTexture2D(&desc, nullptr, staging.put()))) {
                out.add(name + " has no staging copy");
                continue;
            }
            device.context->CopyResource(staging.get(), texture.get());
            for (unsigned m = 0; m < mips; ++m) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(device.context->Map(staging.get(), m, D3D11_MAP_READ, 0, &mapped))) {
                    out.add(name + " mip " + std::to_string(m) + " cannot be mapped");
                    continue;
                }
                const unsigned row_bytes = BcCodec::blocks_across(mip_dim(size.width, m)) * bb;
                const unsigned rows = BcCodec::blocks_across(mip_dim(size.height, m));
                if (mapped.RowPitch < row_bytes) {
                    out.add(name + " mip " + std::to_string(m) + " has a row pitch of " + std::to_string(mapped.RowPitch) + ", less than the " +
                            std::to_string(row_bytes) + " bytes of its blocks");
                } else {
                    Bytes got = D3d11Tools::block_bytes(mapped.pData, row_bytes, rows, mapped.RowPitch);
                    if (got != levels[m]) {
                        out.add(name + " mip " + std::to_string(m) + " blocks differ from the uploaded ones at byte " +
                                std::to_string(Kit::first_difference(levels[m], got)));
                    }
                }
                device.context->Unmap(staging.get(), m);
            }
            if (BcCodec::has_reference(format) && (bits & Kit::kSampleBits) != 0) {
                sample_against_decoder(runner, out, family, format, levels[0], size.width, size.height, name);
            }
        }
        const std::string label = "D3D11 " + std::string(FormatTable::name(format));
        const auto refused = [&](const char* what, UINT width, UINT height, UINT bind, UINT samples) {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(width, height, format, bind, D3D11_USAGE_DEFAULT, 0, 1, 1, samples);
            Com<ID3D11Texture2D> texture;
            const HRESULT result = device.device->CreateTexture2D(&desc, nullptr, texture.put());
            if (SUCCEEDED(result)) {
                out.add(label + " " + what + " was accepted");
            } else if (result != E_INVALIDARG) {
                out.add(label + " " + what + " failed with " + D3d11Tools::hr(result) + ", expected E_INVALIDARG");
            }
        };
        refused("with a zero width", 0, 4, D3D11_BIND_SHADER_RESOURCE, 1);
        refused("with a zero height", 4, 0, D3D11_BIND_SHADER_RESOURCE, 1);
        refused("as a render target", 8, 8, D3D11_BIND_RENDER_TARGET, 1);
        refused("as a depth stencil", 8, 8, D3D11_BIND_DEPTH_STENCIL, 1);
        refused("as an unordered access texture", 8, 8, D3D11_BIND_UNORDERED_ACCESS, 1);
        refused("with 4 samples", 8, 8, D3D11_BIND_SHADER_RESOURCE, 4);
    }
}

void d3d12_dimensions(D3d12Rig& rig, Limited& out, int& tested) {
    const int families[] = {1, 3, 4, 7};
    D3d12FormatRunner runner(rig);
    for (int family : families) {
        const DXGI_FORMAT format = BcCodec::unorm_format(family);
        UINT bits = 0;
        if (!runner.support(format, bits) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
            continue;
        }
        const unsigned bb = static_cast<unsigned>(BcCodec::block_bytes(family));
        for (const Size& size : kSizes) {
            ++tested;
            const std::string name = "D3D12 " + std::string(FormatTable::name(format)) + " " + size_text(size);
            const unsigned mips = mip_count(size);
            HRESULT result = S_OK;
            ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(size.width, size.height, format, D3D12_RESOURCE_FLAG_NONE, mips),
                                                          D3D12_RESOURCE_STATE_COMMON, nullptr, &result);
            if (texture == nullptr) {
                out.add(name + " cannot be created with " + std::to_string(mips) + " mips: " + D3d12Tools::hr(result));
                continue;
            }
            const D3D12_RESOURCE_DESC desc = texture->GetDesc();
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mips);
            std::vector<UINT> rows(mips);
            std::vector<UINT64> row_sizes(mips);
            UINT64 total = 0;
            rig.device()->GetCopyableFootprints(&desc, 0, mips, 0, footprints.data(), rows.data(), row_sizes.data(), &total);
            UINT64 previous_end = 0;
            for (unsigned m = 0; m < mips; ++m) {
                const unsigned want_rows = BcCodec::blocks_across(mip_dim(size.height, m));
                const UINT64 want_row_size = static_cast<UINT64>(BcCodec::blocks_across(mip_dim(size.width, m))) * bb;
                const std::string level = name + " mip " + std::to_string(m);
                if (rows[m] != want_rows) {
                    out.add(level + " has " + std::to_string(rows[m]) + " rows, expected " + std::to_string(want_rows));
                }
                if (row_sizes[m] != want_row_size) {
                    out.add(level + " has a row size of " + std::to_string(row_sizes[m]) + " bytes, expected " + std::to_string(want_row_size));
                }
                if (footprints[m].Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0 || footprints[m].Footprint.RowPitch < row_sizes[m]) {
                    out.add(level + " has a row pitch of " + std::to_string(footprints[m].Footprint.RowPitch) + " that is not a 256 byte multiple of at least the row size");
                }
                if (footprints[m].Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0 || footprints[m].Offset < previous_end) {
                    out.add(level + " is placed at offset " + std::to_string(footprints[m].Offset) + ", not aligned to 512 or overlapping the previous mip");
                }
                previous_end = footprints[m].Offset + static_cast<UINT64>(footprints[m].Footprint.RowPitch) * (rows[m] - 1) + row_sizes[m];
            }
            if (total < previous_end) {
                out.add(name + " reports a total of " + std::to_string(total) + " bytes, less than the end of the last mip at " + std::to_string(previous_end));
            }
            if (mips > 1 && (rows[mips - 1] != 1 || row_sizes[mips - 1] != bb)) {
                out.add(name + " last mip is not exactly one block");
            }
            const Bytes data = family_blocks(family, 0x600 + static_cast<std::uint64_t>(family), static_cast<int>(level_blocks(size, 0)));
            std::string error;
            TexelImage image;
            if (!D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COMMON, 0, data.data(), error) ||
                !D3d12Tools::read_texture(rig, texture, D3D12_RESOURCE_STATE_COMMON, 0, image, error)) {
                out.add(name + " copy failed: " + error);
            } else if (image.data != data) {
                out.add(name + " blocks of mip 0 changed in an upload and readback at byte " + std::to_string(Kit::first_difference(data, image.data)));
            }
            if (BcCodec::has_reference(format) && (bits & Kit::kSampleBits) != 0) {
                sample_against_decoder(runner, out, family, format, data, size.width, size.height, name);
            }
        }
        const std::string label = "D3D12 " + std::string(FormatTable::name(format));
        const auto refused = [&](const char* what, D3D12_RESOURCE_DESC desc) {
            ID3D12Resource* texture = nullptr;
            const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
            const HRESULT result = rig.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                        IID_PPV_ARGS(&texture));
            if (SUCCEEDED(result)) {
                out.add(label + " " + what + " was accepted");
                rig.hold(Com<ID3D12Resource>(texture));
            } else if (result != E_INVALIDARG) {
                out.add(label + " " + what + " failed with " + D3d12Tools::hr(result) + ", expected E_INVALIDARG");
            }
        };
        refused("with a zero width", D3d12Tools::texture_desc(0, 4, format));
        refused("as a render target", D3d12Tools::texture_desc(8, 8, format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET));
        refused("as a depth stencil", D3d12Tools::texture_desc(8, 8, format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
        refused("with 4 samples", D3d12Tools::texture_desc(8, 8, format, D3D12_RESOURCE_FLAG_NONE, 1, 1, 4));
    }
}

Outcome vulkan_dimensions(Context& context) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        if (!rig.features.textureCompressionBC) {
            return "textureCompressionBC is not supported";
        }
        Limited out(problems);
        for (int family : {1, 3, 4, 7}) {
            const FormatEntry* entry = FormatTable::find(BcCodec::unorm_format(family));
            const VkFormat format = static_cast<VkFormat>(entry->vulkan);
            const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            for (const Size& size : kSizes) {
                const std::string name = std::string("Vulkan ") + entry->name + " " + size_text(size);
                const unsigned mips = mip_count(size);
                VulkanImage image;
                VkResult result = VulkanFormatIo::make(rig, image, format, size.width, size.height, usage, mips);
                if (result != VK_SUCCESS) {
                    out.add(name + " cannot be created: " + vk::Kit::name(result));
                    continue;
                }
                for (unsigned m = 0; m < mips; ++m) {
                    const Bytes data = family_blocks(family, 0x700 + m + static_cast<std::uint64_t>(family) * 16, static_cast<int>(level_blocks(size, m)));
                    Bytes back;
                    result = VulkanFormatIo::upload(rig, image, data, mip_dim(size.width, m), mip_dim(size.height, m), m);
                    if (result == VK_SUCCESS) {
                        result = VulkanFormatIo::download(rig, image, data.size(), mip_dim(size.width, m), mip_dim(size.height, m), back, m);
                    }
                    if (result != VK_SUCCESS) {
                        out.add(name + " mip " + std::to_string(m) + " transfer failed: " + vk::Kit::name(result));
                    } else if (back != data) {
                        out.add(name + " mip " + std::to_string(m) + " blocks changed at byte " + std::to_string(Kit::first_difference(data, back)));
                    }
                }
            }
        }
        return {};
    });
}

void gl_dimensions(Context& context, Problems& problems) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        problems.add("OpenGL: " + error);
        return;
    }
    const GlApi& api = rig.gl;
    Limited out(problems);
    for (int family : {1, 3, 4, 7}) {
        const bool available = family <= 3   ? api.has_extension("GL_EXT_texture_compression_s3tc")
                               : family <= 5 ? api.supports(3, 0, "GL_ARB_texture_compression_rgtc")
                                             : api.supports(4, 2, "GL_ARB_texture_compression_bptc");
        if (!available || api.glCompressedTexImage2D == nullptr || api.glGetCompressedTexImage == nullptr) {
            continue;
        }
        const FormatEntry* entry = FormatTable::find(BcCodec::unorm_format(family));
        const unsigned bb = static_cast<unsigned>(BcCodec::block_bytes(family));
        for (const Size& size : kSizes) {
            const std::string name = std::string("OpenGL ") + entry->name + " " + size_text(size);
            api.drain_errors();
            unsigned texture = 0;
            api.glGenTextures(1, &texture);
            api.glBindTexture(glc::TEXTURE_2D, texture);
            api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
            api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
            const unsigned mips = mip_count(size);
            for (unsigned m = 0; m < mips; ++m) {
                const Bytes data = family_blocks(family, 0x800 + m + static_cast<std::uint64_t>(family) * 16, static_cast<int>(level_blocks(size, m)));
                const int width = static_cast<int>(mip_dim(size.width, m));
                const int height = static_cast<int>(mip_dim(size.height, m));
                api.glCompressedTexImage2D(glc::TEXTURE_2D, static_cast<int>(m), entry->gl_internal, width, height, 0, static_cast<int>(data.size()), data.data());
                unsigned failure = api.glGetError();
                if (failure != 0) {
                    out.add(name + " level " + std::to_string(m) + " with the documented image size of " + std::to_string(data.size()) + " failed with " +
                            GlApi::error_name(failure));
                    continue;
                }
                int stored = 0;
                api.glGetTexLevelParameteriv(glc::TEXTURE_2D, static_cast<int>(m), glc::TEXTURE_COMPRESSED_IMAGE_SIZE, &stored);
                if (static_cast<std::size_t>(stored) != data.size()) {
                    out.add(name + " level " + std::to_string(m) + " reports a compressed size of " + std::to_string(stored) + ", expected " + std::to_string(data.size()));
                }
                Bytes back(data.size(), 0xCD);
                api.glGetCompressedTexImage(glc::TEXTURE_2D, static_cast<int>(m), back.data());
                if (api.glGetError() != 0 || back != data) {
                    out.add(name + " level " + std::to_string(m) + " blocks changed in an upload and readback");
                }
                if (m == 0) {
                    api.glCompressedTexImage2D(glc::TEXTURE_2D, 0, entry->gl_internal, width, height, 0, static_cast<int>(data.size() + bb), data.data());
                    if (api.glGetError() != glc::INVALID_VALUE) {
                        out.add(name + " accepted an image size of " + std::to_string(data.size() + bb) + " bytes, expected GL_INVALID_VALUE");
                    }
                    api.glCompressedTexImage2D(glc::TEXTURE_2D, 0, entry->gl_internal, width, height, 0, static_cast<int>(data.size() - 1), data.data());
                    if (api.glGetError() != glc::INVALID_VALUE) {
                        out.add(name + " accepted an image size of " + std::to_string(data.size() - 1) + " bytes, expected GL_INVALID_VALUE");
                    }
                }
            }
            api.glFinish();
            api.glBindTexture(glc::TEXTURE_2D, 0);
            api.glDeleteTextures(1, &texture);
        }
    }
}

}  // namespace

Outcome BcChecks::bc_blocks(Context& context) {
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
            runner_blocks(runner, out, tested, [&](DXGI_FORMAT format, int family, const std::string& name) { d3d11_copies(device, out, format, family, name); });
            context.measure("fmt.bc.d3d11.formats", tested);
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
            runner_blocks(runner, out, tested, [&](DXGI_FORMAT format, int family, const std::string& name) { d3d12_partial(rig, out, format, family, name); });
            context.measure("fmt.bc.d3d12.formats", tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    if (Kit::enabled("vulkan")) {
        merge("Vulkan", vulkan_blocks(context), problems, skips, exercised);
    } else {
        skips.push_back("Vulkan excluded");
    }
    if (Kit::enabled("gl")) {
        gl_blocks(context, problems);
        exercised = true;
    } else {
        skips.push_back("OpenGL excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

Outcome BcChecks::bc_dimensions(Context& context) {
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
            int tested = 0;
            d3d11_dimensions(device, out, tested);
            context.measure("fmt.bcsize.d3d11.cases", tested);
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
            int tested = 0;
            d3d12_dimensions(rig, out, tested);
            context.measure("fmt.bcsize.d3d12.cases", tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    if (Kit::enabled("vulkan")) {
        merge("Vulkan", vulkan_dimensions(context), problems, skips, exercised);
    } else {
        skips.push_back("Vulkan excluded");
    }
    if (Kit::enabled("gl")) {
        gl_dimensions(context, problems);
        exercised = true;
    } else {
        skips.push_back("OpenGL excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::fmt
