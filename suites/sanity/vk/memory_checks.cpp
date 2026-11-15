#include "sanity/vk/memory_checks.hpp"

#include "sanity/vk/kit.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::vk {

namespace {

constexpr VkBufferUsageFlags kTransfer = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
constexpr VkDeviceSize kMiB = 1ull << 20;

std::string type_label(const VulkanRig& rig, std::uint32_t type) {
    const VkMemoryType& item = rig.memory.memoryTypes[type];
    return "memory type " + std::to_string(type) + " (" + VulkanTools::memory_flags_text(item.propertyFlags) + ", heap " +
           std::to_string(item.heapIndex) + ")";
}

bool power_of_two(std::uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

VkResult fill_and_publish(VulkanRig& rig, VkBuffer buffer, VkDeviceSize size, std::uint32_t value) {
    return rig.run([&](VkCommandBuffer commands) {
        rig.fn.vkCmdFillBuffer(commands, buffer, 0, size, value);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                                    0, nullptr);
    });
}

}  // namespace

Outcome MemoryChecks::every_type_allocates(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkDeviceSize max_allocation = rig.maintenance3.maxMemoryAllocationSize != 0 ? rig.maintenance3.maxMemoryAllocationSize : ~0ull;
        const std::uint32_t small_bits = Kit::buffer_type_bits(rig, kMiB, kTransfer);
        const std::uint32_t image_bits = Kit::image_type_bits(rig, VK_FORMAT_R8G8B8A8_UNORM, 256, 256, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        std::size_t large = 0;
        for (std::uint32_t type = 0; type < rig.memory.memoryTypeCount; ++type) {
            const std::string label = type_label(rig, type);
            const std::uint32_t heap = rig.memory.memoryTypes[type].heapIndex;
            const VkDeviceSize heap_size = rig.memory.memoryHeaps[heap].size;
            VkMemoryAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate.allocationSize = std::min<VkDeviceSize>(kMiB, heap_size);
            allocate.memoryTypeIndex = type;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            if (Kit::succeeds(problems, label + " small allocation", rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory))) {
                rig.fn.vkFreeMemory(rig.device, memory, nullptr);
            }
            if ((small_bits & (1u << type)) != 0) {
                VulkanBuffer buffer;
                if (Kit::succeeds(problems, label + " buffer bound to a small allocation",
                                  buffer.create_in_type(rig, kMiB, kTransfer, type, false))) {
                    if (!power_of_two(buffer.requirements.alignment) || buffer.requirements.size < kMiB) {
                        problems.add(label + " buffer requirements are invalid (size " + std::to_string(buffer.requirements.size) +
                                     ", alignment " + std::to_string(buffer.requirements.alignment) + ")");
                    }
                }
            }
            if ((image_bits & (1u << type)) != 0) {
                VulkanImage image;
                VkImageCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                info.imageType = VK_IMAGE_TYPE_2D;
                info.format = VK_FORMAT_R8G8B8A8_UNORM;
                info.extent = {256, 256, 1};
                info.mipLevels = 1;
                info.arrayLayers = 1;
                info.samples = VK_SAMPLE_COUNT_1_BIT;
                info.tiling = VK_IMAGE_TILING_OPTIMAL;
                info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                VulkanOwned<VkImage> owned;
                if (Kit::succeeds(problems, label + " image create", rig.make(owned, rig.fn.vkCreateImage, rig.fn.vkDestroyImage, info))) {
                    VkMemoryRequirements requirements{};
                    rig.fn.vkGetImageMemoryRequirements(rig.device, owned.get(), &requirements);
                    allocate.allocationSize = requirements.size;
                    if (Kit::succeeds(problems, label + " allocation for an image", rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory))) {
                        Kit::succeeds(problems, label + " image bind", rig.fn.vkBindImageMemory(rig.device, owned.get(), memory, 0));
                        owned.reset();
                        rig.fn.vkFreeMemory(rig.device, memory, nullptr);
                    }
                }
            }
            VkDeviceSize available = heap_size / 2;
            if (rig.enabled_has("VK_EXT_memory_budget")) {
                const std::uint64_t budget = rig.heap_budget(heap);
                const std::uint64_t usage = rig.heap_usage(heap);
                available = budget > usage ? (budget - usage) / 2 : 0;
            }
            const VkDeviceSize chunk = std::min({heap_size / 4, 128 * kMiB, max_allocation, available}) / kMiB * kMiB;
            if (chunk >= 4 * kMiB) {
                const std::uint32_t bits = Kit::buffer_type_bits(rig, chunk, kTransfer);
                if ((bits & (1u << type)) != 0) {
                    VulkanBuffer buffer;
                    Kit::succeeds(problems, label + " buffer bound to a " + VulkanTools::bytes_text(chunk) + " allocation",
                                  buffer.create_in_type(rig, chunk, kTransfer, type, false));
                } else {
                    allocate.allocationSize = chunk;
                    if (Kit::succeeds(problems, label + " " + VulkanTools::bytes_text(chunk) + " allocation",
                                      rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory))) {
                        rig.fn.vkFreeMemory(rig.device, memory, nullptr);
                    }
                }
                ++large;
            }
        }
        context.measure("vk.memory_types", rig.memory.memoryTypeCount);
        context.measure("vk.memory_types_large_chunk", large);
        return {};
    });
}

