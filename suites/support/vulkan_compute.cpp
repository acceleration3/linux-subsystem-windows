#include "support/vulkan_compute.hpp"

#include <map>

namespace lsw::suites {

VkResult VulkanCompute::create(VulkanRig& rig, const std::vector<std::uint32_t>& spirv, const std::vector<VulkanBinding>& bindings,
                               std::uint32_t push_bytes, const VkSpecializationInfo* specialization, bool push_descriptors, VkPipelineCache cache) {
    rig_ = &rig;
    bindings_ = bindings;
    VkResult result = rig.make_shader(module, spirv);
    if (result != VK_SUCCESS) {
        return result;
    }
    std::vector<VkDescriptorSetLayoutBinding> layout_bindings;
    std::map<VkDescriptorType, std::uint32_t> counts;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        VkDescriptorSetLayoutBinding entry{};
        entry.binding = static_cast<std::uint32_t>(i);
        entry.descriptorType = bindings[i].type;
        entry.descriptorCount = bindings[i].count;
        entry.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        entry.pImmutableSamplers = bindings[i].immutable;
        layout_bindings.push_back(entry);
        counts[bindings[i].type] += bindings[i].count;
    }
    VkDescriptorSetLayoutCreateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_info.flags = push_descriptors ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0;
    set_info.bindingCount = static_cast<std::uint32_t>(layout_bindings.size());
    set_info.pBindings = layout_bindings.data();
    result = rig.make(set_layout, rig.fn.vkCreateDescriptorSetLayout, rig.fn.vkDestroyDescriptorSetLayout, set_info);
    if (result != VK_SUCCESS) {
        return result;
    }
    const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes};
    VkDescriptorSetLayout set_layout_handle = set_layout.get();
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &set_layout_handle;
    layout_info.pushConstantRangeCount = push_bytes != 0 ? 1 : 0;
    layout_info.pPushConstantRanges = push_bytes != 0 ? &range : nullptr;
    result = rig.make(layout, rig.fn.vkCreatePipelineLayout, rig.fn.vkDestroyPipelineLayout, layout_info);
    if (result != VK_SUCCESS) {
        return result;
    }
    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = module.get();
    pipeline_info.stage.pName = "main";
    pipeline_info.stage.pSpecializationInfo = specialization;
    pipeline_info.layout = layout.get();
    VkPipeline created = VK_NULL_HANDLE;
    result = rig.fn.vkCreateComputePipelines(rig.device, cache, 1, &pipeline_info, nullptr, &created);
    if (result != VK_SUCCESS) {
        return result;
    }
    pipeline = VulkanOwned<VkPipeline>(&rig, created, rig.fn.vkDestroyPipeline);
    if (push_descriptors) {
        return VK_SUCCESS;
    }
    std::vector<VkDescriptorPoolSize> sizes;
    for (const auto& [type, count] : counts) {
        sizes.push_back({type, count});
    }
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    pool_info.pPoolSizes = sizes.data();
    result = rig.make(pool, rig.fn.vkCreateDescriptorPool, rig.fn.vkDestroyDescriptorPool, pool_info);
    if (result != VK_SUCCESS) {
        return result;
    }
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool.get();
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &set_layout_handle;
    return rig.fn.vkAllocateDescriptorSets(rig.device, &allocate, &set);
}

void VulkanCompute::write_buffer(std::uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range,
                                 std::uint32_t element) {
    const VkDescriptorBufferInfo info{buffer, offset, range};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = element;
    write.descriptorCount = 1;
    write.descriptorType = bindings_[binding].type;
    write.pBufferInfo = &info;
    rig_->fn.vkUpdateDescriptorSets(rig_->device, 1, &write, 0, nullptr);
}

void VulkanCompute::write_texel(std::uint32_t binding, VkBufferView view) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = bindings_[binding].type;
    write.pTexelBufferView = &view;
    rig_->fn.vkUpdateDescriptorSets(rig_->device, 1, &write, 0, nullptr);
}

void VulkanCompute::write_image(std::uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout image_layout) {
    const VkDescriptorImageInfo info{sampler, view, image_layout};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = bindings_[binding].type;
    write.pImageInfo = &info;
    rig_->fn.vkUpdateDescriptorSets(rig_->device, 1, &write, 0, nullptr);
}

void VulkanCompute::bind(VkCommandBuffer commands) const {
    rig_->fn.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.get());
    if (set != VK_NULL_HANDLE) {
        rig_->fn.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout.get(), 0, 1, &set,
                                         static_cast<std::uint32_t>(dynamic_offsets.size()), dynamic_offsets.data());
    }
}

void VulkanCompute::push(VkCommandBuffer commands, const void* data, std::uint32_t size) const {
    rig_->fn.vkCmdPushConstants(commands, layout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, size, data);
}

void VulkanCompute::dispatch(VkCommandBuffer commands, std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
    rig_->fn.vkCmdDispatch(commands, x, y, z);
}

VkResult VulkanCompute::run(const void* push_data, std::uint32_t push_size, std::uint32_t x, std::uint32_t y, std::uint32_t z,
                            VulkanQueue* queue) {
    return rig_->run([&](VkCommandBuffer commands) {
        bind(commands);
        if (push_size != 0) {
            push(commands, push_data, push_size);
        }
        dispatch(commands, x, y, z);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        rig_->fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier,
                                      0, nullptr, 0, nullptr);
    }, queue);
}

}  // namespace lsw::suites
