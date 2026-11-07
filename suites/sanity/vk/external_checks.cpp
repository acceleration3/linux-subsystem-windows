#include "sanity/vk/external_checks.hpp"

#include "sanity/vk/kit.hpp"

#include <cstring>

namespace lsw::suites::sanity::vk {

namespace {

constexpr std::uint64_t kSecond = 1000000000ull;

VulkanRigOptions external_options() {
    VulkanRigOptions options;
    options.device_extensions = {vkw::kExternalMemoryWin32, vkw::kExternalSemaphoreWin32, vkw::kExternalFenceWin32};
    return options;
}

const struct {
    VkExternalMemoryHandleTypeFlagBits type;
    const char* name;
} kMemoryTypes[] = {{VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, "OPAQUE_WIN32"},
                    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT, "OPAQUE_WIN32_KMT"},
                    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, "D3D11_TEXTURE"},
                    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT, "D3D11_TEXTURE_KMT"},
                    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT, "D3D12_HEAP"},
                    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, "D3D12_RESOURCE"}};

void check_properties(Problems& problems, const std::string& what, VkExternalMemoryHandleTypeFlags type, const VkExternalMemoryProperties& props) {
    if (props.externalMemoryFeatures != 0 && (props.compatibleHandleTypes & type) == 0) {
        problems.add(what + ": compatibleHandleTypes does not contain the queried handle type");
    }
    if ((props.exportFromImportedHandleTypes & ~props.compatibleHandleTypes) != 0) {
        problems.add(what + ": exportFromImportedHandleTypes is not a subset of compatibleHandleTypes");
    }
}

VkResult submit_empty(VulkanRig& rig, VulkanQueue& queue, VkFence fence) {
    return rig.submit(queue, 0, nullptr, fence);
}

}  // namespace