Outcome MemoryChecks::host_visible_map(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkDeviceSize size = 64 * 1024;
        const VkDeviceSize atom = rig.props.limits.nonCoherentAtomSize;
        const std::uint32_t bits = Kit::buffer_type_bits(rig, size, kTransfer);
        std::size_t exercised = 0;
        for (std::uint32_t type = 0; type < rig.memory.memoryTypeCount; ++type) {
            if ((rig.memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
                continue;
            }
            const std::string label = type_label(rig, type);
            if ((bits & (1u << type)) == 0) {
                context.note(label + " is not usable by transfer buffers; skipped");
                continue;
            }
            VulkanBuffer host;
            if (!Kit::succeeds(problems, label + " allocate and map", host.create_in_type(rig, size, kTransfer, type, true))) {
                continue;
            }
            ++exercised;
            const std::uint64_t alignment = rig.props.limits.minMemoryMapAlignment;
            if (alignment != 0 && reinterpret_cast<std::uintptr_t>(host.mapped) % alignment != 0) {
                problems.add(label + " mapping is not aligned to minMemoryMapAlignment " + std::to_string(alignment));
            }
            const std::vector<std::uint8_t> pattern = [&] {
                std::vector<std::uint8_t> data(size);
                Pattern::fill(0xA000 + type, 0, data.data(), data.size());
                return data;
            }();
            std::memcpy(host.mapped, pattern.data(), size);
            host.publish();
            VulkanBuffer device;
            VulkanBuffer readback;
            if (!Kit::succeeds(problems, label + " device buffer", Kit::device_buffer(rig, device, size, kTransfer)) ||
                !Kit::succeeds(problems, label + " readback buffer", Kit::host_buffer(rig, readback, size, kTransfer))) {
                continue;
            }
            readback.fill_value(0xCD);
            readback.publish();
            if (Kit::succeeds(problems, label + " copy host to device", Kit::copy(rig, host.buffer, device.buffer, size)) &&
                Kit::succeeds(problems, label + " copy device to readback", Kit::copy(rig, device.buffer, readback.buffer, size))) {
                readback.acquire();
                Kit::same_bytes(context, problems, label + " GPU roundtrip", pattern.data(), readback.bytes(), size);
            }
            rig.fn.vkUnmapMemory(rig.device, host.memory);
            host.mapped = nullptr;
            void* again = nullptr;
            if (Kit::succeeds(problems, label + " remap after unmap", rig.fn.vkMapMemory(rig.device, host.memory, 0, VK_WHOLE_SIZE, 0, &again))) {
                host.mapped = again;
                host.acquire();
                Kit::same_bytes(context, problems, label + " contents after unmap and map", pattern.data(), host.bytes(), size);
                rig.fn.vkUnmapMemory(rig.device, host.memory);
                host.mapped = nullptr;
            }
            const VkDeviceSize offset = atom * 2;
            const VkDeviceSize length = atom * 2;
            void* partial = nullptr;
            if (Kit::succeeds(problems, label + " partial map", rig.fn.vkMapMemory(rig.device, host.memory, offset, length, 0, &partial))) {
                std::memset(partial, 0x6B, static_cast<std::size_t>(length));
                if (!host.coherent()) {
                    VkMappedMemoryRange range{};
                    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                    range.memory = host.memory;
                    range.offset = offset;
                    range.size = length;
                    Kit::succeeds(problems, label + " flush of the partial map", rig.fn.vkFlushMappedMemoryRanges(rig.device, 1, &range));
                }
                rig.fn.vkUnmapMemory(rig.device, host.memory);
                void* whole = nullptr;
                if (Kit::succeeds(problems, label + " remap whole", rig.fn.vkMapMemory(rig.device, host.memory, 0, VK_WHOLE_SIZE, 0, &whole))) {
                    host.mapped = whole;
                    host.acquire();
                    std::vector<std::uint8_t> expected = pattern;
                    std::memset(expected.data() + offset, 0x6B, static_cast<std::size_t>(length));
                    Kit::same_bytes(context, problems, label + " contents written through a partial map", expected.data(), host.bytes(), size);
                    rig.fn.vkUnmapMemory(rig.device, host.memory);
                    host.mapped = nullptr;
                }
            }
        }
        context.measure("vk.host_visible_types_exercised", exercised);
        if (exercised == 0) {
            problems.add("no HOST_VISIBLE memory type is usable by transfer buffers");
        }
        return {};
    });
}

