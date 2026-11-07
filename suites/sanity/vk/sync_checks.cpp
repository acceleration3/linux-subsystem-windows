#include "sanity/vk/sync_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/vk/kit.hpp"
#include "support/gate_window.hpp"
#include "support/vulkan_image_io.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace lsw::suites::sanity::vk {

namespace {

constexpr std::uint64_t kSecond = 1000000000ull;
constexpr VkDeviceSize kSize = 64 * 1024;

void wait_for_host_event(VulkanRig& rig, VkCommandBuffer commands, VkEvent event) {
    rig.fn.vkCmdWaitEvents(commands, 1, &event, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr, 0, nullptr);
}

VkResult submit_one(VulkanRig& rig, VulkanQueue& queue, VkCommandBuffer commands, VkFence fence) {
    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.commandBufferCount = commands != nullptr ? 1 : 0;
    info.pCommandBuffers = commands != nullptr ? &commands : nullptr;
    return rig.submit(queue, 1, &info, fence);
}

void copy_to_host(VulkanRig& rig, VkCommandBuffer commands, VkBuffer source, VkBuffer destination, VkDeviceSize size) {
    const VkBufferCopy region{0, 0, size};
    rig.fn.vkCmdCopyBuffer(commands, source, destination, 1, &region);
    Kit::transfer_barrier(rig, commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
}

bool untouched(const VulkanBuffer& buffer, VkDeviceSize size, std::uint8_t value) {
    const std::uint8_t* bytes = buffer.bytes();
    return std::all_of(bytes, bytes + size, [value](std::uint8_t byte) { return byte == value; });
}

}  // namespace

Outcome SyncChecks::fences(Context& context) {
    if (ChildMode::active()) {
        return GateWindow::peer(context);
    }
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        VulkanOwned<VkFence> fence;
        if (!Kit::succeeds(problems, "fence", rig.make_fence(fence))) {
            return {};
        }
        Kit::expect(problems, "status of an unsignaled fence", rig.fn.vkGetFenceStatus(rig.device, fence.get()), {VK_NOT_READY});
        Kit::expect(problems, "wait with timeout 0 on an unsignaled fence", rig.wait_fence(fence.get(), 0), {VK_TIMEOUT});
        Kit::expect(problems, "wait with a 2 ms timeout on an unsignaled fence", rig.wait_fence(fence.get(), 2000000), {VK_TIMEOUT});
        VulkanOwned<VkFence> signaled;
        if (Kit::succeeds(problems, "fence created signaled", rig.make_fence(signaled, true))) {
            Kit::succeeds(problems, "status of a fence created signaled", rig.fn.vkGetFenceStatus(rig.device, signaled.get()));
            Kit::succeeds(problems, "wait with timeout 0 on a signaled fence", rig.wait_fence(signaled.get(), 0));
            VkFence handle = signaled.get();
            Kit::succeeds(problems, "vkResetFences", rig.fn.vkResetFences(rig.device, 1, &handle));
            Kit::expect(problems, "status after a reset", rig.fn.vkGetFenceStatus(rig.device, signaled.get()), {VK_NOT_READY});
        }
        VulkanBuffer source;
        VulkanBuffer sink;
        VulkanOwned<VkEvent> gate;
        VulkanOwned<VkFence> empty;
        if (!Kit::succeeds(problems, "source", Kit::host_buffer(rig, source, kSize)) || !Kit::succeeds(problems, "sink", Kit::host_buffer(rig, sink, kSize)) ||
            !Kit::succeeds(problems, "gate event", rig.make_event(gate)) || !Kit::succeeds(problems, "second fence", rig.make_fence(empty))) {
            return {};
        }
        source.fill_pattern(0xFE00);
        source.publish();
        sink.fill_value(0xCD);
        sink.publish();
        VulkanQueue& queue = rig.main();
        VkResult code = VK_SUCCESS;
        const VkCommandBuffer commands = Kit::record(rig, queue, [&](VkCommandBuffer cb) {
            wait_for_host_event(rig, cb, gate.get());
            copy_to_host(rig, cb, source.buffer, sink.buffer, kSize);
        }, 0, &code);
        if (!Kit::succeeds(problems, "recording of the gated copy", code)) {
            return {};
        }
        GateWindow window(context.id());
        window.open(problems);
        Kit::succeeds(problems, "gated submission", submit_one(rig, queue, commands, fence.get()));
        window.begin();
        Kit::succeeds(problems, "empty submission behind it", submit_one(rig, queue, nullptr, empty.get()));
        Kit::expect(problems, "status of a fence whose work waits for an unset event", rig.fn.vkGetFenceStatus(rig.device, fence.get()), {VK_NOT_READY});
        Kit::expect(problems, "wait on a fence whose work waits for an unset event", rig.wait_fence(fence.get(), 1000000), {VK_TIMEOUT});
        Kit::expect(problems, "status of the empty-submission fence behind unfinished work", rig.fn.vkGetFenceStatus(rig.device, empty.get()), {VK_NOT_READY});
        window.sleep_until(2600);
        Kit::expect(problems, "bounded wait on the gated fence past the kernel ring timeout", rig.wait_fence(fence.get(), 100000000), {VK_TIMEOUT});
        Kit::expect(problems, "status of the gated fence past the kernel ring timeout", rig.fn.vkGetFenceStatus(rig.device, fence.get()), {VK_NOT_READY});
        window.sleep_until_release();
        sink.acquire();
        if (!untouched(sink, kSize, 0xCD)) {
            problems.add("the gated copy wrote its destination before the event was set");
        }
        Kit::succeeds(problems, "vkSetEvent", rig.fn.vkSetEvent(rig.device, gate.get()));
        window.close(context, problems, "fences.gate");
        if (Kit::succeeds(problems, "wait for the gated work", rig.wait_fence(fence.get(), 20 * kSecond)) &&
            Kit::succeeds(problems, "wait for the empty submission", rig.wait_fence(empty.get(), 20 * kSecond))) {
            sink.acquire();
            Kit::same_bytes(context, problems, "data read after the fence wait", source.bytes(), sink.bytes(), kSize);
            Kit::succeeds(problems, "status after signaling", rig.fn.vkGetFenceStatus(rig.device, fence.get()));
        }
        VkFence handles[2] = {fence.get(), empty.get()};
        Kit::succeeds(problems, "reset of both fences", rig.fn.vkResetFences(rig.device, 2, handles));
        VulkanOwned<VkEvent> second_gate;
        if (Kit::succeeds(problems, "second gate", rig.make_event(second_gate))) {
            VulkanOwned<VkEvent> third_gate;
            Kit::succeeds(problems, "third gate", rig.make_event(third_gate));
            const VkCommandBuffer first = Kit::record(rig, queue, [&](VkCommandBuffer cb) { wait_for_host_event(rig, cb, second_gate.get()); }, 0, &code);
            const VkCommandBuffer second = Kit::record(rig, queue, [&](VkCommandBuffer cb) { wait_for_host_event(rig, cb, third_gate.get()); }, 0, &code);
            Kit::succeeds(problems, "first reused submission", submit_one(rig, queue, first, fence.get()));
            Kit::succeeds(problems, "second reused submission", submit_one(rig, queue, second, empty.get()));
            Kit::expect(problems, "wait for any of two unfinished fences", rig.fn.vkWaitForFences(rig.device, 2, handles, VK_FALSE, 1000000), {VK_TIMEOUT});
            Kit::succeeds(problems, "release of the first gate", rig.fn.vkSetEvent(rig.device, second_gate.get()));
            Kit::succeeds(problems, "wait for any of two fences", rig.fn.vkWaitForFences(rig.device, 2, handles, VK_FALSE, 20 * kSecond));
            Kit::expect(problems, "wait for all of two fences while one is pending", rig.fn.vkWaitForFences(rig.device, 2, handles, VK_TRUE, 1000000), {VK_TIMEOUT});
            Kit::succeeds(problems, "release of the second gate", rig.fn.vkSetEvent(rig.device, third_gate.get()));
            Kit::succeeds(problems, "wait for all of two fences", rig.fn.vkWaitForFences(rig.device, 2, handles, VK_TRUE, 20 * kSecond));
        }
        rig.wait_idle();
        return {};
    });
}

