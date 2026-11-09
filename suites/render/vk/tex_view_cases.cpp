#include "render/common/pattern.hpp"
#include "render/vk/formatdata.hpp"
#include "render/vk/quadkit.hpp"

#include <cstring>

namespace lsw::render::vk {

namespace {

CaseSpec spec_of(const std::string& name) {
    CaseSpec spec("tex", name);
    spec.tolerance(Tolerance::exact());
    return spec;
}

struct SampleConstants {
    float scale[4];
    float bias[4];
    std::int32_t info[4];
};

Image bytes_as_image(const std::vector<std::uint8_t>& bytes, std::uint32_t width) {
    Image image(width, static_cast<std::uint32_t>(bytes.size() / width), PixelKind::R8Uint);
    std::memcpy(image.bytes().data(), bytes.data(), bytes.size());
    return image;
}

void register_views() {
    for (int listed = 0; listed < 2; ++listed) {
    CaseSpec alias = spec_of(listed != 0 ? "view_mutable_format_list_srgb_unorm" : "view_mutable_srgb_unorm_alias");
    alias.audit_ok(listed != 0 ? "the same sRGB and UNORM views as view_mutable_srgb_unorm_alias, created from an image that lists them in VkImageFormatListCreateInfo; the subject is the listed creation path" : "views the unlisted mutable image; view_mutable_format_list_srgb_unorm is the same picture through the format list path");
    if (listed != 0) {
        alias.min_version(1, 2);
    }
    Cases::add(std::move(alias), [listed](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        TextureDesc desc;
        desc.width = desc.height = 16;
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        const VkFormat listed_formats[2] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB};
        VkImageFormatListCreateInfo format_list{};
        format_list.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
        format_list.viewFormatCount = 2;
        format_list.pViewFormats = listed_formats;
        if (listed != 0) {
            desc.next = &format_list;
        }
        const Texture texture = rig.texture(desc);
        std::vector<std::uint8_t> bytes(16 * 16 * 4);
        for (unsigned y = 0; y < 16; ++y) {
            for (unsigned x = 0; x < 16; ++x) {
                const std::size_t at = (y * 16 + x) * 4;
                bytes[at] = static_cast<std::uint8_t>(x * 16);
                bytes[at + 1] = static_cast<std::uint8_t>(y * 16);
                bytes[at + 2] = 128;
                bytes[at + 3] = 255;
            }
        }
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {16, 16, 1};
        std::string error;
        rig.must(kit.upload_texture(texture, {region}, bytes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error), error);
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = texture.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8G8B8A8_SRGB;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView srgb = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateImageView(kit.device, &view, nullptr, &srgb), "vkCreateImageView");
        kit.defer([&kit, srgb] { kit.api.vkDestroyImageView(kit.device, srgb, nullptr); });
        const VkSampler sampler = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorSet sets[2] = {
            rig.dset(set_layout, {DescriptorWrite{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {}, {sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}}),
            rig.dset(set_layout, {DescriptorWrite{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {}, {sampler, srgb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}})};
        const VkPipelineLayout layout = rig.playout({set_layout}, sizeof(SampleConstants), VK_SHADER_STAGE_FRAGMENT_BIT);
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("tex_f.frag");
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        const SampleConstants constants{{1, 1, 1, 1}, {0, 0, 0, 0}, {16, 16, 0, 0}};
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, &constants, sizeof constants);
            for (unsigned i = 0; i < 2; ++i) {
                rig.viewport(cb, static_cast<float>(i) * 64.0f, 0, 64, 128);
                kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &sets[i], 0, nullptr);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            }
        }, layout));
    }, [](Kit& kit) { return Cases::need_format(kit, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "RGBA8 sRGB sampling"); });
    }
    CaseSpec block = spec_of("view_uncompressed_block_texel");
    block.feature("textureCompressionBC").min_version(1, 1);
    Cases::add(std::move(block), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        TextureDesc desc;
        desc.width = desc.height = 16;
        desc.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
        desc.create_view = false;
        const Texture texture = rig.texture(desc);
        const FormatEntry entry{"bc1", VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 'u', 8, Kind::Uint, {8, 8, 8, 8}};
        const auto blocks = make_format_bytes(entry, 16);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {16, 16, 1};
        std::string error;
        rig.must(kit.upload_texture(texture, {region}, blocks, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error), error);
        VkImageViewUsageCreateInfo usage{};
        usage.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        usage.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.pNext = &usage;
        view.image = texture.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R32G32_UINT;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView uint_view = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateImageView(kit.device, &view, nullptr, &uint_view), "vkCreateImageView");
        kit.defer([&kit, uint_view] { kit.api.vkDestroyImageView(kit.device, uint_view, nullptr); });
        const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorSet set = rig.dset(set_layout, {DescriptorWrite{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {}, {sampler, uint_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}});
        const VkPipelineLayout layout = rig.playout({set_layout}, sizeof(SampleConstants), VK_SHADER_STAGE_FRAGMENT_BIT);
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("tex_u.frag");
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        const SampleConstants constants{{1.0f / 4294967295.0f, 1.0f / 4294967295.0f, 1, 1}, {0, 0, 0, 0}, {4, 4, 0, 0}};
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, &constants, sizeof constants);
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        }, layout, {set}));
    }, [](Kit& kit) { return Cases::need_format(kit, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "BC1 sampling"); });
}