Outcome MemoryChecks::host_coherent_no_flush(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkDeviceSize size = 64 * 1024;
        const std::uint32_t bits = Kit::buffer_type_bits(rig, size, kTransfer);
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        std::size_t exercised = 0;
        for (std::uint32_t type = 0; type < rig.memory.memoryTypeCount; ++type) {
            if ((rig.memory.memoryTypes[type].propertyFlags & wanted) != wanted || (bits & (1u << type)) == 0) {
                continue;
            }
            const std::string label = type_label(rig, type);
            VulkanBuffer host;
            VulkanBuffer device;
            VulkanBuffer readback;
            if (!Kit::succeeds(problems, label + " allocate and map", host.create_in_type(rig, size, kTransfer, type, true)) ||
                !Kit::succeeds(problems, label + " device buffer", Kit::device_buffer(rig, device, size, kTransfer)) ||
                !Kit::succeeds(problems, label + " readback buffer", Kit::host_buffer(rig, readback, size, kTransfer))) {
                continue;
            }
            ++exercised;
            for (int round = 0; round < 2; ++round) {
                std::vector<std::uint8_t> pattern(size);
                Pattern::fill(0xB000 + type * 4 + static_cast<unsigned>(round), 0, pattern.data(), pattern.size());
                std::memcpy(host.mapped, pattern.data(), size);
                readback.fill_value(0xCD);
                readback.publish();
                if (Kit::succeeds(problems, label + " copy host to device", Kit::copy(rig, host.buffer, device.buffer, size)) &&
                    Kit::succeeds(problems, label + " copy device to readback", Kit::copy(rig, device.buffer, readback.buffer, size))) {
                    readback.acquire();
                    Kit::same_bytes(context, problems, label + " host write seen by the GPU without a flush, round " + std::to_string(round),
                                    pattern.data(), readback.bytes(), size);
                }
            }
            std::memset(host.mapped, 0xCD, size);
            if (Kit::succeeds(problems, label + " GPU fill of host memory", fill_and_publish(rig, host.buffer, size, 0xA5A5A5A5u))) {
                std::vector<std::uint8_t> expected(size, 0xA5);
                Kit::same_bytes(context, problems, label + " GPU fill seen by the host without an invalidate", expected.data(), host.bytes(), size);
            }
            std::vector<std::uint8_t> pattern(size);
            Pattern::fill(0xB800 + type, 0, pattern.data(), pattern.size());
            std::memcpy(readback.mapped, pattern.data(), size);
            readback.publish();
            std::memset(host.mapped, 0xCD, size);
            if (Kit::succeeds(problems, label + " copy readback to device", Kit::copy(rig, readback.buffer, device.buffer, size)) &&
                Kit::succeeds(problems, label + " copy device to host memory", Kit::copy(rig, device.buffer, host.buffer, size))) {
                Kit::same_bytes(context, problems, label + " GPU copy seen by the host without an invalidate", pattern.data(), host.bytes(), size);
            }
        }
        context.measure("vk.coherent_types_exercised", exercised);
        if (exercised == 0) {
            problems.add("the device reports no usable HOST_VISIBLE | HOST_COHERENT memory type, which the specification requires");
        }
        return {};
    });
}