namespace {

struct Chain {
    VulkanQueue* first = nullptr;
    VulkanQueue* second = nullptr;
    bool ownership = false;
    std::string label;
};

void run_chain(Context& context, VulkanRig& rig, const Chain& chain, std::uint32_t iterations, Problems& problems) {
    VulkanBuffer x;
    VulkanBuffer y;
    VulkanBuffer host;
    if (!Kit::succeeds(problems, chain.label + " buffer X", Kit::device_buffer(rig, x, kSize)) || !Kit::succeeds(problems, chain.label + " buffer Y", Kit::device_buffer(rig, y, kSize)) ||
        !Kit::succeeds(problems, chain.label + " host buffer", Kit::host_buffer(rig, host, kSize))) {
        return;
    }
    VulkanOwned<VkSemaphore> first_to_second;
    VulkanOwned<VkSemaphore> second_to_first;
    VulkanOwned<VkFence> fence;
    if (!Kit::succeeds(problems, chain.label + " semaphore", rig.make_semaphore(first_to_second)) || !Kit::succeeds(problems, chain.label + " semaphore", rig.make_semaphore(second_to_first)) ||
        !Kit::succeeds(problems, chain.label + " fence", rig.make_fence(fence))) {
        return;
    }
    const std::uint32_t fa = chain.first->family;
    const std::uint32_t fb = chain.second->family;
    const auto buffer_barrier = [&](VkBuffer buffer, std::uint32_t from, std::uint32_t to, VkAccessFlags source, VkAccessFlags destination) {
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = source;
        barrier.dstAccessMask = destination;
        barrier.srcQueueFamilyIndex = from;
        barrier.dstQueueFamilyIndex = to;
        barrier.buffer = buffer;
        barrier.size = VK_WHOLE_SIZE;
        return barrier;
    };
    for (std::uint32_t i = 0; i < iterations; ++i) {
        const std::uint32_t value = 0x1000 + i;
        const bool transfer_ownership = chain.ownership && i == 0;
        VkResult code = VK_SUCCESS;
        const VkCommandBuffer a = Kit::record(rig, *chain.first, [&](VkCommandBuffer cb) {
            rig.fn.vkCmdFillBuffer(cb, x.buffer, 0, kSize, value);
            if (transfer_ownership) {
                const VkBufferMemoryBarrier release = buffer_barrier(x.buffer, fa, fb, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
                rig.fn.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &release, 0, nullptr);
            }
        }, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, &code);
        const VkCommandBuffer b = Kit::record(rig, *chain.second, [&](VkCommandBuffer cb) {
            if (transfer_ownership) {
                const VkBufferMemoryBarrier acquire = buffer_barrier(x.buffer, fa, fb, 0, VK_ACCESS_TRANSFER_READ_BIT);
                rig.fn.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 0, nullptr);
            }
            const VkBufferCopy region{0, 0, kSize};
            rig.fn.vkCmdCopyBuffer(cb, x.buffer, y.buffer, 1, &region);
            if (transfer_ownership) {
                const VkBufferMemoryBarrier release = buffer_barrier(y.buffer, fb, fa, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
                rig.fn.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &release, 0, nullptr);
            }
        }, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, &code);
        const VkCommandBuffer c = Kit::record(rig, *chain.first, [&](VkCommandBuffer cb) {
            if (transfer_ownership) {
                const VkBufferMemoryBarrier acquire = buffer_barrier(y.buffer, fb, fa, 0, VK_ACCESS_TRANSFER_READ_BIT);
                rig.fn.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 0, nullptr);
            }
            copy_to_host(rig, cb, y.buffer, host.buffer, kSize);
        }, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, &code);
        if (a == nullptr || b == nullptr || c == nullptr) {
            Kit::succeeds(problems, chain.label + " recording", code == VK_SUCCESS ? VK_ERROR_UNKNOWN : code);
            return;
        }
        if (chain.ownership && i > 0) {
            break;
        }
        host.fill_value(0xCD);
        host.publish();
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSemaphore s1 = first_to_second.get();
        VkSemaphore s2 = second_to_first.get();
        VkSubmitInfo info_c{};
        info_c.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info_c.waitSemaphoreCount = 1;
        info_c.pWaitSemaphores = &s2;
        info_c.pWaitDstStageMask = &wait_stage;
        info_c.commandBufferCount = 1;
        info_c.pCommandBuffers = &c;
        VkSubmitInfo info_b{};
        info_b.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info_b.waitSemaphoreCount = 1;
        info_b.pWaitSemaphores = &s1;
        info_b.pWaitDstStageMask = &wait_stage;
        info_b.commandBufferCount = 1;
        info_b.pCommandBuffers = &b;
        info_b.signalSemaphoreCount = 1;
        info_b.pSignalSemaphores = &s2;
        VkSubmitInfo info_a{};
        info_a.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info_a.commandBufferCount = 1;
        info_a.pCommandBuffers = &a;
        info_a.signalSemaphoreCount = 1;
        info_a.pSignalSemaphores = &s1;
        if (!Kit::succeeds(problems, chain.label + " submission of the producer", rig.submit(*chain.first, 1, &info_a, VK_NULL_HANDLE)) ||
            !Kit::succeeds(problems, chain.label + " submission of the middle stage", rig.submit(*chain.second, 1, &info_b, VK_NULL_HANDLE)) ||
            !Kit::succeeds(problems, chain.label + " submission of the consumer", rig.submit(*chain.first, 1, &info_c, fence.get()))) {
            rig.wait_idle();
            return;
        }
        if (!Kit::succeeds(problems, chain.label + " completion of iteration " + std::to_string(i), rig.wait_fence(fence.get(), 20 * kSecond))) {
            return;
        }
        VkFence handle = fence.get();
        (void)rig.fn.vkResetFences(rig.device, 1, &handle);
        host.acquire();
        std::vector<std::uint8_t> expected(kSize);
        for (std::size_t w = 0; w < kSize / 4; ++w) {
            std::memcpy(expected.data() + w * 4, &value, 4);
        }
        if (!Kit::same_bytes(context, problems, chain.label + " iteration " + std::to_string(i), expected.data(), host.bytes(), kSize)) {
            return;
        }
    }
}

}  // namespace

