#include "sanity/vk/query_checks.hpp"

#include "sanity/vk/kit.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace lsw::suites::sanity::vk {

namespace {

constexpr std::uint64_t kSecond = 1000000000ull;

std::uint64_t mask_for(std::uint32_t bits) {
    return bits >= 64 ? ~0ull : ((1ull << bits) - 1);
}

}  // namespace

Outcome QueryChecks::timestamps(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const double period = static_cast<double>(rig.props.limits.timestampPeriod);
        for (VulkanQueue* family_queue : rig.queues_supporting(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            VulkanQueue& queue = *family_queue;
            const std::uint32_t bits = queue.properties.timestampValidBits;
            if (bits == 0) {
                continue;
            }
            const std::string label = "queue " + std::to_string(queue.index) + " of family " + std::to_string(queue.family);
            VulkanOwned<VkQueryPool> pool;
            VulkanBuffer scratch;
            if (!Kit::succeeds(problems, label + " timestamp pool", rig.make_query_pool(pool, VK_QUERY_TYPE_TIMESTAMP, 4)) ||
                !Kit::succeeds(problems, label + " scratch", Kit::device_buffer(rig, scratch, 1 << 20))) {
                continue;
            }
            const VkResult recorded = rig.run(
                [&](VkCommandBuffer cb) {
                    rig.fn.vkCmdResetQueryPool(cb, pool.get(), 0, 4);
                    rig.fn.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool.get(), 0);
                    rig.fn.vkCmdFillBuffer(cb, scratch.buffer, 0, 1 << 20, 1);
                    Kit::transfer_barrier(rig, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                    rig.fn.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, pool.get(), 1);
                    rig.fn.vkCmdFillBuffer(cb, scratch.buffer, 0, 1 << 20, 2);
                    Kit::transfer_barrier(rig, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                    rig.fn.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, pool.get(), 2);
                    rig.fn.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool.get(), 3);
                },
                &queue);
            if (!Kit::succeeds(problems, label + " timestamp submission", recorded)) {
                continue;
            }
            std::uint64_t values[4] = {};
            if (!Kit::succeeds(problems, label + " results", rig.fn.vkGetQueryPoolResults(rig.device, pool.get(), 0, 4, sizeof(values), values, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT))) {
                continue;
            }
            const std::uint64_t mask = mask_for(bits);
            std::uint64_t previous = values[0] & mask;
            for (int i = 1; i < 4; ++i) {
                const std::uint64_t now = values[i] & mask;
                const std::uint64_t delta = (now - previous) & mask;
                if (delta > (mask >> 1)) {
                    problems.add(label + ": timestamp " + std::to_string(i) + " is behind timestamp " + std::to_string(i - 1));
                }
                previous = now;
            }
            const double span_ms = static_cast<double>((values[3] - values[0]) & mask) * period / 1e6;
            context.measure("vk.timestamp_span_ms." + std::to_string(queue.family) + "." + std::to_string(queue.index), span_ms);
            if (span_ms < 0.0 || span_ms > 10000.0) {
                problems.add(label + ": the timestamp span converts to " + std::to_string(span_ms) + " ms");
            }
        }
        VulkanQueue& queue = rig.main();
        VulkanOwned<VkQueryPool> pool;
        VulkanOwned<VkEvent> gate;
        if (queue.properties.timestampValidBits != 0 && Kit::succeeds(problems, "availability pool", rig.make_query_pool(pool, VK_QUERY_TYPE_TIMESTAMP, 1)) &&
            Kit::succeeds(problems, "gate", rig.make_event(gate))) {
            Kit::succeeds(problems, "pool reset", rig.run([&](VkCommandBuffer cb) { rig.fn.vkCmdResetQueryPool(cb, pool.get(), 0, 1); }));
            std::uint64_t result[2] = {0xFF, 0xFF};
            Kit::expect(problems, "results of a reset query", rig.fn.vkGetQueryPoolResults(rig.device, pool.get(), 0, 1, sizeof(result), result, 16,
                                                                                        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT), {VK_NOT_READY});
            if (result[1] != 0) {
                problems.add("the availability word of a reset query is " + std::to_string(result[1]));
            }
            VkResult code = VK_SUCCESS;
            const VkCommandBuffer commands = Kit::record(rig, queue, [&](VkCommandBuffer cb) {
                VkEvent event = gate.get();
                rig.fn.vkCmdWaitEvents(cb, 1, &event, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, nullptr, 0, nullptr, 0, nullptr);
                rig.fn.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool.get(), 0);
            }, 0, &code);
            VulkanOwned<VkFence> fence;
            if (commands != nullptr && Kit::succeeds(problems, "fence", rig.make_fence(fence))) {
                VkSubmitInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                info.commandBufferCount = 1;
                info.pCommandBuffers = &commands;
                if (Kit::succeeds(problems, "gated timestamp submission", rig.submit(queue, 1, &info, fence.get()))) {
                    std::uint64_t probe = 0;
                    const VkResult unfinished = rig.fn.vkGetQueryPoolResults(rig.device, pool.get(), 0, 1, 8, &probe, 8, VK_QUERY_RESULT_64_BIT);
                    std::atomic<bool> started{false};
                    std::atomic<bool> released{false};
                    std::atomic<bool> early{false};
                    std::atomic<int> seen{VK_SUCCESS};
                    std::thread waiter([&] {
                        started = true;
                        std::uint64_t value = 0;
                        seen = rig.fn.vkGetQueryPoolResults(rig.device, pool.get(), 0, 1, 8, &value, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
                        early = !released.load();
                    });
                    while (!started.load()) {
                        std::this_thread::yield();
                    }
                    released = true;
                    (void)rig.fn.vkSetEvent(rig.device, gate.get());
                    waiter.join();
                    if (early.load()) {
                        problems.add("vkGetQueryPoolResults with WAIT_BIT returned before the query was written");
                    }
                    if (unfinished != VK_NOT_READY) {
                        problems.add("vkGetQueryPoolResults without WAIT_BIT on an unwritten query returned " + Kit::name(unfinished) + ", expected VK_NOT_READY");
                    }
                    if (seen.load() != VK_SUCCESS) {
                        problems.add("the waiting vkGetQueryPoolResults returned " + Kit::name(static_cast<VkResult>(seen.load())));
                    }
                    Kit::succeeds(problems, "completion", rig.wait_fence(fence.get(), 20 * kSecond));
                    Kit::succeeds(problems, "results after availability", rig.fn.vkGetQueryPoolResults(rig.device, pool.get(), 0, 1, sizeof(result), result, 16,
                                                                                                   VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
                    if (result[1] == 0) {
                        problems.add("the availability word is 0 after the query completed");
                    }
                }
            }
        }
        rig.wait_idle();
        {
            const std::vector<SceneRect> rects = {
                SceneRect{2, 2, 10, 10, {1, 0, 0, 1}, 0.3f}, SceneRect{6, 6, 14, 14, {0, 1, 0, 1}, 0.6f}, SceneRect{3, 3, 8, 8, {0, 0, 1, 1}, 0.9f}};
            VulkanOwned<VkQueryPool> occlusion;
            VulkanOwned<VkQueryPool> statistics;
            const VkQueryPipelineStatisticFlags wanted = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
                                                         VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
            SceneOptions options;
            options.depth = true;
            options.query_count = 3;
            const bool precise = rig.features.occlusionQueryPrecise != VK_FALSE;
            options.occlusion_flags = precise ? VK_QUERY_CONTROL_PRECISE_BIT : 0;
            bool ready = Kit::succeeds(problems, "occlusion pool", rig.make_query_pool(occlusion, VK_QUERY_TYPE_OCCLUSION, 3));
            options.occlusion_pool = occlusion.get();
            const bool use_statistics = rig.features.pipelineStatisticsQuery != VK_FALSE;
            if (use_statistics) {
                ready = ready && Kit::succeeds(problems, "statistics pool", rig.make_query_pool(statistics, VK_QUERY_TYPE_PIPELINE_STATISTICS, 1, wanted));
                options.statistics_pool = statistics.get();
            } else {
                context.note("pipelineStatisticsQuery is not reported; statistics queries skipped");
            }
            VulkanScene scene;
            SceneResult out;
            const float clear[4] = {0, 0, 0, 1};
            if (ready && Kit::succeeds(problems, "query scene", scene.create(rig, options)) && Kit::succeeds(problems, "query render", scene.render(rects, clear, 1.0f, out))) {
                std::uint64_t counts[3] = {};
                if (Kit::succeeds(problems, "occlusion results", rig.fn.vkGetQueryPoolResults(rig.device, occlusion.get(), 0, 3, sizeof(counts), counts, 8,
                                                                                            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT))) {
                    const std::uint64_t expected[3] = {64, 48, 0};
                    for (int i = 0; i < 3; ++i) {
                        if (precise ? counts[i] != expected[i] : (counts[i] != 0) != (expected[i] != 0)) {
                            problems.add("occlusion query " + std::to_string(i) + " returned " + std::to_string(counts[i]) + ", expected " + (precise ? std::to_string(expected[i]) : expected[i] != 0 ? "non-zero" : "0"));
                        }
                    }
                }
                if (use_statistics) {
                    std::uint64_t stats[3] = {};
                    if (Kit::succeeds(problems, "statistics results", rig.fn.vkGetQueryPoolResults(rig.device, statistics.get(), 0, 1, sizeof(stats), stats, sizeof(stats),
                                                                                                 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT))) {
                        if (stats[0] != 12 || stats[1] != 6) {
                            problems.add("input assembly counted " + std::to_string(stats[0]) + " vertices and " + std::to_string(stats[1]) + " primitives, expected 12 and 6");
                        }
                        if (stats[2] < 64 + 64 + 25) {
                            problems.add("fragment shader invocations " + std::to_string(stats[2]) + " are below the 153 covered fragments");
                        }
                    }
                }
            }
        }
        if (rig.enabled_has("VK_EXT_calibrated_timestamps") && rig.fn.vkGetCalibratedTimestampsEXT != nullptr && rig.fn.vkGetPhysicalDeviceCalibrateableTimeDomainsEXT != nullptr) {
            std::uint32_t count = 0;
            rig.fn.vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(rig.physical, &count, nullptr);
            std::vector<VkTimeDomainEXT> domains(count);
            rig.fn.vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(rig.physical, &count, domains.data());
            bool device_domain = false;
            bool counter_domain = false;
            for (const VkTimeDomainEXT domain : domains) {
                device_domain = device_domain || domain == VK_TIME_DOMAIN_DEVICE_EXT;
                counter_domain = counter_domain || domain == VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
            }
            if (!device_domain) {
                problems.add("the calibrateable time domains do not include the device domain");
            } else if (counter_domain) {
                LARGE_INTEGER frequency{};
                ::QueryPerformanceFrequency(&frequency);
                const VkCalibratedTimestampInfoEXT infos[2] = {{VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, nullptr, VK_TIME_DOMAIN_DEVICE_EXT},
                                                               {VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, nullptr, VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT}};
                std::uint64_t first[2] = {};
                std::uint64_t second[2] = {};
                std::uint64_t deviation = 0;
                LARGE_INTEGER before{};
                LARGE_INTEGER middle{};
                LARGE_INTEGER after{};
                ::QueryPerformanceCounter(&before);
                const VkResult a = rig.fn.vkGetCalibratedTimestampsEXT(rig.device, 2, infos, first, &deviation);
                ::QueryPerformanceCounter(&middle);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                const VkResult b = rig.fn.vkGetCalibratedTimestampsEXT(rig.device, 2, infos, second, &deviation);
                ::QueryPerformanceCounter(&after);
                if (Kit::succeeds(problems, "first calibrated timestamps", a) && Kit::succeeds(problems, "second calibrated timestamps", b)) {
                    const double slack = static_cast<double>(deviation) / 1e9 * static_cast<double>(frequency.QuadPart) + static_cast<double>(frequency.QuadPart) / 1000.0;
                    if (static_cast<double>(first[1]) < static_cast<double>(before.QuadPart) - slack || static_cast<double>(first[1]) > static_cast<double>(middle.QuadPart) + slack) {
                        problems.add("the calibrated performance counter value lies outside the QueryPerformanceCounter interval around the call");
                    }
                    const double device_ms = static_cast<double>(second[0] - first[0]) * period / 1e6;
                    const double counter_ms = static_cast<double>(second[1] - first[1]) * 1000.0 / static_cast<double>(frequency.QuadPart);
                    context.measure("vk.calibrated_device_ms", device_ms);
                    context.measure("vk.calibrated_counter_ms", counter_ms);
                    if (second[0] < first[0] || device_ms < counter_ms * 0.75 || device_ms > counter_ms * 1.25) {
                        problems.add("over " + std::to_string(counter_ms) + " ms of QueryPerformanceCounter time the device clock advanced " + std::to_string(device_ms) + " ms");
                    }
                }
            }
        }
        return {};
    });
}