Outcome MemoryChecks::noncoherent_flush_invalidate(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkDeviceSize size = 64 * 1024;
        const VkDeviceSize atom = rig.props.limits.nonCoherentAtomSize;
        const std::uint32_t bits = Kit::buffer_type_bits(rig, size, kTransfer);
        std::size_t exercised = 0;
        for (std::uint32_t type = 0; type < rig.memory.memoryTypeCount; ++type) {
            const VkMemoryPropertyFlags flags = rig.memory.memoryTypes[type].propertyFlags;
            if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 || (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0 ||
                (bits & (1u << type)) == 0) {
                continue;
            }
            const std::string label = type_label(rig, type);
            VulkanBuffer host;
            VulkanBuffer device;
            VulkanBuffer readback;
            if (!Kit::succeeds(problems, label + " allocate and map", host.create_in_type(rig, size, kTransfer, type, true)) ||
                !Kit::succeeds(problems, label + " device buffer", Kit::device_buffer(rig, device, size, kTransfer)) ||
                !Kit::succeeds(problems, label + " readback buffer", Kit::host_buffer(rig, readback, size, kTransfer))) {
                continue;
            }
            ++exercised;
            std::memset(host.mapped, 0xCD, size);
            Kit::succeeds(problems, label + " flush of the initial fill", host.flush());
            struct Piece {
                VkDeviceSize offset;
                VkDeviceSize length;
            };
            const Piece pieces[3] = {{0, atom}, {atom * 4, atom * 2}, {atom * 16, atom * 3}};
            std::vector<std::uint8_t> expected(size, 0xCD);
            std::vector<VkMappedMemoryRange> ranges;
            for (const Piece& piece : pieces) {
                Pattern::fill(0xC000 + piece.offset, 0, host.bytes() + piece.offset, static_cast<std::size_t>(piece.length));
                std::memcpy(expected.data() + piece.offset, host.bytes() + piece.offset, static_cast<std::size_t>(piece.length));
                VkMappedMemoryRange range{};
                range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                range.memory = host.memory;
                range.offset = piece.offset;
                range.size = piece.length;
                ranges.push_back(range);
            }
            Kit::succeeds(problems, label + " flush of three aligned ranges",
                          rig.fn.vkFlushMappedMemoryRanges(rig.device, static_cast<std::uint32_t>(ranges.size()), ranges.data()));
            readback.fill_value(0xEE);
            readback.publish();
            if (Kit::succeeds(problems, label + " copy host to device", Kit::copy(rig, host.buffer, device.buffer, size)) &&
                Kit::succeeds(problems, label + " copy device to readback", Kit::copy(rig, device.buffer, readback.buffer, size))) {
                readback.acquire();
                Kit::same_bytes(context, problems, label + " flushed ranges seen by the GPU", expected.data(), readback.bytes(), size);
            }
            if (Kit::succeeds(problems, label + " GPU fill of host memory", fill_and_publish(rig, host.buffer, size, 0x5A5A5A5Au))) {
                const std::vector<std::uint8_t> filled(size, 0x5A);
                Kit::succeeds(problems, label + " invalidate of three aligned ranges",
                              rig.fn.vkInvalidateMappedMemoryRanges(rig.device, static_cast<std::uint32_t>(ranges.size()), ranges.data()));
                for (const Piece& piece : pieces) {
                    Kit::same_bytes(context, problems, label + " invalidated range at " + std::to_string(piece.offset),
                                    filled.data() + piece.offset, host.bytes() + piece.offset, static_cast<std::size_t>(piece.length));
                }
                Kit::succeeds(problems, label + " invalidate of the whole allocation", host.invalidate());
                Kit::same_bytes(context, problems, label + " whole allocation after the GPU fill", filled.data(), host.bytes(), size);
            }
        }
        context.measure("vk.noncoherent_types_exercised", exercised);
        if (exercised == 0) {
            return "the device reports no non-coherent HOST_VISIBLE memory type usable by transfer buffers";
        }
        return {};
    });
}