void run_atomics(Rig& rig, CaptureSet& out, VkFormat format, const char* shader, std::uint32_t texel_bytes) {
    Kit& kit = rig.kit;
    TextureDesc desc;
    desc.width = desc.height = 4;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const Texture texture = rig.texture(desc);
    const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}});
    const VkDescriptorSet set = rig.dset(set_layout, {DescriptorWrite{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {}, {VK_NULL_HANDLE, texture.view, VK_IMAGE_LAYOUT_GENERAL}}});
    const VkPipelineLayout layout = rig.playout({set_layout});
    const VkPipeline pipeline = rig.compute(shader, layout);
    const Buffer dst = rig.blank(4 * 4 * texel_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    rig.run([&](VkCommandBuffer cb) {
        kit.barrier(cb, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        kit.api.vkCmdClearColorImage(cb, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        kit.barrier(cb, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        kit.api.vkCmdDispatch(cb, 8, 1, 1);
        kit.barrier(cb, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {4, 4, 1};
        kit.api.vkCmdCopyImageToBuffer(cb, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.buffer, 1, &region);
    });
    out.add("", bytes_as_image(rig.bytes(dst, 0, 4 * 4 * texel_bytes), 4 * texel_bytes));
}

void register_atomics_and_ycbcr() {
    Cases::add(spec_of("storage_image_atomic_add"), [](Rig& rig, CaptureSet& out) { run_atomics(rig, out, VK_FORMAT_R32_UINT, "atomics.comp", 4); },
               [](Kit& kit) { return Cases::need_format(kit, VK_FORMAT_R32_UINT, VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT, "R32 uint storage atomics"); });
    CaseSpec int64 = spec_of("storage_image_atomic_int64");
    int64.extension("VK_EXT_shader_image_atomic_int64").feature("shaderInt64");
    Cases::add(std::move(int64), [](Rig& rig, CaptureSet& out) { run_atomics(rig, out, VK_FORMAT_R64_UINT, "atomics64.comp", 8); },
               [](Kit& kit) -> std::string {
                   const auto f = kit.query_features<VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT);
                   if (f.shaderImageInt64Atomics == VK_FALSE) {
                       return "shaderImageInt64Atomics";
                   }
                   return Cases::need_format(kit, VK_FORMAT_R64_UINT, VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT, "R64 uint storage atomics");
               });
    for (int three = 0; three < 2; ++three) {
    const VkFormat ycbcr_format = three != 0 ? VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM : VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    CaseSpec ycbcr = spec_of(three != 0 ? "ycbcr_3plane_420_conversion" : "ycbcr_nv12_conversion");
    ycbcr.min_version(1, 1).tolerance(Tolerance::exact());
    ycbcr.audit_ok(three != 0 ? "the same picture as the NV12 row stored in three planes; the subject is the plane layout" : "the same picture as the 3 plane row stored in two interleaved planes; the subject is the plane layout");
    Cases::add(std::move(ycbcr), [three, ycbcr_format](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        VkSamplerYcbcrConversionCreateInfo conv_info{};
        conv_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
        conv_info.format = ycbcr_format;
        conv_info.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
        conv_info.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
        conv_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        conv_info.xChromaOffset = conv_info.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
        conv_info.chromaFilter = VK_FILTER_LINEAR;
        VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateSamplerYcbcrConversion(kit.device, &conv_info, nullptr, &conversion), "vkCreateSamplerYcbcrConversion");
        kit.defer([&kit, conversion] { kit.api.vkDestroySamplerYcbcrConversion(kit.device, conversion, nullptr); });
        VkSamplerYcbcrConversionInfo conv_ref{};
        conv_ref.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
        conv_ref.conversion = conversion;
        TextureDesc desc;
        desc.width = desc.height = 32;
        desc.format = ycbcr_format;
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        desc.create_view = false;
        const Texture texture = rig.texture(desc);
        std::vector<std::uint8_t> bytes(32 * 32 + 16 * 16 * 2);
        for (unsigned y = 0; y < 32; ++y) {
            for (unsigned x = 0; x < 32; ++x) {
                bytes[y * 32 + x] = static_cast<std::uint8_t>(40 + ((x / 4 + y / 4) % 2) * 120 + x);
            }
        }
        for (unsigned y = 0; y < 16; ++y) {
            for (unsigned x = 0; x < 16; ++x) {
                if (three != 0) {
                    bytes[32 * 32 + y * 16 + x] = static_cast<std::uint8_t>(90 + x * 8);
                    bytes[32 * 32 + 256 + y * 16 + x] = static_cast<std::uint8_t>(200 - y * 8);
                } else {
                    bytes[32 * 32 + (y * 16 + x) * 2] = static_cast<std::uint8_t>(90 + x * 8);
                    bytes[32 * 32 + (y * 16 + x) * 2 + 1] = static_cast<std::uint8_t>(200 - y * 8);
                }
            }
        }
        VkBufferImageCopy regions[3]{};
        regions[0].imageSubresource = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 0, 1};
        regions[0].imageExtent = {32, 32, 1};
        regions[1].bufferOffset = 32 * 32;
        regions[1].imageSubresource = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 0, 1};
        regions[1].imageExtent = {16, 16, 1};
        regions[2].bufferOffset = 32 * 32 + 256;
        regions[2].imageSubresource = {VK_IMAGE_ASPECT_PLANE_2_BIT, 0, 0, 1};
        regions[2].imageExtent = {16, 16, 1};
        Buffer staging;
        std::string error;
        rig.must(kit.make_buffer_with(staging, bytes.data(), bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, error), error);
        rig.run([&](VkCommandBuffer cb) {
            kit.barrier(cb, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            kit.api.vkCmdCopyBufferToImage(cb, staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, three != 0 ? 3 : 2, regions);
            kit.barrier(cb, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.pNext = &conv_ref;
        view.image = texture.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = ycbcr_format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView image_view = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateImageView(kit.device, &view, nullptr, &image_view), "vkCreateImageView");
        kit.defer([&kit, image_view] { kit.api.vkDestroyImageView(kit.device, image_view, nullptr); });
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.pNext = &conv_ref;
        sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxAnisotropy = 1.0f;
        const VkSampler sampler = rig.sampler(sampler_info);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler}});
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateDescriptorPool(kit.device, &pool_info, nullptr, &pool), "vkCreateDescriptorPool");
        kit.defer([&kit, pool] { kit.api.vkDestroyDescriptorPool(kit.device, pool, nullptr); });
        VkDescriptorSetAllocateInfo set_alloc{};
        set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_alloc.descriptorPool = pool;
        set_alloc.descriptorSetCount = 1;
        set_alloc.pSetLayouts = &set_layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkAllocateDescriptorSets(kit.device, &set_alloc, &set), "vkAllocateDescriptorSets");
        const VkDescriptorImageInfo image_info{VK_NULL_HANDLE, image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        kit.api.vkUpdateDescriptorSets(kit.device, 1, &write, 0, nullptr);
        const VkPipelineLayout layout = rig.playout({set_layout});
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("ycbcr.frag");
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) { kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, layout, {set}));
    }, [ycbcr_format](Kit& kit) -> std::string {
        const auto f = kit.query_features<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
        if (f.samplerYcbcrConversion == VK_FALSE) {
            return "samplerYcbcrConversion";
        }
        return Cases::need_format(kit, ycbcr_format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                  VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT, "YCbCr 4:2:0 sampling");
    });
    }
}

const int registered = (register_views(), register_atomics_and_ycbcr(), 0);

}  // namespace

}  // namespace lsw::render::vk
