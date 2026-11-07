#include "sanity/sync/sync_device.hpp"

#include "support/vulkan_buffer.hpp"
#include "support/vulkan_compute.hpp"
#include "support/vulkan_rig.hpp"
#include "support/vulkan_shaders.hpp"

#include <windows.h>

#include <cstring>
#include <vector>

namespace lsw::suites::sanity::sync {

namespace {

class SyncVulkan : public SyncDevice {
public:
    ~SyncVulkan() override {
        if (rig_.device != nullptr) {
            static_cast<void>(rig_.wait_idle());
            release_pending();
        }
    }

    bool open(std::string& error) override {
        const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
        if (adapters.empty()) {
            return false;
        }
        VulkanRigOptions options;
        options.queues_per_family = 1;
        if (!rig_.open(adapters.front(), options, error)) {
            return false;
        }
        VkResult result = buffer_.create(rig_, kSlots * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (result != VK_SUCCESS) {
            error = "creating the buffer failed: " + VulkanTools::result_name(result);
            return false;
        }
        buffer_.fill_value(0);
        buffer_.publish();
        result = compute_.create(rig_, VulkanShaders::words(VulkanShader::Step), {VulkanBinding{}}, 16);
        if (result == VK_SUCCESS) {
            compute_.write_buffer(0, buffer_.buffer);
            result = rig_.make_fence(fence_);
        }
        if (result != VK_SUCCESS) {
            error = "creating the compute objects failed: " + VulkanTools::result_name(result);
            return false;
        }
        return true;
    }

    bool set(std::uint32_t slot, std::uint32_t value, std::string& error) override { return step(0, slot, value, error); }

    bool add(std::uint32_t slot, std::uint32_t value, std::string& error) override { return step(1, slot, value, error); }

    bool signal(std::string& error) override {
        VkFence fence = fence_.get();
        VkResult result = rig_.fn.vkResetFences(rig_.device, 1, &fence);
        if (result == VK_SUCCESS) {
            result = record_submit([](VkCommandBuffer) {}, fence);
        }
        if (result != VK_SUCCESS) {
            error = "signal submit failed: " + VulkanTools::result_name(result);
            return false;
        }
        return true;
    }

    SyncState wait(int timeout_ms, std::string& detail) override {
        const VkResult result = rig_.wait_fence(fence_.get(), static_cast<std::uint64_t>(timeout_ms) * 1000000ull);
        if (result == VK_SUCCESS) {
            release_pending();
            return SyncState::Done;
        }
        if (result == VK_TIMEOUT) {
            return SyncState::Timeout;
        }
        detail = "vkWaitForFences returned " + VulkanTools::result_name(result);
        return result == VK_ERROR_DEVICE_LOST ? SyncState::Lost : SyncState::Error;
    }

    bool finish(std::string& error) override {
        const VkResult result = rig_.wait_idle();
        if (result != VK_SUCCESS) {
            error = "vkDeviceWaitIdle returned " + VulkanTools::result_name(result);
            return false;
        }
        return true;
    }

    bool read(std::uint32_t slot, std::uint32_t& out, std::string&) override {
        buffer_.acquire();
        std::memcpy(&out, buffer_.bytes() + slot * 4, 4);
        return true;
    }

private:
    void barrier(VkCommandBuffer commands, VkPipelineStageFlags source, VkAccessFlags source_access, VkPipelineStageFlags destination, VkAccessFlags destination_access) {
        VkMemoryBarrier memory{};
        memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memory.srcAccessMask = source_access;
        memory.dstAccessMask = destination_access;
        rig_.fn.vkCmdPipelineBarrier(commands, source, destination, 0, 1, &memory, 0, nullptr, 0, nullptr);
    }

    template <typename Body>
    VkResult record_submit(const Body& body, VkFence fence) {
        VulkanQueue& queue = rig_.main();
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = queue.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer commands = nullptr;
        VkResult result = rig_.fn.vkAllocateCommandBuffers(rig_.device, &allocate, &commands);
        if (result != VK_SUCCESS) {
            return result;
        }
        pending_.push_back(commands);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = rig_.fn.vkBeginCommandBuffer(commands, &begin);
        if (result != VK_SUCCESS) {
            return result;
        }
        body(commands);
        barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        result = rig_.fn.vkEndCommandBuffer(commands);
        if (result != VK_SUCCESS) {
            return result;
        }
        VkSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &commands;
        return rig_.submit(queue, 1, &info, fence);
    }

    bool step(std::uint32_t op, std::uint32_t slot, std::uint32_t value, std::string& error) {
        const std::uint32_t values[4] = {op, value, slot, 0};
        const VkResult result = record_submit(
            [&](VkCommandBuffer commands) {
                barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                compute_.bind(commands);
                compute_.push(commands, values, sizeof(values));
                compute_.dispatch(commands, 1);
            },
            VK_NULL_HANDLE);
        if (result != VK_SUCCESS) {
            error = "recording or submitting the step failed: " + VulkanTools::result_name(result);
            return false;
        }
        return true;
    }

    void release_pending() {
        if (!pending_.empty()) {
            rig_.fn.vkFreeCommandBuffers(rig_.device, rig_.main().pool, static_cast<std::uint32_t>(pending_.size()), pending_.data());
            pending_.clear();
        }
    }

    VulkanRig rig_;
    VulkanBuffer buffer_;
    VulkanCompute compute_;
    VulkanOwned<VkFence> fence_;
    std::vector<VkCommandBuffer> pending_;
};

}  // namespace

std::unique_ptr<SyncDevice> make_sync_vulkan() {
    return std::make_unique<SyncVulkan>();
}

}  // namespace lsw::suites::sanity::sync