Outcome MemoryChecks::device_local_and_budget(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkDeviceSize size = 4 * kMiB;
        const std::uint32_t bits = Kit::buffer_type_bits(rig, size, kTransfer);
        std::size_t local_types = 0;
        for (std::uint32_t type = 0; type < rig.memory.memoryTypeCount; ++type) {
            if ((rig.memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0 || (bits & (1u << type)) == 0) {
                continue;
            }
            ++local_types;
            const std::string label = type_label(rig, type);
            VulkanBuffer local;
            VulkanBuffer host;
            VulkanBuffer readback;
            if (!Kit::succeeds(problems, label + " allocation", local.create_in_type(rig, size, kTransfer, type, false)) ||
                !Kit::succeeds(problems, label + " source buffer", Kit::host_buffer(rig, host, size, kTransfer)) ||
                !Kit::succeeds(problems, label + " readback buffer", Kit::host_buffer(rig, readback, size, kTransfer))) {
                continue;
            }
            std::vector<std::uint8_t> pattern(size);
            Pattern::fill(0xD000 + type, 0, pattern.data(), pattern.size());
            std::memcpy(host.mapped, pattern.data(), size);
            host.publish();
            readback.fill_value(0xCD);
            readback.publish();
            if (Kit::succeeds(problems, label + " upload", Kit::copy(rig, host.buffer, local.buffer, size)) &&
                Kit::succeeds(problems, label + " download", Kit::copy(rig, local.buffer, readback.buffer, size))) {
                readback.acquire();
                Kit::same_bytes(context, problems, label + " roundtrip", pattern.data(), readback.bytes(), size);
            }
        }
        if (local_types == 0) {
            problems.add("no DEVICE_LOCAL memory type is usable by transfer buffers");
        }
        if (!rig.enabled_has("VK_EXT_memory_budget")) {
            context.note("VK_EXT_memory_budget is not advertised; budget rows skipped");
            return {};
        }
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
        budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        VkPhysicalDeviceMemoryProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        props2.pNext = &budget;
        rig.fn.vkGetPhysicalDeviceMemoryProperties2(rig.physical, &props2);
        for (std::uint32_t heap = rig.memory.memoryHeapCount; heap < VK_MAX_MEMORY_HEAPS; ++heap) {
            if (budget.heapBudget[heap] != 0 || budget.heapUsage[heap] != 0) {
                problems.add("heapBudget/heapUsage of the unused heap index " + std::to_string(heap) + " is not zero");
            }
        }
        for (std::uint32_t heap = 0; heap < rig.memory.memoryHeapCount; ++heap) {
            context.measure("vk.heap" + std::to_string(heap) + ".budget", budget.heapBudget[heap]);
            context.measure("vk.heap" + std::to_string(heap) + ".usage", budget.heapUsage[heap]);
            if (budget.heapBudget[heap] == 0) {
                problems.add("heap " + std::to_string(heap) + " reports a budget of 0");
            }
        }
        for (std::uint32_t heap = 0; heap < rig.memory.memoryHeapCount; ++heap) {
            const bool local = (rig.memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            const std::uint64_t chunk = 64 * kMiB;
            std::uint32_t type = 0;
            bool found = false;
            for (; type < rig.memory.memoryTypeCount; ++type) {
                if (rig.memory.memoryTypes[type].heapIndex == heap && (Kit::buffer_type_bits(rig, chunk, kTransfer) & (1u << type)) != 0) {
                    found = true;
                    break;
                }
            }
            const std::uint64_t before = rig.heap_usage(heap);
            if (!found || rig.heap_budget(heap) < before + chunk * 2) {
                context.note("heap " + std::to_string(heap) + " skipped for the usage movement probe (no buffer type or not enough budget)");
                continue;
            }
            std::uint64_t during = 0;
            {
                VulkanBuffer block;
                if (!Kit::succeeds(problems, "allocation of " + VulkanTools::bytes_text(chunk) + " in heap " + std::to_string(heap),
                                   block.create_in_type(rig, chunk, kTransfer, type, false))) {
                    continue;
                }
                const VkResult touched = rig.run([&](VkCommandBuffer commands) { rig.fn.vkCmdFillBuffer(commands, block.buffer, 0, chunk, 0x11111111u); });
                Kit::succeeds(problems, "fill of the allocation in heap " + std::to_string(heap), touched);
                during = rig.heap_usage(heap);
                const std::uint64_t budget_now = rig.heap_budget(heap);
                if (during > budget_now) {
                    problems.add("heap " + std::to_string(heap) + " heapUsage " + std::to_string(during) + " exceeds heapBudget " +
                                 std::to_string(budget_now) + " after a successful allocation");
                }
            }
            const std::uint64_t after = rig.heap_usage(heap);
            const std::string name = std::string(local ? "device-local" : "host") + " heap " + std::to_string(heap);
            context.measure("vk.heap" + std::to_string(heap) + ".usage_delta_alloc", static_cast<std::int64_t>(during) - static_cast<std::int64_t>(before));
            context.measure("vk.heap" + std::to_string(heap) + ".usage_delta_free", static_cast<std::int64_t>(after) - static_cast<std::int64_t>(during));
            if (during < before + chunk / 2) {
                problems.add(name + ": heapUsage moved from " + std::to_string(before) + " to " + std::to_string(during) + " (" +
                             std::to_string(static_cast<std::int64_t>(during) - static_cast<std::int64_t>(before)) + " bytes) after allocating and filling " +
                             std::to_string(chunk) + " bytes; it must rise by about the allocation");
            } else if (after + chunk / 2 > during) {
                problems.add(name + ": heapUsage stays at " + std::to_string(after) + " after freeing " + std::to_string(chunk) +
                             " bytes (it was " + std::to_string(during) + " while allocated)");
            }
        }
        return {};
    });
}