Outcome SyncChecks::binary_semaphores(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        Chain same;
        same.first = &rig.queues[rig.main_index];
        same.second = &rig.queues[rig.main_index];
        same.label = "single queue";
        run_chain(context, rig, same, 40, problems);
        Chain pair;
        pair.label = "two queues of one family";
        for (VulkanQueue& a : rig.queues) {
            for (VulkanQueue& b : rig.queues) {
                if (pair.first == nullptr && a.family == b.family && a.index == 0 && b.index == 1 && (a.properties.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) != 0) {
                    pair.first = &a;
                    pair.second = &b;
                }
            }
        }
        if (pair.first != nullptr) {
            run_chain(context, rig, pair, 40, problems);
        } else {
            context.note("no family exposes two queues; the same-family cross-queue chain is skipped");
        }
        Chain across;
        across.ownership = true;
        across.label = "queue family ownership transfer";
        for (VulkanQueue& a : rig.queues) {
            for (VulkanQueue& b : rig.queues) {
                if (across.first == nullptr && a.family != b.family && a.index == 0 && b.index == 0 &&
                    (a.properties.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) != 0 &&
                    (b.properties.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) != 0 && a.family == rig.main().family) {
                    across.first = &a;
                    across.second = &b;
                }
            }
        }
        if (across.first != nullptr) {
            run_chain(context, rig, across, 1, problems);
        } else {
            context.note("only one transfer-capable queue family; ownership transfers are skipped");
        }
        rig.wait_idle();
        return {};
    });
}

