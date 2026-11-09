#include "render/common/pattern.hpp"
#include "render/vk/quadkit.hpp"

#include <cstring>

namespace lsw::render::vk {

namespace {

struct SampleConstants {
    float scale[4];
    float bias[4];
    std::int32_t info[4];
};

std::string sparse_caps(Kit& kit) {
    if (!kit.features.sparseBinding || !kit.features.sparseResidencyImage2D) {
        return "sparseResidencyImage2D";
    }
    if (kit.properties.sparseProperties.residencyNonResidentStrict == VK_FALSE) {
        return "residencyNonResidentStrict";
    }
    std::uint32_t count = 0;
    kit.api.vkGetPhysicalDeviceQueueFamilyProperties(kit.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    kit.api.vkGetPhysicalDeviceQueueFamilyProperties(kit.physical, &count, families.data());
    return (families[kit.family].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0 ? std::string() : std::string("sparse binding queue");
}

void register_sparse() {
    CaseSpec spec("tex", "sparse_residency_unbound_tiles_read_zero");
    spec.feature("sparseBinding").feature("sparseResidencyImage2D").tolerance(Tolerance::tight(1));
    Cases::add(std::move(spec), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        target.clear_color[0] = 0.3f;
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = {256, 256, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage image = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateImage(kit.device, &info, nullptr, &image), "vkCreateImage sparse");
        kit.defer([&kit, image] { kit.api.vkDestroyImage(kit.device, image, nullptr); });
        std::uint32_t count = 0;
        kit.api.vkGetImageSparseMemoryRequirements(kit.device, image, &count, nullptr);
        std::vector<VkSparseImageMemoryRequirements> reqs(count);
        kit.api.vkGetImageSparseMemoryRequirements(kit.device, image, &count, reqs.data());
        rig.must(count != 0, "no sparse memory requirements");
        const VkExtent3D granularity = reqs[0].formatProperties.imageGranularity;
        VkMemoryRequirements mem_req{};
        kit.api.vkGetImageMemoryRequirements(kit.device, image, &mem_req);
        const std::uint32_t type = kit.memory_type(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        rig.must(type != ~0u, "no memory type for a sparse image");
        const VkDeviceSize tile_bytes = mem_req.alignment;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = tile_bytes * 2;
        alloc.memoryTypeIndex = type;
        rig.vk_ok(kit.api.vkAllocateMemory(kit.device, &alloc, nullptr, &memory), "vkAllocateMemory");
        kit.defer([&kit, memory] { kit.api.vkFreeMemory(kit.device, memory, nullptr); });
        VkSparseImageMemoryBind binds[2]{};
        binds[0].subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
        binds[0].offset = {0, 0, 0};
        binds[0].extent = granularity;
        binds[0].memory = memory;
        binds[0].memoryOffset = 0;
        binds[1] = binds[0];
        binds[1].offset = {static_cast<std::int32_t>(granularity.width), static_cast<std::int32_t>(granularity.height), 0};
        binds[1].memoryOffset = tile_bytes;
        VkSparseImageMemoryBindInfo image_bind{image, 2, binds};
        VkBindSparseInfo bind{};
        bind.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
        bind.imageBindCount = 1;
        bind.pImageBinds = &image_bind;
        rig.vk_ok(kit.api.vkQueueBindSparse(kit.queue, 1, &bind, VK_NULL_HANDLE), "vkQueueBindSparse");
        rig.vk_ok(kit.api.vkQueueWaitIdle(kit.queue), "vkQueueWaitIdle");
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateImageView(kit.device, &view_info, nullptr, &view), "vkCreateImageView");
        kit.defer([&kit, view] { kit.api.vkDestroyImageView(kit.device, view, nullptr); });
        const std::uint32_t tile_w = granularity.width;
        const std::uint32_t tile_h = granularity.height;
        const auto tile_a = Pattern::checker(tile_w, tile_h, 16, Pattern::pack(240, 80, 60), Pattern::pack(250, 220, 90));
        const auto tile_b = Pattern::checker(tile_w, tile_h, 8, Pattern::pack(60, 90, 240), Pattern::pack(80, 240, 200));
        std::vector<std::uint8_t> bytes = tile_a;
        bytes.insert(bytes.end(), tile_b.begin(), tile_b.end());
        Buffer staging;
        std::string error;
        rig.must(kit.make_buffer_with(staging, bytes.data(), bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, error), error);
        rig.run([&](VkCommandBuffer cb) {
            kit.barrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy regions[2]{};
            regions[0].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            regions[0].imageExtent = {tile_w, tile_h, 1};
            regions[1] = regions[0];
            regions[1].bufferOffset = tile_a.size();
            regions[1].imageOffset = {static_cast<std::int32_t>(tile_w), static_cast<std::int32_t>(tile_h), 0};
            kit.api.vkCmdCopyBufferToImage(cb, staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2, regions);
            kit.barrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorSet set = rig.dset(set_layout, {DescriptorWrite{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {}, {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}});
        const VkPipelineLayout layout = rig.playout({set_layout}, sizeof(SampleConstants), VK_SHADER_STAGE_FRAGMENT_BIT);
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("tex_f.frag");
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        const SampleConstants constants{{1, 1, 1, 1}, {0, 0, 0, 0}, {256, 256, 0, 0}};
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, &constants, sizeof constants);
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        }, layout, {set}));
    }, sparse_caps);
}

const int registered = (register_sparse(), 0);

}  // namespace

}  // namespace lsw::render::vk
