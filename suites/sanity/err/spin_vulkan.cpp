#include "sanity/err/spin_device.hpp"

#include "support/vulkan_buffer.hpp"
#include "support/vulkan_compute.hpp"
#include "support/vulkan_rig.hpp"
#include "support/vulkan_shaders.hpp"

#include <cstring>

namespace lsw::suites::sanity::err {

namespace {

class SpinVulkan : public SpinDevice {
public:
    bool open(bool forever, std::string& error) override {
        forever_ = forever;
        const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
        if (adapters.empty()) {
            return false;
        }
        VulkanRigOptions options;
        options.queues_per_family = 1;
        if (!rig_.open(adapters.front(), options, error)) {
            return false;
        }
        VkResult result = buffer_.create(rig_, kThreads * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (result != VK_SUCCESS) {
            error = "creating the buffer failed: " + VulkanTools::result_name(result);
            return false;
        }
        buffer_.fill_value(0);
        buffer_.publish();
        result = compute_.create(rig_, VulkanShaders::words(forever ? VulkanShader::SpinForever : VulkanShader::Spin), {VulkanBinding{}}, forever ? 0 : 8);
        if (result != VK_SUCCESS) {
            error = "creating the compute pipeline failed: " + VulkanTools::result_name(result);
            return false;
        }
        compute_.write_buffer(0, buffer_.buffer);
        result = rig_.make_fence(fence_);
        if (result != VK_SUCCESS) {
            error = "creating the fence failed: " + VulkanTools::result_name(result);
            return false;
        }
        return true;
    }

    bool submit(std::uint32_t outer, std::string& error) override {
        const std::uint32_t values[2] = {kInner, outer};
        VulkanQueue& queue = rig_.main();
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = queue.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer commands = nullptr;
        VkResult result = rig_.fn.vkAllocateCommandBuffers(rig_.device, &allocate, &commands);
        if (result == VK_SUCCESS) {
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            result = rig_.fn.vkBeginCommandBuffer(commands, &begin);
        }
        if (result != VK_SUCCESS) {
            error = "recording failed: " + VulkanTools::result_name(result);
            return false;
        }
        compute_.bind(commands);
        if (!forever_) {
            compute_.push(commands, values, sizeof(values));
        }
        compute_.dispatch(commands, 1);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        rig_.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        result = rig_.fn.vkEndCommandBuffer(commands);
        VkFence fence = fence_.get();
        if (result == VK_SUCCESS) {
            result = rig_.fn.vkResetFences(rig_.device, 1, &fence);
        }
        if (result == VK_SUCCESS) {
            VkSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &commands;
            result = rig_.submit(queue, 1, &info, fence);
        }
        if (result != VK_SUCCESS) {
            error = "vkQueueSubmit failed: " + VulkanTools::result_name(result);
            return false;
        }
        return true;
    }

    SpinState poll(int timeout_ms, std::string& detail) override {
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
        for (;;) {
            const VkResult result = rig_.wait_fence(fence_.get(), 20000000ull);
            if (result == VK_SUCCESS) {
                return SpinState::Done;
            }
            if (result == VK_ERROR_DEVICE_LOST) {
                detail = "vkWaitForFences returned VK_ERROR_DEVICE_LOST";
                return SpinState::Lost;
            }
            if (result != VK_TIMEOUT) {
                detail = "vkWaitForFences returned " + VulkanTools::result_name(result);
                return SpinState::Error;
            }
            if (static_cast<LONGLONG>(GetTickCount64() - end) >= 0) {
                return SpinState::Pending;
            }
        }
    }

    SpinState wait_native(int timeout_ms, std::string& detail) override {
        const VkResult result = rig_.wait_fence(fence_.get(), static_cast<std::uint64_t>(timeout_ms) * 1000000ull);
        if (result == VK_SUCCESS) {
            return SpinState::Done;
        }
        if (result == VK_TIMEOUT) {
            return SpinState::Pending;
        }
        detail = "vkWaitForFences returned " + VulkanTools::result_name(result);
        return result == VK_ERROR_DEVICE_LOST ? SpinState::Lost : SpinState::Error;
    }

    bool read(std::vector<std::uint32_t>& out, std::string&) override {
        buffer_.acquire();
        out.resize(kThreads);
        std::memcpy(out.data(), buffer_.bytes(), kThreads * 4);
        return true;
    }

private:
    bool forever_ = false;
    VulkanRig rig_;
    VulkanBuffer buffer_;
    VulkanCompute compute_;
    VulkanOwned<VkFence> fence_;
};

}  // namespace

std::unique_ptr<SpinDevice> make_spin_vulkan() {
    return std::make_unique<SpinVulkan>();
}

}  // namespace lsw::suites::sanity::err