Outcome SyncChecks::timeline_semaphores(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        if (!rig.timeline) {
            return "timelineSemaphore is not reported by this device";
        }
        VulkanOwned<VkSemaphore> timeline;
        if (!Kit::succeeds(problems, "timeline semaphore", rig.make_semaphore(timeline, true, 3))) {
            return {};
        }
        std::uint64_t value = 0;
        Kit::succeeds(problems, "vkGetSemaphoreCounterValue", rig.fn.vkGetSemaphoreCounterValue(rig.device, timeline.get(), &value));
        if (value != 3) {
            problems.add("initial value " + std::to_string(value) + ", expected 3");
        }
        const auto wait = [&](std::uint64_t target, std::uint64_t timeout, bool any = false) {
            VkSemaphore handle = timeline.get();
            VkSemaphoreWaitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            info.flags = any ? VK_SEMAPHORE_WAIT_ANY_BIT : 0;
            info.semaphoreCount = 1;
            info.pSemaphores = &handle;
            info.pValues = &target;
            return rig.fn.vkWaitSemaphores(rig.device, &info, timeout);
        };
        const auto signal = [&](std::uint64_t target) {
            VkSemaphoreSignalInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
            info.semaphore = timeline.get();
            info.value = target;
            return rig.fn.vkSignalSemaphore(rig.device, &info);
        };
        Kit::succeeds(problems, "host wait for a value already reached", wait(3, 0));
        Kit::expect(problems, "host wait for a future value with timeout 0", wait(4, 0), {VK_TIMEOUT});
        Kit::expect(problems, "host wait for a future value with a 5 ms timeout", wait(10, 5000000), {VK_TIMEOUT});
        Kit::succeeds(problems, "host signal of 5", signal(5));
        rig.fn.vkGetSemaphoreCounterValue(rig.device, timeline.get(), &value);
        if (value != 5) {
            problems.add("value after the host signal of 5 is " + std::to_string(value));
        }
        Kit::succeeds(problems, "host wait for 5", wait(5, 0));
        VulkanBuffer source;
        VulkanBuffer middle;
        VulkanBuffer sink;
        if (!Kit::succeeds(problems, "source", Kit::host_buffer(rig, source, kSize)) || !Kit::succeeds(problems, "middle", Kit::device_buffer(rig, middle, kSize)) ||
            !Kit::succeeds(problems, "sink", Kit::host_buffer(rig, sink, kSize))) {
            return {};
        }
        source.fill_pattern(0x717E);
        source.publish();
        sink.fill_value(0xCD);
        sink.publish();
        VulkanQueue& queue = rig.main();
        VkResult code = VK_SUCCESS;
        const VkCommandBuffer upload = Kit::record(rig, queue, [&](VkCommandBuffer cb) {
            const VkBufferCopy region{0, 0, kSize};
            rig.fn.vkCmdCopyBuffer(cb, source.buffer, middle.buffer, 1, &region);
        }, 0, &code);
        const VkCommandBuffer download = Kit::record(rig, queue, [&](VkCommandBuffer cb) { copy_to_host(rig, cb, middle.buffer, sink.buffer, kSize); }, 0, &code);
        if (upload == nullptr || download == nullptr) {
            Kit::succeeds(problems, "recording", code);
            return {};
        }
        const auto submit = [&](VkCommandBuffer commands, std::uint64_t wait_value, std::uint64_t signal_value) {
            VkSemaphore handle = timeline.get();
            const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkTimelineSemaphoreSubmitInfo timeline_info{};
            timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
            timeline_info.waitSemaphoreValueCount = wait_value != 0 ? 1 : 0;
            timeline_info.pWaitSemaphoreValues = &wait_value;
            timeline_info.signalSemaphoreValueCount = 1;
            timeline_info.pSignalSemaphoreValues = &signal_value;
            VkSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.pNext = &timeline_info;
            info.waitSemaphoreCount = wait_value != 0 ? 1 : 0;
            info.pWaitSemaphores = &handle;
            info.pWaitDstStageMask = &stage;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &commands;
            info.signalSemaphoreCount = 1;
            info.pSignalSemaphores = &handle;
            return rig.submit(queue, 1, &info, VK_NULL_HANDLE);
        };
        Kit::succeeds(problems, "submission of the download waiting for 20", submit(download, 20, 21));
        Kit::succeeds(problems, "submission of the upload waiting for 10", submit(upload, 10, 11));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        rig.fn.vkGetSemaphoreCounterValue(rig.device, timeline.get(), &value);
        if (value != 5) {
            problems.add("the counter moved to " + std::to_string(value) + " although both submissions wait for values not yet signaled");
        }
        Kit::expect(problems, "host wait for the first device signal before it can happen", wait(11, 10000000), {VK_TIMEOUT});
        Kit::succeeds(problems, "host signal of 10", signal(10));
        const bool first_done = Kit::succeeds(problems, "host wait for the device signal of 11", wait(11, 20 * kSecond));
        if (first_done) {
            rig.fn.vkGetSemaphoreCounterValue(rig.device, timeline.get(), &value);
            if (value < 11 || value >= 20) {
                problems.add("the counter is " + std::to_string(value) + " after the first device signal, expected 11");
            }
            Kit::expect(problems, "host wait for 21 while the second submission waits for 20", wait(21, 5000000), {VK_TIMEOUT});
        }
        Kit::succeeds(problems, "host signal of 20", signal(20));
        if (first_done && Kit::succeeds(problems, "host wait for the device signal of 21", wait(21, 20 * kSecond))) {
            sink.acquire();
            Kit::same_bytes(context, problems, "data moved by two timeline-ordered submissions", source.bytes(), sink.bytes(), kSize);
        }
        VulkanOwned<VkSemaphore> other;
        if (Kit::succeeds(problems, "second timeline semaphore", rig.make_semaphore(other, true, 0))) {
            VkSemaphore both[2] = {timeline.get(), other.get()};
            std::uint64_t targets[2] = {21, 1};
            VkSemaphoreWaitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            info.semaphoreCount = 2;
            info.pSemaphores = both;
            info.pValues = targets;
            Kit::expect(problems, "wait for all of two semaphores where one is behind", rig.fn.vkWaitSemaphores(rig.device, &info, 5000000), {VK_TIMEOUT});
            info.flags = VK_SEMAPHORE_WAIT_ANY_BIT;
            Kit::succeeds(problems, "wait for any of two semaphores where one is reached", rig.fn.vkWaitSemaphores(rig.device, &info, 0));
        }
        const std::uint64_t difference = rig.timeline_props.maxTimelineSemaphoreValueDifference;
        context.measure("vk.maxTimelineSemaphoreValueDifference", difference);
        if (difference < 0x7FFFFFFFull) {
            problems.add("maxTimelineSemaphoreValueDifference is " + std::to_string(difference) + ", below the required 2^31-1");
        }
        rig.fn.vkGetSemaphoreCounterValue(rig.device, timeline.get(), &value);
        const std::uint64_t jump = std::min<std::uint64_t>(difference, 0x7FFFFFFFFFFFull - value);
        Kit::succeeds(problems, "host signal at the maximum value difference", signal(value + jump));
        rig.fn.vkGetSemaphoreCounterValue(rig.device, timeline.get(), &value);
        if (value != 21 + jump) {
            problems.add("the counter is " + std::to_string(value) + " after a jump of " + std::to_string(jump));
        }
        std::uint64_t previous = value;
        for (int i = 1; i <= 16; ++i) {
            Kit::succeeds(problems, "monotonic host signal", signal(previous + static_cast<std::uint64_t>(i)));
            std::uint64_t now = 0;
            rig.fn.vkGetSemaphoreCounterValue(rig.device, timeline.get(), &now);
            if (now < previous) {
                problems.add("the counter moved backwards from " + std::to_string(previous) + " to " + std::to_string(now));
                break;
            }
            previous = now;
        }
        rig.wait_idle();
        return {};
    });
}