Outcome MemoryChecks::allocation_limits(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const std::uint64_t count_limit = std::min<std::uint64_t>(rig.props.limits.maxMemoryAllocationCount, 4096);
        const std::uint32_t small_bits = Kit::buffer_type_bits(rig, 4096, kTransfer);
        int type = rig.find_type(small_bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        if (type < 0) {
            type = rig.find_type(small_bits, 0, 0);
        }
        if (type < 0) {
            problems.add("no memory type for a small transfer buffer");
            return {};
        }
        {
            std::vector<VkDeviceMemory> blocks;
            VkDeviceMemory block = VK_NULL_HANDLE;
            VkMemoryAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate.allocationSize = 4096;
            allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
            VkResult result = VK_SUCCESS;
            for (std::uint64_t i = 0; i < count_limit; ++i) {
                result = rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &block);
                if (result != VK_SUCCESS) {
                    break;
                }
                blocks.push_back(block);
            }
            context.measure("vk.allocations_made", blocks.size());
            context.measure("vk.maxMemoryAllocationCount", rig.props.limits.maxMemoryAllocationCount);
            if (result != VK_SUCCESS) {
                problems.add("allocation " + std::to_string(blocks.size() + 1) + " of " + std::to_string(count_limit) +
                             " (within maxMemoryAllocationCount) returned " + Kit::name(result));
            }
            for (VkDeviceMemory item : blocks) {
                rig.fn.vkFreeMemory(rig.device, item, nullptr);
            }
        }
        for (std::uint32_t heap = 0; heap < rig.memory.memoryHeapCount; ++heap) {
            std::uint32_t heap_type = 0;
            bool found = false;
            for (; heap_type < rig.memory.memoryTypeCount; ++heap_type) {
                if (rig.memory.memoryTypes[heap_type].heapIndex == heap) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                continue;
            }
            const VkDeviceSize heap_size = rig.memory.memoryHeaps[heap].size;
            const VkDeviceSize cap = rig.maintenance3.maxMemoryAllocationSize != 0 ? rig.maintenance3.maxMemoryAllocationSize : heap_size;
            const VkDeviceSize chunk = std::min({heap_size, cap, 256 * kMiB});
            std::vector<VkDeviceMemory> blocks;
            VkMemoryAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate.allocationSize = chunk;
            allocate.memoryTypeIndex = heap_type;
            VkResult result = VK_SUCCESS;
            VkDeviceSize total = 0;
            const VkDeviceSize ceiling = heap_size + heap_size / 4;
            while (total < ceiling) {
                VkDeviceMemory block = VK_NULL_HANDLE;
                result = rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &block);
                if (result != VK_SUCCESS) {
                    break;
                }
                blocks.push_back(block);
                total += chunk;
            }
            const std::string name = "heap " + std::to_string(heap);
            context.measure("vk.heap" + std::to_string(heap) + ".allocated_before_failure", total);
            if (result != VK_SUCCESS) {
                Kit::expect(problems, "allocation failing in " + name + " after " + VulkanTools::bytes_text(total), result,
                            {VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_TOO_MANY_OBJECTS});
            }
            for (VkDeviceMemory item : blocks) {
                rig.fn.vkFreeMemory(rig.device, item, nullptr);
            }
            blocks.clear();
            VulkanBuffer host;
            VulkanBuffer device;
            VulkanBuffer readback;
            const VkDeviceSize probe = 1 * kMiB;
            if (Kit::succeeds(problems, "host buffer after exhausting " + name, Kit::host_buffer(rig, host, probe, kTransfer)) &&
                Kit::succeeds(problems, "device buffer after exhausting " + name, Kit::device_buffer(rig, device, probe, kTransfer)) &&
                Kit::succeeds(problems, "readback buffer after exhausting " + name, Kit::host_buffer(rig, readback, probe, kTransfer))) {
                std::vector<std::uint8_t> pattern(probe);
                Pattern::fill(0xE000 + heap, 0, pattern.data(), pattern.size());
                std::memcpy(host.mapped, pattern.data(), probe);
                host.publish();
                readback.fill_value(0xCD);
                readback.publish();
                if (Kit::succeeds(problems, "copy after exhausting " + name, Kit::copy(rig, host.buffer, device.buffer, probe)) &&
                    Kit::succeeds(problems, "copy back after exhausting " + name, Kit::copy(rig, device.buffer, readback.buffer, probe))) {
                    readback.acquire();
                    Kit::same_bytes(context, problems, "device after exhausting " + name, pattern.data(), readback.bytes(), probe);
                }
            }
        }
        return {};
    });
}

