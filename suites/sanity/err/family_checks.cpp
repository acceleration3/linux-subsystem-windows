#include "sanity/err/family_checks.hpp"

#include "core/child_mode.hpp"
#include "support/fresh_process.hpp"
#include "support/gate_window.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/vulkan_buffer.hpp"
#include "support/vulkan_compute.hpp"
#include "support/vulkan_rig.hpp"
#include "support/vulkan_shaders.hpp"

namespace lsw::suites::sanity::err {

namespace {

constexpr VkDeviceSize kSize = 64 * 1024;
constexpr std::uint64_t kWaitNs = 5000000000ull;

enum class Kind { Transfer, Graphics, Compute };

const char* kind_name(Kind kind) {
    switch (kind) {
    case Kind::Transfer:
        return "transfer";
    case Kind::Graphics:
        return "graphics";
    case Kind::Compute:
        return "compute";
    }
    return "unknown";
}

bool documented(VkResult result) {
    return result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

Outcome child_role(Context& context, const std::string& name) {
    if (name != "wrong-family") {
        return Outcome::fail("unknown role " + name);
    }
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Outcome::fail("no target adapter: " + error);
    }
    VulkanRig* rig = new VulkanRig();
    VulkanRigOptions options;
    options.queues_per_family = 1;
    if (!rig->open(adapters.front(), options, error)) {
        return Outcome::fail("opening the device failed: " + error);
    }
    VulkanBuffer* first = new VulkanBuffer();
    VulkanBuffer* second = new VulkanBuffer();
    VulkanCompute* compute = new VulkanCompute();
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (first->create(*rig, kSize, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != VK_SUCCESS ||
        second->create(*rig, kSize, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != VK_SUCCESS ||
        compute->create(*rig, VulkanShaders::words(VulkanShader::Spin), {VulkanBinding{}}, 8) != VK_SUCCESS) {
        return Outcome::fail("creating the buffers and the compute pipeline failed");
    }
    compute->write_buffer(0, first->buffer);
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (rig->fn.vkCreateFence(rig->device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
        return Outcome::fail("creating the fence failed");
    }
    Problems problems;
    int tested = 0;
    bool lost = false;
    for (VulkanQueue& queue : rig->queues) {
        const VkQueueFlags flags = queue.properties.queueFlags;
        const bool has_graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool has_compute = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
        const bool has_transfer = has_graphics || has_compute || (flags & VK_QUEUE_TRANSFER_BIT) != 0;
        for (Kind kind : {Kind::Transfer, Kind::Graphics, Kind::Compute}) {
            const bool lacking = kind == Kind::Transfer ? !has_transfer : kind == Kind::Graphics ? !has_graphics : !has_compute;
            if (!lacking || lost) {
                continue;
            }
            ++tested;
            const std::string label = "family." + std::to_string(queue.family) + "." + std::string(kind_name(kind));
            VkCommandBufferAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate.commandPool = queue.pool;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            VkCommandBuffer commands = nullptr;
            VkResult recorded = rig->fn.vkAllocateCommandBuffers(rig->device, &allocate, &commands);
            if (recorded == VK_SUCCESS) {
                VkCommandBufferBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                recorded = rig->fn.vkBeginCommandBuffer(commands, &begin);
            }
            if (recorded == VK_SUCCESS) {
                if (kind == Kind::Transfer) {
                    const VkBufferCopy region{0, 0, kSize};
                    rig->fn.vkCmdCopyBuffer(commands, first->buffer, second->buffer, 1, &region);
                    rig->fn.vkCmdFillBuffer(commands, first->buffer, 0, kSize, 0x5A5A5A5Au);
                } else if (kind == Kind::Graphics) {
                    const VkViewport viewport{0, 0, 64, 64, 0, 1};
                    const VkRect2D scissor{{0, 0}, {64, 64}};
                    rig->fn.vkCmdSetViewport(commands, 0, 1, &viewport);
                    rig->fn.vkCmdSetScissor(commands, 0, 1, &scissor);
                    rig->fn.vkCmdDraw(commands, 3, 1, 0, 0);
                } else {
                    const std::uint32_t values[2] = {1024, 1};
                    compute->bind(commands);
                    compute->push(commands, values, sizeof(values));
                    compute->dispatch(commands, 1);
                }
                recorded = rig->fn.vkEndCommandBuffer(commands);
            }
            VkResult submitted = VK_NOT_READY;
            VkResult waited = VK_NOT_READY;
            if (recorded == VK_SUCCESS) {
                VkSubmitInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                info.commandBufferCount = 1;
                info.pCommandBuffers = &commands;
                rig->fn.vkResetFences(rig->device, 1, &fence);
                submitted = rig->submit(queue, 1, &info, fence);
                if (submitted == VK_SUCCESS) {
                    waited = rig->fn.vkWaitForFences(rig->device, 1, &fence, VK_TRUE, kWaitNs);
                }
            }
            context.measure(label, "record=" + VulkanTools::result_name(recorded) + " submit=" + VulkanTools::result_name(submitted) + " wait=" + VulkanTools::result_name(waited));
            if (!documented(recorded) || (submitted != VK_NOT_READY && !documented(submitted)) || (waited != VK_NOT_READY && !documented(waited))) {
                problems.add(label + ": the guest saw an undocumented result (" + VulkanTools::result_name(recorded) + ", " + VulkanTools::result_name(submitted) + ", " +
                             VulkanTools::result_name(waited) + ")");
            }
            if (submitted == VK_ERROR_DEVICE_LOST || waited == VK_ERROR_DEVICE_LOST) {
                lost = true;
            }
        }
    }
    context.measure("families.tested", tested);
    context.measure("device.lost", lost);
    if (tested == 0) {
        return Context::skip("no queue family lacks transfer, graphics or compute support");
    }
    return problems.outcome();
}

}  // namespace

Outcome FamilyChecks::vk_wrong_queue_family(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, child_role);
    }
    Problems problems;
    GateWindow window(context.id());
    window.open(problems);
    const CheckResult result = RobustChild::run(context.id(), "wrong-family", {}, 120, -1);
    RobustChild::forward(context, "", result);
    context.measure("child.status", Outcome::name(result.status));
    window.close(context, problems, "wrong_queue_family", 5000000);
    FreshProcess::check(context, problems, context.id(), "wrong_queue_family");
    if (result.status == Status::Skip && problems.empty()) {
        return Context::skip(result.reason);
    }
    if (result.status == Status::Fail || result.status == Status::Timeout) {
        problems.add("child: " + std::string(Outcome::name(result.status)) + " " + result.reason);
    } else if (result.status == Status::Crash) {
        context.note("the child process crashed on invalid usage: " + result.reason);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::err