Outcome SyncChecks::events_barriers(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        VulkanOwned<VkEvent> event;
        if (!Kit::succeeds(problems, "event", rig.make_event(event))) {
            return {};
        }
        Kit::expect(problems, "initial event status", rig.fn.vkGetEventStatus(rig.device, event.get()), {VK_EVENT_RESET});
        Kit::succeeds(problems, "vkSetEvent", rig.fn.vkSetEvent(rig.device, event.get()));
        Kit::expect(problems, "status after vkSetEvent", rig.fn.vkGetEventStatus(rig.device, event.get()), {VK_EVENT_SET});
        Kit::succeeds(problems, "vkResetEvent", rig.fn.vkResetEvent(rig.device, event.get()));
        Kit::expect(problems, "status after vkResetEvent", rig.fn.vkGetEventStatus(rig.device, event.get()), {VK_EVENT_RESET});
        const VkResult device_set = rig.run([&](VkCommandBuffer cb) { rig.fn.vkCmdSetEvent(cb, event.get(), VK_PIPELINE_STAGE_TRANSFER_BIT); });
        if (Kit::succeeds(problems, "device-side vkCmdSetEvent", device_set)) {
            Kit::expect(problems, "status after a device-side set", rig.fn.vkGetEventStatus(rig.device, event.get()), {VK_EVENT_SET});
            const VkResult device_reset = rig.run([&](VkCommandBuffer cb) { rig.fn.vkCmdResetEvent(cb, event.get(), VK_PIPELINE_STAGE_TRANSFER_BIT); });
            if (Kit::succeeds(problems, "device-side vkCmdResetEvent", device_reset)) {
                Kit::expect(problems, "status after a device-side reset", rig.fn.vkGetEventStatus(rig.device, event.get()), {VK_EVENT_RESET});
            }
        }
        const std::size_t words = 256;
        VulkanBuffer seed;
        VulkanBuffer params;
        VulkanBuffer b1;
        VulkanBuffer sink;
        VulkanBuffer unrelated;
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        if (!Kit::succeeds(problems, "seed", Kit::host_buffer(rig, seed, words * 4)) || !Kit::succeeds(problems, "params", Kit::host_buffer(rig, params, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) ||
            !Kit::succeeds(problems, "B1", Kit::device_buffer(rig, b1, words * 4, usage)) || !Kit::succeeds(problems, "sink", Kit::host_buffer(rig, sink, words * 4, usage)) ||
            !Kit::succeeds(problems, "unrelated", Kit::device_buffer(rig, unrelated, words * 4, usage))) {
            return {};
        }
        const std::vector<std::uint32_t> data = Kit::words(0xEB, words);
        std::memcpy(seed.mapped, data.data(), words * 4);
        seed.publish();
        params.words()[0] = 1;
        params.words()[1] = 0;
        params.words()[2] = static_cast<std::uint32_t>(words);
        params.words()[3] = 0;
        params.publish();
        VulkanCompute producer;
        VulkanCompute consumer;
        VulkanCompute indirect_consumer;
        const std::vector<VulkanBinding> layout = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
        if (!Kit::succeeds(problems, "producer pipeline", producer.create(rig, VulkanShaders::words(VulkanShader::Ops), layout, 8)) ||
            !Kit::succeeds(problems, "consumer pipeline", consumer.create(rig, VulkanShaders::words(VulkanShader::Ops), layout, 8)) ||
            !Kit::succeeds(problems, "indirect consumer pipeline", indirect_consumer.create(rig, VulkanShaders::words(VulkanShader::Ops), layout, 8))) {
            return {};
        }
        producer.write_buffer(0, seed.buffer);
        producer.write_buffer(1, b1.buffer);
        producer.write_buffer(2, params.buffer);
        consumer.write_buffer(0, b1.buffer);
        consumer.write_buffer(1, sink.buffer);
        consumer.write_buffer(2, params.buffer);
        indirect_consumer.write_buffer(0, seed.buffer);
        indirect_consumer.write_buffer(1, sink.buffer);
        indirect_consumer.write_buffer(2, params.buffer);
        const std::uint32_t push[2] = {0, 0};
        enum class Producer { Fill, Compute };
        enum class Consumer { Copy, Compute, Indirect };
        enum class Mechanism { Barrier, Event };
        const auto produce = [&](VkCommandBuffer cb, Producer kind) {
            if (kind == Producer::Fill) {
                rig.fn.vkCmdFillBuffer(cb, b1.buffer, 0, words * 4, 1);
            } else {
                producer.bind(cb);
                producer.push(cb, push, sizeof(push));
                producer.dispatch(cb, static_cast<std::uint32_t>(words / 64));
            }
        };
        const std::pair<Producer, const char*> producers[] = {{Producer::Fill, "transfer write"}, {Producer::Compute, "compute write"}};
        const std::pair<Consumer, const char*> consumers[] = {{Consumer::Copy, "transfer read"}, {Consumer::Compute, "compute read"}, {Consumer::Indirect, "indirect argument read"}};
        for (const auto& [producer_kind, producer_name] : producers) {
            for (const auto& [consumer_kind, consumer_name] : consumers) {
                if (consumer_kind == Consumer::Indirect && producer_kind != Producer::Fill) {
                    continue;
                }
                for (const Mechanism mechanism : {Mechanism::Barrier, Mechanism::Event}) {
                    const std::string name = std::string(producer_name) + " to " + consumer_name + (mechanism == Mechanism::Barrier ? " by pipeline barrier" : " by event");
                    const VkPipelineStageFlags source_stage = producer_kind == Producer::Fill ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    const VkAccessFlags source_access = producer_kind == Producer::Fill ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT;
                    VkPipelineStageFlags destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    VkAccessFlags destination_access = VK_ACCESS_TRANSFER_READ_BIT;
                    if (consumer_kind == Consumer::Compute) {
                        destination_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                        destination_access = VK_ACCESS_SHADER_READ_BIT;
                    } else if (consumer_kind == Consumer::Indirect) {
                        destination_stage = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
                        destination_access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                    }
                    sink.fill_value(0xCD);
                    sink.publish();
                    VulkanOwned<VkEvent> local;
                    if (mechanism == Mechanism::Event && !Kit::succeeds(problems, name + " event", rig.make_event(local))) {
                        continue;
                    }
                    const VkResult result = rig.run([&](VkCommandBuffer cb) {
                        produce(cb, producer_kind);
                        VkMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                        barrier.srcAccessMask = source_access;
                        barrier.dstAccessMask = destination_access;
                        VkEvent event_handle = local.get();
                        if (mechanism == Mechanism::Event) {
                            rig.fn.vkCmdSetEvent(cb, event_handle, source_stage);
                            rig.fn.vkCmdFillBuffer(cb, unrelated.buffer, 0, words * 4, 0x77);
                            rig.fn.vkCmdWaitEvents(cb, 1, &event_handle, source_stage, destination_stage, 1, &barrier, 0, nullptr, 0, nullptr);
                        } else {
                            rig.fn.vkCmdPipelineBarrier(cb, source_stage, destination_stage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
                        }
                        if (consumer_kind == Consumer::Copy) {
                            const VkBufferCopy region{0, 0, words * 4};
                            rig.fn.vkCmdCopyBuffer(cb, b1.buffer, sink.buffer, 1, &region);
                            Kit::transfer_barrier(rig, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
                        } else if (consumer_kind == Consumer::Compute) {
                            consumer.bind(cb);
                            consumer.push(cb, push, sizeof(push));
                            consumer.dispatch(cb, static_cast<std::uint32_t>(words / 64));
                            Kit::transfer_barrier(rig, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
                        } else {
                            indirect_consumer.bind(cb);
                            indirect_consumer.push(cb, push, sizeof(push));
                            rig.fn.vkCmdDispatchIndirect(cb, b1.buffer, 0);
                            Kit::transfer_barrier(rig, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
                        }
                    });
                    if (!Kit::succeeds(problems, name, result)) {
                        continue;
                    }
                    sink.acquire();
                    std::vector<std::uint32_t> expected(words, 0xCDCDCDCDu);
                    if (consumer_kind == Consumer::Indirect) {
                        for (std::size_t i = 0; i < 64; ++i) {
                            expected[i] = data[i];
                        }
                    } else {
                        for (std::size_t i = 0; i < words; ++i) {
                            expected[i] = producer_kind == Producer::Fill ? 1u : data[i];
                        }
                    }
                    Kit::same_words(context, problems, name, expected, std::vector<std::uint32_t>(sink.words(), sink.words() + words));
                }
            }
        }
        VulkanImage image;
        const std::uint32_t side = 16;
        VulkanBuffer first_data;
        VulkanBuffer second_data;
        VulkanBuffer first_out;
        VulkanBuffer second_out;
        const VkDeviceSize image_bytes = side * side * 4;
        if (Kit::succeeds(problems, "transition image", image.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, side, side, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) &&
            Kit::succeeds(problems, "first data", Kit::host_buffer(rig, first_data, image_bytes)) && Kit::succeeds(problems, "second data", Kit::host_buffer(rig, second_data, image_bytes)) &&
            Kit::succeeds(problems, "first output", Kit::host_buffer(rig, first_out, image_bytes)) && Kit::succeeds(problems, "second output", Kit::host_buffer(rig, second_out, image_bytes))) {
            first_data.fill_pattern(0x1A);
            second_data.fill_pattern(0x1B);
            first_data.publish();
            second_data.publish();
            first_out.fill_value(0xCD);
            second_out.fill_value(0xCD);
            first_out.publish();
            second_out.publish();
            const VkBufferImageCopy region{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {side, side, 1}};
            const auto transition = [&](VkCommandBuffer cb, VkImageLayout from, VkImageLayout to, VkAccessFlags source, VkAccessFlags destination, VkPipelineStageFlags source_stage) {
                const VkImageMemoryBarrier barrier = VulkanImage::barrier(image.image, from, to, source, destination, VK_IMAGE_ASPECT_COLOR_BIT);
                rig.fn.vkCmdPipelineBarrier(cb, source_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            };
            const VkResult sequence = rig.run([&](VkCommandBuffer cb) {
                transition(cb, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
                rig.fn.vkCmdCopyBufferToImage(cb, first_data.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
                rig.fn.vkCmdCopyImageToBuffer(cb, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, first_out.buffer, 1, &region);
                transition(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                rig.fn.vkCmdCopyBufferToImage(cb, second_data.buffer, image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
                transition(cb, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                rig.fn.vkCmdCopyImageToBuffer(cb, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, second_out.buffer, 1, &region);
                Kit::transfer_barrier(rig, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
            });
            if (Kit::succeeds(problems, "layout transition sequence", sequence)) {
                first_out.acquire();
                second_out.acquire();
                Kit::same_bytes(context, problems, "image data after DST to SRC transition", first_data.bytes(), first_out.bytes(), image_bytes);
                Kit::same_bytes(context, problems, "image data after SRC to GENERAL to SRC transitions", second_data.bytes(), second_out.bytes(), image_bytes);
            }
        }
        return {};
    });
}

Outcome SyncChecks::memory_model(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        if (!rig.memory_model || !rig.memory_model_device) {
            return "vulkanMemoryModel and vulkanMemoryModelDeviceScope are not reported by this device";
        }
        const std::uint32_t pairs = 256;
        VulkanBuffer data;
        VulkanBuffer flags;
        VulkanBuffer result;
        VulkanCompute program;
        if (!Kit::succeeds(problems, "data buffer", Kit::host_buffer(rig, data, pairs * 4)) || !Kit::succeeds(problems, "flag buffer", Kit::host_buffer(rig, flags, pairs * 4)) ||
            !Kit::succeeds(problems, "result buffer", Kit::host_buffer(rig, result, pairs * 4)) ||
            !Kit::succeeds(problems, "memory model pipeline",
                           program.create(rig, VulkanShaders::words(VulkanShader::MemoryModel),
                                          {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, 0))) {
            return {};
        }
        program.write_buffer(0, data.buffer);
        program.write_buffer(1, flags.buffer);
        program.write_buffer(2, result.buffer);
        std::uint64_t observed = 0;
        std::uint64_t unobserved = 0;
        for (int round = 0; round < 40; ++round) {
            data.fill_value(0);
            flags.fill_value(0);
            result.fill_value(0);
            data.publish();
            flags.publish();
            result.publish();
            if (!Kit::succeeds(problems, "message passing dispatch", program.run(nullptr, 0, pairs * 2))) {
                return {};
            }
            result.acquire();
            for (std::uint32_t p = 0; p < pairs; ++p) {
                const std::uint32_t seen = result.words()[p];
                if (seen == 0xFFFFFFFFu) {
                    ++unobserved;
                } else if (seen != 1234 + p) {
                    problems.add("pair " + std::to_string(p) + " in round " + std::to_string(round) + ": the reader saw the flag but read " + std::to_string(seen) + ", expected " +
                                 std::to_string(1234 + p));
                    return {};
                } else {
                    ++observed;
                }
            }
        }
        context.measure("vk.message_passing_observed", observed);
        context.measure("vk.message_passing_unobserved", unobserved);
        return {};
    });
}

Outcome SyncChecks::submit2_and_wait_idle(Context& context) {
    if (ChildMode::active()) {
        return GateWindow::peer(context);
    }
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        VulkanBuffer source;
        VulkanBuffer middle;
        VulkanBuffer sink;
        if (!Kit::succeeds(problems, "source", Kit::host_buffer(rig, source, kSize)) || !Kit::succeeds(problems, "middle", Kit::device_buffer(rig, middle, kSize)) ||
            !Kit::succeeds(problems, "sink", Kit::host_buffer(rig, sink, kSize))) {
            return {};
        }
        VulkanQueue& queue = rig.main();
        if (rig.sync2 && rig.fn.vkQueueSubmit2 != nullptr) {
            source.fill_pattern(0x5B2);
            source.publish();
            sink.fill_value(0xCD);
            sink.publish();
            VulkanOwned<VkSemaphore> semaphore;
            VulkanOwned<VkSemaphore> timeline;
            VulkanOwned<VkFence> fence;
            if (Kit::succeeds(problems, "binary semaphore", rig.make_semaphore(semaphore)) && Kit::succeeds(problems, "fence", rig.make_fence(fence))) {
                VkResult code = VK_SUCCESS;
                const VkCommandBuffer upload = Kit::record(rig, queue, [&](VkCommandBuffer cb) {
                    const VkBufferCopy region{0, 0, kSize};
                    rig.fn.vkCmdCopyBuffer(cb, source.buffer, middle.buffer, 1, &region);
                }, 0, &code);
                const VkCommandBuffer download = Kit::record(rig, queue, [&](VkCommandBuffer cb) {
                    const VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                   VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT};
                    const VkBufferCopy region{0, 0, kSize};
                    rig.fn.vkCmdCopyBuffer(cb, middle.buffer, sink.buffer, 1, &region);
                    VkDependencyInfo dependency{};
                    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dependency.memoryBarrierCount = 1;
                    dependency.pMemoryBarriers = &barrier;
                    rig.fn.vkCmdPipelineBarrier2(cb, &dependency);
                }, 0, &code);
                if (upload != nullptr && download != nullptr) {
                    VkCommandBufferSubmitInfo upload_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, upload, 0};
                    VkCommandBufferSubmitInfo download_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, download, 0};
                    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, semaphore.get(), 0, VK_PIPELINE_STAGE_2_COPY_BIT, 0};
                    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, semaphore.get(), 0, VK_PIPELINE_STAGE_2_COPY_BIT, 0};
                    VkSubmitInfo2 first{};
                    first.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
                    first.commandBufferInfoCount = 1;
                    first.pCommandBufferInfos = &upload_info;
                    first.signalSemaphoreInfoCount = 1;
                    first.pSignalSemaphoreInfos = &signal;
                    VkSubmitInfo2 second{};
                    second.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
                    second.waitSemaphoreInfoCount = 1;
                    second.pWaitSemaphoreInfos = &wait;
                    second.commandBufferInfoCount = 1;
                    second.pCommandBufferInfos = &download_info;
                    rig.mark_pending();
                    if (Kit::succeeds(problems, "vkQueueSubmit2 of the producer", rig.fn.vkQueueSubmit2(queue.handle, 1, &first, VK_NULL_HANDLE)) &&
                        Kit::succeeds(problems, "vkQueueSubmit2 of the consumer", rig.fn.vkQueueSubmit2(queue.handle, 1, &second, fence.get())) &&
                        Kit::succeeds(problems, "completion of the vkQueueSubmit2 chain", rig.wait_fence(fence.get(), 20 * kSecond))) {
                        sink.acquire();
                        Kit::same_bytes(context, problems, "data moved by vkQueueSubmit2 with stage-specific semaphores", source.bytes(), sink.bytes(), kSize);
                    }
                }
                if (rig.timeline && Kit::succeeds(problems, "timeline semaphore", rig.make_semaphore(timeline, true, 0))) {
                    sink.fill_value(0xCD);
                    sink.publish();
                    const VkCommandBuffer copy = Kit::record(rig, queue, [&](VkCommandBuffer cb) { copy_to_host(rig, cb, source.buffer, sink.buffer, kSize); }, 0, &code);
                    if (copy != nullptr) {
                        VkCommandBufferSubmitInfo copy_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, copy, 0};
                        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, timeline.get(), 7, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
                        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, timeline.get(), 8, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
                        VkSubmitInfo2 info{};
                        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
                        info.waitSemaphoreInfoCount = 1;
                        info.pWaitSemaphoreInfos = &wait;
                        info.commandBufferInfoCount = 1;
                        info.pCommandBufferInfos = &copy_info;
                        info.signalSemaphoreInfoCount = 1;
                        info.pSignalSemaphoreInfos = &signal;
                        rig.mark_pending();
                        GateWindow timeline_window(context.id());
                        timeline_window.open(problems);
                        if (Kit::succeeds(problems, "vkQueueSubmit2 with timeline values", rig.fn.vkQueueSubmit2(queue.handle, 1, &info, VK_NULL_HANDLE))) {
                            timeline_window.begin();
                            VkSemaphore handle = timeline.get();
                            std::uint64_t target = 8;
                            timeline_window.sleep_until(2600);
                            VkSemaphoreWaitInfo probe_info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1, &handle, &target};
                            Kit::expect(problems, "bounded host wait on a timeline value nobody signalled", rig.fn.vkWaitSemaphores(rig.device, &probe_info, 100000000), {VK_TIMEOUT});
                            timeline_window.sleep_until_release();
                            sink.acquire();
                            if (!untouched(sink, kSize, 0xCD)) {
                                problems.add("the copy waiting for a timeline value ran before the host signalled it");
                            }
                            VkSemaphoreSignalInfo host{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr, timeline.get(), 7};
                            Kit::succeeds(problems, "host signal releasing the timeline wait", rig.fn.vkSignalSemaphore(rig.device, &host));
                            timeline_window.close(context, problems, "submit2.timeline_gate");
                            VkSemaphoreWaitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1, &handle, &target};
                            if (Kit::succeeds(problems, "host wait for the stage-specific timeline signal", rig.fn.vkWaitSemaphores(rig.device, &wait_info, 20 * kSecond))) {
                                sink.acquire();
                                Kit::same_bytes(context, problems, "data copied after a timeline wait in vkQueueSubmit2", source.bytes(), sink.bytes(), kSize);
                            }
                        }
                    }
                }
            }
        } else {
            context.note("synchronization2 is not reported; vkQueueSubmit2 is skipped");
        }
        rig.wait_idle();
        for (const bool whole_device : {false, true}) {
            const std::string name = whole_device ? "vkDeviceWaitIdle" : "vkQueueWaitIdle";
            Kit::succeeds(problems, name + " on an idle device", whole_device ? rig.fn.vkDeviceWaitIdle(rig.device) : rig.fn.vkQueueWaitIdle(queue.handle));
            VulkanOwned<VkEvent> gate;
            if (!Kit::succeeds(problems, name + " gate", rig.make_event(gate))) {
                continue;
            }
            source.fill_pattern(whole_device ? 0x5B4 : 0x5B3);
            source.publish();
            sink.fill_value(0xCD);
            sink.publish();
            VkResult code = VK_SUCCESS;
            const VkCommandBuffer commands = Kit::record(rig, queue, [&](VkCommandBuffer cb) {
                wait_for_host_event(rig, cb, gate.get());
                copy_to_host(rig, cb, source.buffer, sink.buffer, kSize);
            }, 0, &code);
            GateWindow window(context.id());
            window.open(problems);
            if (commands == nullptr || !Kit::succeeds(problems, name + " gated submission", submit_one(rig, queue, commands, VK_NULL_HANDLE))) {
                continue;
            }
            window.begin();
            std::atomic<bool> released{false};
            std::atomic<bool> early{false};
            std::atomic<int> code_seen{VK_SUCCESS};
            std::thread waiter([&] {
                code_seen = whole_device ? rig.fn.vkDeviceWaitIdle(rig.device) : rig.fn.vkQueueWaitIdle(queue.handle);
                early = !released.load();
            });
            window.sleep_until_release();
            const bool returned_while_gated = early.load();
            sink.acquire();
            const bool copied_early = !untouched(sink, kSize, 0xCD);
            released = true;
            (void)rig.fn.vkSetEvent(rig.device, gate.get());
            window.close(context, problems, whole_device ? "device_wait_idle.gate" : "queue_wait_idle.gate");
            waiter.join();
            if (copied_early) {
                problems.add(name + ": the gated copy wrote its destination before the event was set");
            }
            if (returned_while_gated || early.load()) {
                problems.add(name + " returned while submitted work was still waiting for an event");
            }
            if (code_seen.load() != VK_SUCCESS) {
                problems.add(name + " returned " + Kit::name(static_cast<VkResult>(code_seen.load())));
            }
            sink.acquire();
            Kit::same_bytes(context, problems, "data after " + name, source.bytes(), sink.bytes(), kSize);
        }
        rig.wait_idle();
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