Outcome ExternalChecks::memory_caps_consistent(Context& context) {
    return Kit::each_device(context, external_options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        if (rig.fn.vkGetPhysicalDeviceExternalBufferProperties == nullptr) {
            problems.add("vkGetPhysicalDeviceExternalBufferProperties does not resolve on a Vulkan 1.1 device");
            return {};
        }
        VkExternalMemoryFeatureFlags opaque_features = 0;
        for (const auto& item : kMemoryTypes) {
            const std::string what = std::string("buffer handle type ") + item.name;
            VkPhysicalDeviceExternalBufferInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
            info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            info.handleType = item.type;
            VkExternalBufferProperties props{};
            props.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
            rig.fn.vkGetPhysicalDeviceExternalBufferProperties(rig.physical, &info, &props);
            check_properties(problems, what, item.type, props.externalMemoryProperties);
            context.measure(std::string("vk.external_buffer.") + item.name, props.externalMemoryProperties.externalMemoryFeatures);
            if (item.type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT) {
                opaque_features = props.externalMemoryProperties.externalMemoryFeatures;
            }
            VkPhysicalDeviceExternalImageFormatInfo external{};
            external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
            external.handleType = item.type;
            VkPhysicalDeviceImageFormatInfo2 image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
            image_info.pNext = &external;
            image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            image_info.type = VK_IMAGE_TYPE_2D;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            VkExternalImageFormatProperties external_props{};
            external_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
            VkImageFormatProperties2 image_props{};
            image_props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
            image_props.pNext = &external_props;
            const VkResult result = rig.fn.vkGetPhysicalDeviceImageFormatProperties2(rig.physical, &image_info, &image_props);
            Kit::expect(problems, std::string("image handle type ") + item.name, result, {VK_SUCCESS, VK_ERROR_FORMAT_NOT_SUPPORTED});
            if (result == VK_SUCCESS) {
                check_properties(problems, std::string("image handle type ") + item.name, item.type, external_props.externalMemoryProperties);
            }
        }
        const VkExternalMemoryFeatureFlags both = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
        if ((opaque_features & both) != both || !rig.enabled_has(vkw::kExternalMemoryWin32) || rig.fn.vkGetMemoryWin32HandleKHR == nullptr) {
            context.note("OPAQUE_WIN32 buffer memory is not both exportable and importable here; the export/import roundtrip is skipped");
            return {};
        }
        const VkDeviceSize size = 64 * 1024;
        VkExternalMemoryBufferCreateInfo external_buffer{};
        external_buffer.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
        external_buffer.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.pNext = &external_buffer;
        buffer_info.size = size;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VulkanOwned<VkBuffer> exporter;
        VulkanOwned<VkBuffer> importer;
        VulkanBuffer sink;
        if (!Kit::succeeds(problems, "exportable buffer", rig.make(exporter, rig.fn.vkCreateBuffer, rig.fn.vkDestroyBuffer, buffer_info)) ||
            !Kit::succeeds(problems, "importing buffer", rig.make(importer, rig.fn.vkCreateBuffer, rig.fn.vkDestroyBuffer, buffer_info)) ||
            !Kit::succeeds(problems, "sink", Kit::host_buffer(rig, sink, size))) {
            return {};
        }
        VkMemoryRequirements need{};
        rig.fn.vkGetBufferMemoryRequirements(rig.device, exporter.get(), &need);
        const int type = rig.find_type(need.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type < 0) {
            problems.add("no memory type for the exportable buffer");
            return {};
        }
        VkExportMemoryAllocateInfo export_info{};
        export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        VkMemoryDedicatedAllocateInfo dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.buffer = exporter.get();
        const bool dedicated_only = (opaque_features & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0;
        if (dedicated_only) {
            export_info.pNext = &dedicated;
        }
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.pNext = &export_info;
        allocate.allocationSize = need.size;
        allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
        VkDeviceMemory exported = VK_NULL_HANDLE;
        VkDeviceMemory imported = VK_NULL_HANDLE;
        if (!Kit::succeeds(problems, "exportable allocation", rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &exported)) ||
            !Kit::succeeds(problems, "bind of the exportable buffer", rig.fn.vkBindBufferMemory(rig.device, exporter.get(), exported, 0))) {
            if (exported != VK_NULL_HANDLE) {
                rig.fn.vkFreeMemory(rig.device, exported, nullptr);
            }
            return {};
        }
        const VkResult filled = rig.run([&](VkCommandBuffer cb) { rig.fn.vkCmdFillBuffer(cb, exporter.get(), 0, size, 0x42424242u); });
        HANDLE handle = nullptr;
        const vkw::MemoryGetHandleInfo get{vkw::kMemoryGetWin32Handle, nullptr, exported, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT};
        if (Kit::succeeds(problems, "fill of the exportable buffer", filled) &&
            Kit::succeeds(problems, "vkGetMemoryWin32HandleKHR", rig.fn.vkGetMemoryWin32HandleKHR(rig.device, &get, &handle)) && handle != nullptr) {
            VkMemoryDedicatedAllocateInfo import_dedicated{};
            import_dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            import_dedicated.buffer = importer.get();
            const vkw::ImportMemoryInfo import_win32{vkw::kImportMemoryWin32, dedicated_only ? &import_dedicated : nullptr,
                                                     VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, handle, nullptr};
            VkMemoryAllocateInfo import_allocate{};
            import_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            import_allocate.pNext = &import_win32;
            import_allocate.allocationSize = need.size;
            import_allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
            if (Kit::succeeds(problems, "import of the exported handle", rig.fn.vkAllocateMemory(rig.device, &import_allocate, nullptr, &imported)) &&
                Kit::succeeds(problems, "bind of the importing buffer", rig.fn.vkBindBufferMemory(rig.device, importer.get(), imported, 0))) {
                sink.fill_value(0xCD);
                sink.publish();
                if (Kit::succeeds(problems, "copy out of the imported memory", Kit::copy(rig, importer.get(), sink.buffer, size))) {
                    sink.acquire();
                    const std::vector<std::uint8_t> expected(size, 0x42);
                    Kit::same_bytes(context, problems, "data written through the exporting buffer and read through the imported one", expected.data(), sink.bytes(), size);
                }
            }
            ::CloseHandle(handle);
        }
        rig.wait_idle();
        importer.reset();
        exporter.reset();
        if (imported != VK_NULL_HANDLE) {
            rig.fn.vkFreeMemory(rig.device, imported, nullptr);
        }
        rig.fn.vkFreeMemory(rig.device, exported, nullptr);
        return {};
    });
}

