#include "render/vk/formatdata.hpp"
#include "render/vk/rig.hpp"

namespace lsw::render::vk {

namespace {

struct TexFormat {
    const char* name;
    VkFormat format;
    char cls;
    unsigned bytes;
    Kind kind;
    unsigned bits[4];
    unsigned block_w;
    unsigned block_h;
    const char* feature;
};

std::vector<TexFormat> tex_formats() {
    using K = Kind;
    return {
        {"r8_unorm", VK_FORMAT_R8_UNORM, 'f', 1, K::Unorm, {8, 0, 0, 0}, 1, 1, nullptr},
        {"r8g8_unorm", VK_FORMAT_R8G8_UNORM, 'f', 2, K::Unorm, {8, 8, 0, 0}, 1, 1, nullptr},
        {"r8g8b8a8_unorm", VK_FORMAT_R8G8B8A8_UNORM, 'f', 4, K::Unorm, {8, 8, 8, 8}, 1, 1, nullptr},
        {"b8g8r8a8_unorm", VK_FORMAT_B8G8R8A8_UNORM, 'f', 4, K::Unorm, {8, 8, 8, 8}, 1, 1, nullptr},
        {"r8g8b8a8_srgb", VK_FORMAT_R8G8B8A8_SRGB, 'f', 4, K::Unorm, {8, 8, 8, 8}, 1, 1, nullptr},
        {"b8g8r8a8_srgb", VK_FORMAT_B8G8R8A8_SRGB, 'f', 4, K::Unorm, {8, 8, 8, 8}, 1, 1, nullptr},
        {"r8g8b8a8_snorm", VK_FORMAT_R8G8B8A8_SNORM, 'f', 4, K::Snorm, {8, 8, 8, 8}, 1, 1, nullptr},
        {"r8_uint", VK_FORMAT_R8_UINT, 'u', 1, K::Uint, {8, 0, 0, 0}, 1, 1, nullptr},
        {"r8g8b8a8_uint", VK_FORMAT_R8G8B8A8_UINT, 'u', 4, K::Uint, {8, 8, 8, 8}, 1, 1, nullptr},
        {"r8g8b8a8_sint", VK_FORMAT_R8G8B8A8_SINT, 'i', 4, K::Sint, {8, 8, 8, 8}, 1, 1, nullptr},
        {"r16_unorm", VK_FORMAT_R16_UNORM, 'f', 2, K::Unorm, {16, 0, 0, 0}, 1, 1, nullptr},
        {"r16g16_unorm", VK_FORMAT_R16G16_UNORM, 'f', 4, K::Unorm, {16, 16, 0, 0}, 1, 1, nullptr},
        {"r16g16b16a16_unorm", VK_FORMAT_R16G16B16A16_UNORM, 'f', 8, K::Unorm, {16, 16, 16, 16}, 1, 1, nullptr},
        {"r16g16b16a16_snorm", VK_FORMAT_R16G16B16A16_SNORM, 'f', 8, K::Snorm, {16, 16, 16, 16}, 1, 1, nullptr},
        {"r16g16b16a16_uint", VK_FORMAT_R16G16B16A16_UINT, 'u', 8, K::Uint, {16, 16, 16, 16}, 1, 1, nullptr},
        {"r16_sint", VK_FORMAT_R16_SINT, 'i', 2, K::Sint, {16, 0, 0, 0}, 1, 1, nullptr},
        {"r16_sfloat", VK_FORMAT_R16_SFLOAT, 'f', 2, K::Float, {16, 0, 0, 0}, 1, 1, nullptr},
        {"r16g16b16a16_sfloat", VK_FORMAT_R16G16B16A16_SFLOAT, 'f', 8, K::Float, {16, 16, 16, 16}, 1, 1, nullptr},
        {"r32_uint", VK_FORMAT_R32_UINT, 'u', 4, K::Uint, {32, 0, 0, 0}, 1, 1, nullptr},
        {"r32g32b32a32_uint", VK_FORMAT_R32G32B32A32_UINT, 'u', 16, K::Uint, {32, 32, 32, 32}, 1, 1, nullptr},
        {"r32_sint", VK_FORMAT_R32_SINT, 'i', 4, K::Sint, {32, 0, 0, 0}, 1, 1, nullptr},
        {"r32_sfloat", VK_FORMAT_R32_SFLOAT, 'f', 4, K::Float, {32, 0, 0, 0}, 1, 1, nullptr},
        {"r32g32_sfloat", VK_FORMAT_R32G32_SFLOAT, 'f', 8, K::Float, {32, 32, 0, 0}, 1, 1, nullptr},
        {"r32g32b32a32_sfloat", VK_FORMAT_R32G32B32A32_SFLOAT, 'f', 16, K::Float, {32, 32, 32, 32}, 1, 1, nullptr},
        {"a2b10g10r10_unorm", VK_FORMAT_A2B10G10R10_UNORM_PACK32, 'f', 4, K::Unorm, {10, 10, 10, 2}, 1, 1, nullptr},
        {"a2b10g10r10_uint", VK_FORMAT_A2B10G10R10_UINT_PACK32, 'u', 4, K::Uint, {10, 10, 10, 2}, 1, 1, nullptr},
        {"a2r10g10b10_unorm", VK_FORMAT_A2R10G10B10_UNORM_PACK32, 'f', 4, K::Unorm, {10, 10, 10, 2}, 1, 1, nullptr},
        {"b10g11r11_ufloat", VK_FORMAT_B10G11R11_UFLOAT_PACK32, 'f', 4, K::Float11, {11, 11, 10, 0}, 1, 1, nullptr},
        {"e5b9g9r9_ufloat", VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 'f', 4, K::Unorm, {9, 9, 9, 5}, 1, 1, nullptr},
        {"r5g6b5_unorm", VK_FORMAT_R5G6B5_UNORM_PACK16, 'f', 2, K::Unorm, {5, 6, 5, 0}, 1, 1, nullptr},
        {"b5g6r5_unorm", VK_FORMAT_B5G6R5_UNORM_PACK16, 'f', 2, K::Unorm, {5, 6, 5, 0}, 1, 1, nullptr},
        {"r4g4b4a4_unorm", VK_FORMAT_R4G4B4A4_UNORM_PACK16, 'f', 2, K::Unorm, {4, 4, 4, 4}, 1, 1, nullptr},
        {"b4g4r4a4_unorm", VK_FORMAT_B4G4R4A4_UNORM_PACK16, 'f', 2, K::Unorm, {4, 4, 4, 4}, 1, 1, nullptr},
        {"a1r5g5b5_unorm", VK_FORMAT_A1R5G5B5_UNORM_PACK16, 'f', 2, K::Unorm, {5, 5, 5, 1}, 1, 1, nullptr},
        {"r5g5b5a1_unorm", VK_FORMAT_R5G5B5A1_UNORM_PACK16, 'f', 2, K::Unorm, {5, 5, 5, 1}, 1, 1, nullptr},
        {"bc1_rgb_unorm", VK_FORMAT_BC1_RGB_UNORM_BLOCK, 'f', 8, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc1_rgba_srgb", VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 'f', 8, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc2_unorm", VK_FORMAT_BC2_UNORM_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc3_unorm", VK_FORMAT_BC3_UNORM_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc4_unorm", VK_FORMAT_BC4_UNORM_BLOCK, 'f', 8, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc4_snorm", VK_FORMAT_BC4_SNORM_BLOCK, 'f', 8, K::Snorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc5_unorm", VK_FORMAT_BC5_UNORM_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc5_snorm", VK_FORMAT_BC5_SNORM_BLOCK, 'f', 16, K::Snorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc6h_ufloat", VK_FORMAT_BC6H_UFLOAT_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"bc7_unorm", VK_FORMAT_BC7_UNORM_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionBC"},
        {"etc2_rgb8", VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, 'f', 8, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionETC2"},
        {"etc2_rgba8", VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionETC2"},
        {"eac_r11", VK_FORMAT_EAC_R11_UNORM_BLOCK, 'f', 8, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionETC2"},
        {"astc_4x4_unorm", VK_FORMAT_ASTC_4x4_UNORM_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 4, 4, "textureCompressionASTC_LDR"},
        {"astc_8x8_unorm", VK_FORMAT_ASTC_8x8_UNORM_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 8, 8, "textureCompressionASTC_LDR"},
        {"astc_12x12_srgb", VK_FORMAT_ASTC_12x12_SRGB_BLOCK, 'f', 16, K::Unorm, {8, 8, 8, 8}, 12, 12, "textureCompressionASTC_LDR"},
    };
}

struct SampleConstants {
    float scale[4];
    float bias[4];
    std::int32_t info[4];
};

void register_formats() {
    for (const TexFormat& entry : tex_formats()) {
        CaseSpec spec("tex", std::string("fmt_") + entry.name);
        spec.tolerance(Tolerance::tight(1));
        if (entry.feature != nullptr) {
            spec.feature(entry.feature);
        }
        Cases::add(
            std::move(spec),
            [entry](Rig& rig, CaptureSet& out) {
                constexpr std::uint32_t kSize = 48;
                Target& target = rig.make_target(TargetDesc{});
                TextureDesc desc;
                desc.width = desc.height = kSize;
                desc.format = entry.format;
                const Texture texture = rig.texture(desc);
                const std::uint32_t blocks_x = (kSize + entry.block_w - 1) / entry.block_w;
                const std::uint32_t blocks_y = (kSize + entry.block_h - 1) / entry.block_h;
                FormatEntry data_entry{entry.name, entry.format, entry.cls, entry.bytes, entry.kind, {entry.bits[0], entry.bits[1], entry.bits[2], entry.bits[3]}};
                const std::vector<std::uint8_t> bytes = make_format_bytes(data_entry, blocks_x * blocks_y);
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {kSize, kSize, 1};
                std::string upload_error;
                rig.must(rig.kit.upload_texture(texture, {region}, bytes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, upload_error), upload_error);
                const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
                const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
                DescriptorWrite write;
                write.binding = 0;
                write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.image = {sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                const VkDescriptorSet set = rig.dset(set_layout, {write});
                const VkPipelineLayout layout = rig.playout({set_layout}, sizeof(SampleConstants), VK_SHADER_STAGE_FRAGMENT_BIT);
                PipelineDesc pipe;
                pipe.vertex = rig.sh("full.vert");
                const char shader[] = {'t', 'e', 'x', '_', entry.cls, '.', 'f', 'r', 'a', 'g', 0};
                pipe.fragment = rig.sh(shader);
                pipe.layout = layout;
                pipe.pass = target.clear_pass;
                const VkPipeline pipeline = rig.pipe(pipe);
                SampleConstants constants{};
                make_scale(data_entry, constants.scale, constants.bias);
                constants.info[0] = constants.info[1] = static_cast<std::int32_t>(kSize);
                out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                    rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, &constants, sizeof constants);
                    rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                }, layout, {set}));
            },
            [entry](Kit& kit) { return Cases::need_format(kit, entry.format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "sampled image format"); });
    }
}

const int registered = (register_formats(), 0);

}  // namespace

}  // namespace lsw::render::vk
