#include "lsw/icd/memory.hpp"

#include "lsw/icd/connection.hpp"
#include "lsw/icd/handle_wrap.hpp"
#include "lsw/icd/memory_shadow.hpp"
#include "vk_icd_entries.h"  // generated::Wrap_vk*

#include <array>
#include <cstdint>
#include <cstring>
#include <vulkan/vulkan_core.h>

namespace lsw::icd::memory {

namespace {

template<typename Handle>
[[nodiscard]] std::uint64_t pack_u64(Handle h) noexcept {
    std::uint64_t out = 0;
    static_assert(sizeof(h) == sizeof(out));
    std::memcpy(&out, &h, sizeof(out));
    return out;
}

// ---- Wrappers around the codegen entries (track allocations) -------------

VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice device,
                                              const VkMemoryAllocateInfo* pAllocateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDeviceMemory* pMemory) {
    const VkResult rc =
        ::lsw::icd::generated::Wrap_vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
    if (rc != VK_SUCCESS || pMemory == nullptr) {
        return rc;
    }
    auto* conn = ::lsw::icd::connection_of(device);
    if (conn != nullptr) {
        conn->memory_shadow().record_allocation(pack_u64(*pMemory), pAllocateInfo->allocationSize);
    }
    return rc;
}

VKAPI_ATTR void VKAPI_CALL FreeMemory(VkDevice device, VkDeviceMemory memory,
                                      const VkAllocationCallbacks* pAllocator) {
    if (memory != VK_NULL_HANDLE) {
        if (auto* conn = ::lsw::icd::connection_of(device); conn != nullptr) {
            conn->memory_shadow().release_allocation(pack_u64(memory));
        }
    }
    ::lsw::icd::generated::Wrap_vkFreeMemory(device, memory, pAllocator);
}

// ---- Hand-written entries (codegen can't lower void**) -------------------

VKAPI_ATTR VkResult VKAPI_CALL MapMemory(VkDevice device, VkDeviceMemory memory,
                                         VkDeviceSize offset, VkDeviceSize size,
                                         VkMemoryMapFlags /*flags*/, void** ppData) {
    auto* conn = ::lsw::icd::connection_of(device);
    if (conn == nullptr) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return conn->memory_shadow().map(pack_u64(memory), offset, size, ppData);
}

VKAPI_ATTR void VKAPI_CALL UnmapMemory(VkDevice device, VkDeviceMemory memory) {
    auto* conn = ::lsw::icd::connection_of(device);
    if (conn == nullptr) {
        return;
    }
    (void)conn->memory_shadow().unmap_and_flush(
        conn->transport(), pack_u64(::lsw::icd::unwrap_to_server_handle(device)), pack_u64(memory));
}

VKAPI_ATTR VkResult VKAPI_CALL FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                                       const VkMappedMemoryRange* pMemoryRanges) {
    auto* conn = ::lsw::icd::connection_of(device);
    if (conn == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    const std::uint64_t srv_device = pack_u64(::lsw::icd::unwrap_to_server_handle(device));
    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        const auto& r = pMemoryRanges[i];
        const VkResult rc = conn->memory_shadow().flush_range(conn->transport(), srv_device,
                                                              pack_u64(r.memory), r.offset, r.size);
        if (rc != VK_SUCCESS) {
            return rc;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL InvalidateMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) {
    auto* conn = ::lsw::icd::connection_of(device);
    if (conn == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    const std::uint64_t srv_device = pack_u64(::lsw::icd::unwrap_to_server_handle(device));
    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        const auto& r = pMemoryRanges[i];
        const VkResult rc = conn->memory_shadow().invalidate_range(
            conn->transport(), srv_device, pack_u64(r.memory), r.offset, r.size);
        if (rc != VK_SUCCESS) {
            return rc;
        }
    }
    return VK_SUCCESS;
}

const std::array<Entry, 6> kEntries = {{
    {"vkAllocateMemory", reinterpret_cast<PFN_vkVoidFunction>(&AllocateMemory)},
    {"vkFreeMemory", reinterpret_cast<PFN_vkVoidFunction>(&FreeMemory)},
    {"vkMapMemory", reinterpret_cast<PFN_vkVoidFunction>(&MapMemory)},
    {"vkUnmapMemory", reinterpret_cast<PFN_vkVoidFunction>(&UnmapMemory)},
    {"vkFlushMappedMemoryRanges", reinterpret_cast<PFN_vkVoidFunction>(&FlushMappedMemoryRanges)},
    {"vkInvalidateMappedMemoryRanges",
     reinterpret_cast<PFN_vkVoidFunction>(&InvalidateMappedMemoryRanges)},
}};

}  // namespace

std::span<const Entry> entries() noexcept {
    return {kEntries.data(), kEntries.size()};
}

}  // namespace lsw::icd::memory