Outcome ExternalChecks::semaphore_fence_caps(Context& context) {
    return Kit::each_device(context, external_options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        if (rig.fn.vkGetPhysicalDeviceExternalSemaphoreProperties == nullptr || rig.fn.vkGetPhysicalDeviceExternalFenceProperties == nullptr) {
            problems.add("the external semaphore or fence capability queries do not resolve on a Vulkan 1.1 device");
            return {};
        }
        VkExternalSemaphoreFeatureFlags semaphore_features = 0;
        VkExternalFenceFeatureFlags fence_features = 0;
        const struct {
            VkExternalSemaphoreHandleTypeFlagBits type;
            const char* name;
        } semaphore_types[] = {{VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT, "OPAQUE_WIN32"},
                               {VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT, "OPAQUE_WIN32_KMT"},
                               {VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT, "D3D12_FENCE"}};
        for (const auto& item : semaphore_types) {
            VkPhysicalDeviceExternalSemaphoreInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
            info.handleType = item.type;
            VkExternalSemaphoreProperties props{};
            props.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
            rig.fn.vkGetPhysicalDeviceExternalSemaphoreProperties(rig.physical, &info, &props);
            if (props.externalSemaphoreFeatures != 0 && (props.compatibleHandleTypes & static_cast<VkFlags>(item.type)) == 0) {
                problems.add(std::string("semaphore handle type ") + item.name + ": compatibleHandleTypes does not contain the queried type");
            }
            if ((props.exportFromImportedHandleTypes & ~props.compatibleHandleTypes) != 0) {
                problems.add(std::string("semaphore handle type ") + item.name + ": exportFromImportedHandleTypes is not a subset of compatibleHandleTypes");
            }
            context.measure(std::string("vk.external_semaphore.") + item.name, props.externalSemaphoreFeatures);
            if (item.type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT) {
                semaphore_features = props.externalSemaphoreFeatures;
            }
        }
        const struct {
            VkExternalFenceHandleTypeFlagBits type;
            const char* name;
        } fence_types[] = {{VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT, "OPAQUE_WIN32"}, {VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT, "OPAQUE_WIN32_KMT"}};
        for (const auto& item : fence_types) {
            VkPhysicalDeviceExternalFenceInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO;
            info.handleType = item.type;
            VkExternalFenceProperties props{};
            props.sType = VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES;
            rig.fn.vkGetPhysicalDeviceExternalFenceProperties(rig.physical, &info, &props);
            if (props.externalFenceFeatures != 0 && (props.compatibleHandleTypes & static_cast<VkFlags>(item.type)) == 0) {
                problems.add(std::string("fence handle type ") + item.name + ": compatibleHandleTypes does not contain the queried type");
            }
            if ((props.exportFromImportedHandleTypes & ~props.compatibleHandleTypes) != 0) {
                problems.add(std::string("fence handle type ") + item.name + ": exportFromImportedHandleTypes is not a subset of compatibleHandleTypes");
            }
            context.measure(std::string("vk.external_fence.") + item.name, props.externalFenceFeatures);
            if (item.type == VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT) {
                fence_features = props.externalFenceFeatures;
            }
        }
        VulkanQueue& queue = rig.main();
        const VkExternalSemaphoreFeatureFlags semaphore_both = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
        if ((semaphore_features & semaphore_both) == semaphore_both && rig.enabled_has(vkw::kExternalSemaphoreWin32) && rig.fn.vkGetSemaphoreWin32HandleKHR != nullptr &&
            rig.fn.vkImportSemaphoreWin32HandleKHR != nullptr) {
            VkExportSemaphoreCreateInfo export_info{};
            export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
            export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            VkSemaphoreCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            info.pNext = &export_info;
            VulkanOwned<VkSemaphore> first;
            VulkanOwned<VkSemaphore> second;
            VulkanOwned<VkFence> fence;
            VulkanBuffer source;
            VulkanBuffer sink;
            if (Kit::succeeds(problems, "exportable semaphore", rig.make(first, rig.fn.vkCreateSemaphore, rig.fn.vkDestroySemaphore, info)) &&
                Kit::succeeds(problems, "second exportable semaphore", rig.make(second, rig.fn.vkCreateSemaphore, rig.fn.vkDestroySemaphore, info)) &&
                Kit::succeeds(problems, "fence", rig.make_fence(fence)) && Kit::succeeds(problems, "source", Kit::host_buffer(rig, source, 4096)) &&
                Kit::succeeds(problems, "sink", Kit::host_buffer(rig, sink, 4096))) {
                HANDLE handle = nullptr;
                const vkw::SemaphoreGetHandleInfo get{vkw::kSemaphoreGetWin32Handle, nullptr, first.get(), VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT};
                if (Kit::succeeds(problems, "vkGetSemaphoreWin32HandleKHR", rig.fn.vkGetSemaphoreWin32HandleKHR(rig.device, &get, &handle)) && handle != nullptr) {
                    const vkw::ImportSemaphoreInfo import_info{vkw::kImportSemaphoreWin32, nullptr, second.get(), 0, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT, handle, nullptr};
                    if (Kit::succeeds(problems, "vkImportSemaphoreWin32HandleKHR", rig.fn.vkImportSemaphoreWin32HandleKHR(rig.device, &import_info))) {
                        source.fill_pattern(0xE5E5);
                        source.publish();
                        sink.fill_value(0xCD);
                        sink.publish();
                        VkResult code = VK_SUCCESS;
                        const VkCommandBuffer copy = Kit::record(rig, queue, [&](VkCommandBuffer cb) {
                            const VkBufferCopy region{0, 0, 4096};
                            rig.fn.vkCmdCopyBuffer(cb, source.buffer, sink.buffer, 1, &region);
                            Kit::transfer_barrier(rig, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
                        }, 0, &code);
                        VkSemaphore signal_handle = first.get();
                        VkSemaphore wait_handle = second.get();
                        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                        VkSubmitInfo waiter{};
                        waiter.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                        waiter.waitSemaphoreCount = 1;
                        waiter.pWaitSemaphores = &wait_handle;
                        waiter.pWaitDstStageMask = &stage;
                        waiter.commandBufferCount = copy != nullptr ? 1 : 0;
                        waiter.pCommandBuffers = &copy;
                        VkSubmitInfo signaler{};
                        signaler.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                        signaler.signalSemaphoreCount = 1;
                        signaler.pSignalSemaphores = &signal_handle;
                        if (copy != nullptr && Kit::succeeds(problems, "submission signaling the exported semaphore", rig.submit(queue, 1, &signaler, VK_NULL_HANDLE)) &&
                            Kit::succeeds(problems, "submission waiting on the imported semaphore", rig.submit(queue, 1, &waiter, fence.get())) &&
                            Kit::succeeds(problems, "wait for work ordered through the shared semaphore", rig.wait_fence(fence.get(), 20 * kSecond))) {
                            sink.acquire();
                            Kit::same_bytes(context, problems, "data copied after the imported semaphore was signaled through the exporter", source.bytes(), sink.bytes(), 4096);
                        }
                    }
                    ::CloseHandle(handle);
                }
            }
            rig.wait_idle();
        } else {
            context.note("OPAQUE_WIN32 semaphores are not both exportable and importable here; the semaphore roundtrip is skipped");
        }
        const VkExternalFenceFeatureFlags fence_both = VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_FENCE_FEATURE_IMPORTABLE_BIT;
        if ((fence_features & fence_both) == fence_both && rig.enabled_has(vkw::kExternalFenceWin32) && rig.fn.vkGetFenceWin32HandleKHR != nullptr &&
            rig.fn.vkImportFenceWin32HandleKHR != nullptr) {
            VkExportFenceCreateInfo export_info{};
            export_info.sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO;
            export_info.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            VkFenceCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            info.pNext = &export_info;
            VulkanOwned<VkFence> first;
            VulkanOwned<VkFence> second;
            if (Kit::succeeds(problems, "exportable fence", rig.make(first, rig.fn.vkCreateFence, rig.fn.vkDestroyFence, info)) &&
                Kit::succeeds(problems, "importing fence", rig.make(second, rig.fn.vkCreateFence, rig.fn.vkDestroyFence, info))) {
                HANDLE handle = nullptr;
                const vkw::FenceGetHandleInfo get{vkw::kFenceGetWin32Handle, nullptr, first.get(), VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT};
                if (Kit::succeeds(problems, "vkGetFenceWin32HandleKHR", rig.fn.vkGetFenceWin32HandleKHR(rig.device, &get, &handle)) && handle != nullptr) {
                    const vkw::ImportFenceInfo import_info{vkw::kImportFenceWin32, nullptr, second.get(), 0, VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT, handle, nullptr};
                    if (Kit::succeeds(problems, "vkImportFenceWin32HandleKHR", rig.fn.vkImportFenceWin32HandleKHR(rig.device, &import_info))) {
                        Kit::expect(problems, "wait on the imported fence before the exporter is signaled", rig.wait_fence(second.get(), 5000000), {VK_TIMEOUT});
                        if (Kit::succeeds(problems, "submission signaling the exported fence", submit_empty(rig, queue, first.get()))) {
                            Kit::succeeds(problems, "wait on the imported fence after the exporter signaled", rig.wait_fence(second.get(), 20 * kSecond));
                        }
                    }
                    ::CloseHandle(handle);
                }
            }
            rig.wait_idle();
        } else {
            context.note("OPAQUE_WIN32 fences are not both exportable and importable here; the fence roundtrip is skipped");
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