Outcome QueryChecks::buffers_reset_reuse(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        VulkanQueue& queue = rig.main();
        VulkanBuffer source;
        VulkanBuffer sink;
        const VkDeviceSize size = 4096;
        if (!Kit::succeeds(problems, "source", Kit::host_buffer(rig, source, size)) || !Kit::succeeds(problems, "sink", Kit::host_buffer(rig, sink, size))) {
            return {};
        }
        const auto record = [&](VkCommandBuffer cb) {
            const VkBufferCopy region{0, 0, size};
            rig.fn.vkCmdCopyBuffer(cb, source.buffer, sink.buffer, 1, &region);
            Kit::transfer_barrier(rig, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        };
        const auto submit_and_check = [&](VkCommandBuffer commands, const std::string& name, std::uint64_t seed) {
            source.fill_pattern(seed);
            source.publish();
            sink.fill_value(0xCD);
            sink.publish();
            VulkanOwned<VkFence> fence;
            if (!Kit::succeeds(problems, name + " fence", rig.make_fence(fence))) {
                return;
            }
            VkSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &commands;
            if (Kit::succeeds(problems, name + " submission", rig.submit(queue, 1, &info, fence.get())) && Kit::succeeds(problems, name + " completion", rig.wait_fence(fence.get(), 20 * kSecond))) {
                sink.acquire();
                Kit::same_bytes(context, problems, name, source.bytes(), sink.bytes(), size);
            }
        };
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = queue.family;
        VulkanOwned<VkCommandPool> plain_pool;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VulkanOwned<VkCommandPool> resettable;
        if (!Kit::succeeds(problems, "pool without per-buffer reset", rig.make(plain_pool, rig.fn.vkCreateCommandPool, rig.fn.vkDestroyCommandPool, VkCommandPoolCreateInfo{
                                                                                  VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0, queue.family})) ||
            !Kit::succeeds(problems, "pool with per-buffer reset", rig.make(resettable, rig.fn.vkCreateCommandPool, rig.fn.vkDestroyCommandPool, pool_info))) {
            return {};
        }
        const auto allocate = [&](VkCommandPool pool, VkCommandBufferLevel level) {
            VkCommandBufferAllocateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            info.commandPool = pool;
            info.level = level;
            info.commandBufferCount = 1;
            VkCommandBuffer commands = nullptr;
            Kit::succeeds(problems, "command buffer allocation", rig.fn.vkAllocateCommandBuffers(rig.device, &info, &commands));
            return commands;
        };
        const auto begin = [&](VkCommandBuffer commands, VkCommandBufferUsageFlags flags) {
            VkCommandBufferBeginInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags = flags;
            return rig.fn.vkBeginCommandBuffer(commands, &info);
        };
        VkCommandBuffer simultaneous = allocate(resettable.get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        if (simultaneous != nullptr && Kit::succeeds(problems, "begin with SIMULTANEOUS_USE", begin(simultaneous, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))) {
            record(simultaneous);
            if (Kit::succeeds(problems, "end of the simultaneous buffer", rig.fn.vkEndCommandBuffer(simultaneous))) {
                for (std::uint64_t i = 0; i < 4; ++i) {
                    submit_and_check(simultaneous, "SIMULTANEOUS_USE buffer submitted again, round " + std::to_string(i), 0x100 + i);
                }
            }
        }
        VkCommandBuffer one_time = allocate(resettable.get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        for (std::uint64_t i = 0; one_time != nullptr && i < 3; ++i) {
            if (Kit::succeeds(problems, "re-begin of a ONE_TIME_SUBMIT buffer (implicit reset)", begin(one_time, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))) {
                record(one_time);
                if (Kit::succeeds(problems, "end of the one-time buffer", rig.fn.vkEndCommandBuffer(one_time))) {
                    submit_and_check(one_time, "ONE_TIME_SUBMIT buffer re-recorded, round " + std::to_string(i), 0x200 + i);
                }
            }
        }
        VkCommandBuffer explicit_reset = allocate(resettable.get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        if (explicit_reset != nullptr && Kit::succeeds(problems, "begin", begin(explicit_reset, 0))) {
            record(explicit_reset);
            if (Kit::succeeds(problems, "end", rig.fn.vkEndCommandBuffer(explicit_reset))) {
                submit_and_check(explicit_reset, "buffer before vkResetCommandBuffer", 0x300);
                Kit::succeeds(problems, "vkResetCommandBuffer", rig.fn.vkResetCommandBuffer(explicit_reset, 0));
                if (Kit::succeeds(problems, "begin after reset", begin(explicit_reset, 0))) {
                    record(explicit_reset);
                    if (Kit::succeeds(problems, "end after reset", rig.fn.vkEndCommandBuffer(explicit_reset))) {
                        submit_and_check(explicit_reset, "buffer re-recorded after vkResetCommandBuffer", 0x301);
                    }
                }
            }
        }
        VkCommandBuffer pooled[3] = {allocate(plain_pool.get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY), allocate(plain_pool.get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY),
                                     allocate(plain_pool.get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY)};
        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 3; ++i) {
                if (pooled[i] != nullptr && Kit::succeeds(problems, "begin in a pooled buffer", begin(pooled[i], 0))) {
                    record(pooled[i]);
                    Kit::succeeds(problems, "end in a pooled buffer", rig.fn.vkEndCommandBuffer(pooled[i]));
                }
            }
            for (int i = 0; i < 3; ++i) {
                if (pooled[i] != nullptr) {
                    submit_and_check(pooled[i], "pooled buffer " + std::to_string(i) + " in round " + std::to_string(round), 0x400 + static_cast<std::uint64_t>(round * 3 + i));
                }
            }
            Kit::succeeds(problems, "vkResetCommandPool", rig.fn.vkResetCommandPool(rig.device, plain_pool.get(), 0));
        }
        rig.wait_idle();
        VkCommandBuffer secondary = allocate(resettable.get(), VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        VkCommandBuffer primary = allocate(resettable.get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        if (secondary != nullptr && primary != nullptr) {
            VkCommandBufferInheritanceInfo inherit{};
            inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
            VkCommandBufferBeginInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.pInheritanceInfo = &inherit;
            if (Kit::succeeds(problems, "secondary begin", rig.fn.vkBeginCommandBuffer(secondary, &info))) {
                record(secondary);
                if (Kit::succeeds(problems, "secondary end", rig.fn.vkEndCommandBuffer(secondary)) && Kit::succeeds(problems, "primary begin", begin(primary, 0))) {
                    rig.fn.vkCmdExecuteCommands(primary, 1, &secondary);
                    if (Kit::succeeds(problems, "primary end", rig.fn.vkEndCommandBuffer(primary))) {
                        submit_and_check(primary, "secondary command buffer executed from a primary", 0x500);
                    }
                }
            }
        }
        rig.wait_idle();
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