Outcome MemoryChecks::dedicated_and_aliasing(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkDeviceSize size = 256 * 1024;
        if (rig.fn.vkGetBufferMemoryRequirements2 == nullptr || rig.fn.vkGetImageMemoryRequirements2 == nullptr) {
            problems.add("vkGetBufferMemoryRequirements2 or vkGetImageMemoryRequirements2 does not resolve on a Vulkan 1.1 device");
            return {};
        }
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = kTransfer;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VulkanOwned<VkBuffer> buffer;
        if (!Kit::succeeds(problems, "buffer", rig.make(buffer, rig.fn.vkCreateBuffer, rig.fn.vkDestroyBuffer, buffer_info))) {
            return {};
        }
        VkMemoryDedicatedRequirements dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
        VkMemoryRequirements2 requirements{};
        requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        requirements.pNext = &dedicated;
        VkBufferMemoryRequirementsInfo2 info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
        info.buffer = buffer.get();
        rig.fn.vkGetBufferMemoryRequirements2(rig.device, &info, &requirements);
        VkMemoryRequirements plain{};
        rig.fn.vkGetBufferMemoryRequirements(rig.device, buffer.get(), &plain);
        if (std::memcmp(&plain, &requirements.memoryRequirements, sizeof(plain)) != 0) {
            problems.add("vkGetBufferMemoryRequirements and vkGetBufferMemoryRequirements2 disagree");
        }
        if (dedicated.requiresDedicatedAllocation != VK_FALSE && dedicated.prefersDedicatedAllocation == VK_FALSE) {
            problems.add("buffer requires a dedicated allocation but does not prefer one");
        }
        const int type = rig.find_type(plain.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
        if (type < 0) {
            problems.add("no HOST_VISIBLE | HOST_COHERENT type for a transfer buffer");
            return {};
        }
        VkMemoryDedicatedAllocateInfo dedicated_info{};
        dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated_info.buffer = buffer.get();
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.pNext = &dedicated_info;
        allocate.allocationSize = plain.size;
        allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (Kit::succeeds(problems, "dedicated allocation", rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory))) {
            Kit::succeeds(problems, "bind to a dedicated allocation", rig.fn.vkBindBufferMemory(rig.device, buffer.get(), memory, 0));
            buffer.reset();
            rig.fn.vkFreeMemory(rig.device, memory, nullptr);
        }
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent = {512, 512, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VulkanOwned<VkImage> image;
        if (Kit::succeeds(problems, "image", rig.make(image, rig.fn.vkCreateImage, rig.fn.vkDestroyImage, image_info))) {
            VkMemoryDedicatedRequirements image_dedicated{};
            image_dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
            VkMemoryRequirements2 image_requirements{};
            image_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
            image_requirements.pNext = &image_dedicated;
            VkImageMemoryRequirementsInfo2 image_query{};
            image_query.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
            image_query.image = image.get();
            rig.fn.vkGetImageMemoryRequirements2(rig.device, &image_query, &image_requirements);
            if (image_dedicated.requiresDedicatedAllocation != VK_FALSE && image_dedicated.prefersDedicatedAllocation == VK_FALSE) {
                problems.add("image requires a dedicated allocation but does not prefer one");
            }
            const int image_type = rig.find_type(image_requirements.memoryRequirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (image_type >= 0) {
                VkMemoryDedicatedAllocateInfo image_dedicated_info{};
                image_dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
                image_dedicated_info.image = image.get();
                allocate.pNext = &image_dedicated_info;
                allocate.allocationSize = image_requirements.memoryRequirements.size;
                allocate.memoryTypeIndex = static_cast<std::uint32_t>(image_type);
                if (Kit::succeeds(problems, "dedicated allocation for an image", rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory))) {
                    Kit::succeeds(problems, "bind an image to a dedicated allocation", rig.fn.vkBindImageMemory(rig.device, image.get(), memory, 0));
                    image.reset();
                    rig.fn.vkFreeMemory(rig.device, memory, nullptr);
                }
            }
        }
        VulkanOwned<VkBuffer> first;
        VulkanOwned<VkBuffer> second;
        VulkanBuffer sink;
        if (!Kit::succeeds(problems, "first aliased buffer", rig.make(first, rig.fn.vkCreateBuffer, rig.fn.vkDestroyBuffer, buffer_info)) ||
            !Kit::succeeds(problems, "second aliased buffer", rig.make(second, rig.fn.vkCreateBuffer, rig.fn.vkDestroyBuffer, buffer_info)) ||
            !Kit::succeeds(problems, "sink buffer", Kit::host_buffer(rig, sink, size, kTransfer))) {
            return {};
        }
        VkMemoryRequirements need{};
        rig.fn.vkGetBufferMemoryRequirements(rig.device, first.get(), &need);
        allocate.pNext = nullptr;
        allocate.allocationSize = need.size;
        allocate.memoryTypeIndex = static_cast<std::uint32_t>(rig.find_type(need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0));
        void* mapped = nullptr;
        if (!Kit::succeeds(problems, "allocation shared by two buffers", rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory)) ||
            !Kit::succeeds(problems, "bind of the first buffer", rig.fn.vkBindBufferMemory(rig.device, first.get(), memory, 0)) ||
            !Kit::succeeds(problems, "bind of the second buffer to the same range", rig.fn.vkBindBufferMemory(rig.device, second.get(), memory, 0)) ||
            !Kit::succeeds(problems, "map of the shared allocation", rig.fn.vkMapMemory(rig.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped))) {
            first.reset();
            second.reset();
            if (memory != VK_NULL_HANDLE) {
                rig.fn.vkFreeMemory(rig.device, memory, nullptr);
            }
            return {};
        }
        std::vector<std::uint8_t> pattern(size);
        Pattern::fill(0xF123, 0, pattern.data(), pattern.size());
        std::memcpy(mapped, pattern.data(), size);
        sink.fill_value(0xCD);
        sink.publish();
        if (Kit::succeeds(problems, "copy through the second alias", Kit::copy(rig, second.get(), sink.buffer, size))) {
            sink.acquire();
            Kit::same_bytes(context, problems, "host write seen through an aliasing buffer", pattern.data(), sink.bytes(), size);
        }
        const VkResult filled = rig.run([&](VkCommandBuffer commands) {
            rig.fn.vkCmdFillBuffer(commands, first.get(), 0, size, 0x3C3C3C3Cu);
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0,
                                        nullptr, 0, nullptr);
            const VkBufferCopy region{0, 0, size};
            rig.fn.vkCmdCopyBuffer(commands, second.get(), sink.buffer, 1, &region);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                                        nullptr, 0, nullptr);
        });
        if (Kit::succeeds(problems, "fill through one alias and copy through the other", filled)) {
            sink.acquire();
            const std::vector<std::uint8_t> expected(size, 0x3C);
            Kit::same_bytes(context, problems, "GPU write seen through an aliasing buffer after a barrier", expected.data(), sink.bytes(), size);
        }
        rig.wait_idle();
        rig.fn.vkUnmapMemory(rig.device, memory);
        first.reset();
        second.reset();
        rig.fn.vkFreeMemory(rig.device, memory, nullptr);
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
