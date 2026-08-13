// THIS FILE IS GENERATED. DO NOT EDIT.
// Regenerate with: cmake --build build --target regenerate-vulkan
//                  (or: python -m lsw_codegen --xml ... --out generated/)


#include "vk_dispatch_client.h"

#include "lsw/base/log.hpp"
#include "lsw/protocol/buffer.hpp"
#include "vk_protocol_ids.h"
#include "vk_serialize.h"

namespace lsw::vkproto {

void lsw_vkDestroyInstance(::lsw::protocol::Transport& t, VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyInstance(instance={}, pAllocator=ptr)",static_cast<const void*>(instance));
    ::lsw::protocol::Buffer payload;
    serialize_VkInstance(payload, instance);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyInstance, payload.bytes());
    LSW_TRACE("client vkDestroyInstance -> (async, no reply expected)");
}

void lsw_vkDestroyDevice(::lsw::protocol::Transport& t, VkDevice device, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyDevice(device={}, pAllocator=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyDevice, payload.bytes());
    LSW_TRACE("client vkDestroyDevice -> (async, no reply expected)");
}

void lsw_vkGetDeviceQueue(::lsw::protocol::Transport& t, VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) {
    LSW_TRACE("client vkGetDeviceQueue(device={}, queueFamilyIndex={}, queueIndex={}, pQueue=ptr)",static_cast<const void*>(device), static_cast<long long>(queueFamilyIndex), static_cast<long long>(queueIndex));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(queueFamilyIndex);
    payload.write_pod(queueIndex);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceQueue, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!deserialize_VkQueue(r, *pQueue)) {
        return;
    }
    LSW_TRACE("client vkGetDeviceQueue -> result_code={}", result_code);
}

VkResult lsw_vkEnumeratePhysicalDevices(::lsw::protocol::Transport& t, VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    LSW_TRACE("client vkEnumeratePhysicalDevices(instance={}, pPhysicalDeviceCount=ptr, pPhysicalDevices=ptr)",static_cast<const void*>(instance));
    ::lsw::protocol::Buffer payload;
    serialize_VkInstance(payload, instance);
    const std::uint8_t array_present = pPhysicalDevices != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pPhysicalDeviceCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkEnumeratePhysicalDevices, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pPhysicalDeviceCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (array_present) {
        for (std::uint32_t k = 0; k < *pPhysicalDeviceCount; ++k) {
            if (!deserialize_VkPhysicalDevice(r, pPhysicalDevices[k])) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkEnumeratePhysicalDevices -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkGetPhysicalDeviceProperties2(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceProperties2(physicalDevice={}, pProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    payload.write_pod(pProperties->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceProperties2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkPhysicalDeviceProperties2(r, pProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceProperties2 -> result_code={}", result_code);
}

VkResult lsw_vkCreateInstance(::lsw::protocol::Transport& t, const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    LSW_TRACE("client vkCreateInstance(pCreateInfo=ptr, pAllocator=ptr, pInstance=ptr)");
    ::lsw::protocol::Buffer payload;
    serialize_VkInstanceCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateInstance, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkInstance(r, *pInstance)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateInstance -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateDevice(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    LSW_TRACE("client vkCreateDevice(physicalDevice={}, pCreateInfo=ptr, pAllocator=ptr, pDevice=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkDeviceCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateDevice, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkDevice(r, *pDevice)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateDevice -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkGetPhysicalDeviceProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceProperties(physicalDevice={}, pProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkPhysicalDeviceProperties(r, pProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceProperties -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceFeatures(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) {
    LSW_TRACE("client vkGetPhysicalDeviceFeatures(physicalDevice={}, pFeatures=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceFeatures, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkPhysicalDeviceFeatures(r, pFeatures, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceFeatures -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceFormatProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceFormatProperties(physicalDevice={}, format={}, pFormatProperties=ptr)",static_cast<const void*>(physicalDevice), static_cast<long long>(format));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    payload.write_pod(format);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceFormatProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkFormatProperties(r, pFormatProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceFormatProperties -> result_code={}", result_code);
}

VkResult lsw_vkGetPhysicalDeviceImageFormatProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceImageFormatProperties(physicalDevice={}, format={}, type={}, tiling={}, usage={}, flags={}, pImageFormatProperties=ptr)",static_cast<const void*>(physicalDevice), static_cast<long long>(format), static_cast<long long>(type), static_cast<long long>(tiling), static_cast<long long>(usage), static_cast<long long>(flags));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    payload.write_pod(format);
    payload.write_pod(type);
    payload.write_pod(tiling);
    payload.write_pod(usage);
    payload.write_pod(flags);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceImageFormatProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkImageFormatProperties(r, pImageFormatProperties, arena_out)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetPhysicalDeviceImageFormatProperties -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkGetPhysicalDeviceMemoryProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceMemoryProperties(physicalDevice={}, pMemoryProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceMemoryProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkPhysicalDeviceMemoryProperties(r, pMemoryProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceMemoryProperties -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceQueueFamilyProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice={}, pQueueFamilyPropertyCount=ptr, pQueueFamilyProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    const std::uint8_t array_present = pQueueFamilyProperties != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pQueueFamilyPropertyCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceQueueFamilyProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pQueueFamilyPropertyCount)) {
        return;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pQueueFamilyPropertyCount; ++k) {
            if (!deserialize_VkQueueFamilyProperties(r, &pQueueFamilyProperties[k], arena_arr)) {
            return;
            }
        }
    }
    LSW_TRACE("client vkGetPhysicalDeviceQueueFamilyProperties -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceSparseImageFormatProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceSparseImageFormatProperties(physicalDevice={}, format={}, type={}, samples={}, usage={}, tiling={}, pPropertyCount=ptr, pProperties=ptr)",static_cast<const void*>(physicalDevice), static_cast<long long>(format), static_cast<long long>(type), static_cast<long long>(samples), static_cast<long long>(usage), static_cast<long long>(tiling));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    payload.write_pod(format);
    payload.write_pod(type);
    payload.write_pod(samples);
    payload.write_pod(usage);
    payload.write_pod(tiling);
    const std::uint8_t array_present = pProperties != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pPropertyCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceSparseImageFormatProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pPropertyCount)) {
        return;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pPropertyCount; ++k) {
            if (!deserialize_VkSparseImageFormatProperties(r, &pProperties[k], arena_arr)) {
            return;
            }
        }
    }
    LSW_TRACE("client vkGetPhysicalDeviceSparseImageFormatProperties -> result_code={}", result_code);
}

VkResult lsw_vkEnumerateDeviceExtensionProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    LSW_TRACE("client vkEnumerateDeviceExtensionProperties(physicalDevice={}, pLayerName=ptr, pPropertyCount=ptr, pProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    {
        const char* s = pLayerName;
        if (s == nullptr) {
            payload.write_varint(0);
        } else {
            const std::size_t n = std::strlen(s) + 1u;
            payload.write_varint(n);
            payload.write(s, n);
        }
    }
    const std::uint8_t array_present = pProperties != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pPropertyCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkEnumerateDeviceExtensionProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pPropertyCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pPropertyCount; ++k) {
            if (!deserialize_VkExtensionProperties(r, &pProperties[k], arena_arr)) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkEnumerateDeviceExtensionProperties -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkEnumerateDeviceLayerProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    LSW_TRACE("client vkEnumerateDeviceLayerProperties(physicalDevice={}, pPropertyCount=ptr, pProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    const std::uint8_t array_present = pProperties != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pPropertyCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkEnumerateDeviceLayerProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pPropertyCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pPropertyCount; ++k) {
            if (!deserialize_VkLayerProperties(r, &pProperties[k], arena_arr)) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkEnumerateDeviceLayerProperties -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkGetPhysicalDeviceFeatures2(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) {
    LSW_TRACE("client vkGetPhysicalDeviceFeatures2(physicalDevice={}, pFeatures=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    payload.write_pod(pFeatures->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceFeatures2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkPhysicalDeviceFeatures2(r, pFeatures, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceFeatures2 -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceFormatProperties2(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceFormatProperties2(physicalDevice={}, format={}, pFormatProperties=ptr)",static_cast<const void*>(physicalDevice), static_cast<long long>(format));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    payload.write_pod(format);
    payload.write_pod(pFormatProperties->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceFormatProperties2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkFormatProperties2(r, pFormatProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceFormatProperties2 -> result_code={}", result_code);
}

VkResult lsw_vkGetPhysicalDeviceImageFormatProperties2(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceImageFormatProperties2(physicalDevice={}, pImageFormatInfo=ptr, pImageFormatProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkPhysicalDeviceImageFormatInfo2(payload, *pImageFormatInfo);
    payload.write_pod(pImageFormatProperties->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceImageFormatProperties2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkImageFormatProperties2(r, pImageFormatProperties, arena_out)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetPhysicalDeviceImageFormatProperties2 -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkGetPhysicalDeviceMemoryProperties2(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceMemoryProperties2(physicalDevice={}, pMemoryProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    payload.write_pod(pMemoryProperties->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceMemoryProperties2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkPhysicalDeviceMemoryProperties2(r, pMemoryProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceMemoryProperties2 -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceQueueFamilyProperties2(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice={}, pQueueFamilyPropertyCount=ptr, pQueueFamilyProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    const std::uint8_t array_present = pQueueFamilyProperties != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pQueueFamilyPropertyCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceQueueFamilyProperties2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pQueueFamilyPropertyCount)) {
        return;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pQueueFamilyPropertyCount; ++k) {
            if (!deserialize_VkQueueFamilyProperties2(r, &pQueueFamilyProperties[k], arena_arr)) {
            return;
            }
        }
    }
    LSW_TRACE("client vkGetPhysicalDeviceQueueFamilyProperties2 -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceSparseImageFormatProperties2(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceSparseImageFormatProperties2(physicalDevice={}, pFormatInfo=ptr, pPropertyCount=ptr, pProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkPhysicalDeviceSparseImageFormatInfo2(payload, *pFormatInfo);
    const std::uint8_t array_present = pProperties != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pPropertyCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceSparseImageFormatProperties2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pPropertyCount)) {
        return;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pPropertyCount; ++k) {
            if (!deserialize_VkSparseImageFormatProperties2(r, &pProperties[k], arena_arr)) {
            return;
            }
        }
    }
    LSW_TRACE("client vkGetPhysicalDeviceSparseImageFormatProperties2 -> result_code={}", result_code);
}

VkResult lsw_vkDeviceWaitIdle(::lsw::protocol::Transport& t, VkDevice device) {
    LSW_TRACE("client vkDeviceWaitIdle(device={})",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkDeviceWaitIdle, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkDeviceWaitIdle -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkQueueWaitIdle(::lsw::protocol::Transport& t, VkQueue queue) {
    LSW_TRACE("client vkQueueWaitIdle(queue={})",static_cast<const void*>(queue));
    ::lsw::protocol::Buffer payload;
    serialize_VkQueue(payload, queue);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkQueueWaitIdle, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkQueueWaitIdle -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkAllocateMemory(::lsw::protocol::Transport& t, VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
    LSW_TRACE("client vkAllocateMemory(device={}, pAllocateInfo=ptr, pAllocator=ptr, pMemory=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkMemoryAllocateInfo(payload, *pAllocateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkAllocateMemory, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkDeviceMemory(r, *pMemory)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkAllocateMemory -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkFreeMemory(::lsw::protocol::Transport& t, VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkFreeMemory(device={}, memory={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(memory));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDeviceMemory(payload, memory);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkFreeMemory, payload.bytes());
    LSW_TRACE("client vkFreeMemory -> (async, no reply expected)");
}

VkResult lsw_vkBindBufferMemory(::lsw::protocol::Transport& t, VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    LSW_TRACE("client vkBindBufferMemory(device={}, buffer={}, memory={}, memoryOffset={})",static_cast<const void*>(device), static_cast<const void*>(buffer), static_cast<const void*>(memory), static_cast<long long>(memoryOffset));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBuffer(payload, buffer);
    serialize_VkDeviceMemory(payload, memory);
    payload.write_pod(memoryOffset);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkBindBufferMemory, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkBindBufferMemory -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkBindImageMemory(::lsw::protocol::Transport& t, VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    LSW_TRACE("client vkBindImageMemory(device={}, image={}, memory={}, memoryOffset={})",static_cast<const void*>(device), static_cast<const void*>(image), static_cast<const void*>(memory), static_cast<long long>(memoryOffset));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImage(payload, image);
    serialize_VkDeviceMemory(payload, memory);
    payload.write_pod(memoryOffset);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkBindImageMemory, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkBindImageMemory -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkGetBufferMemoryRequirements(::lsw::protocol::Transport& t, VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) {
    LSW_TRACE("client vkGetBufferMemoryRequirements(device={}, buffer={}, pMemoryRequirements=ptr)",static_cast<const void*>(device), static_cast<const void*>(buffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBuffer(payload, buffer);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetBufferMemoryRequirements, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkMemoryRequirements(r, pMemoryRequirements, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetBufferMemoryRequirements -> result_code={}", result_code);
}

void lsw_vkGetImageMemoryRequirements(::lsw::protocol::Transport& t, VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) {
    LSW_TRACE("client vkGetImageMemoryRequirements(device={}, image={}, pMemoryRequirements=ptr)",static_cast<const void*>(device), static_cast<const void*>(image));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImage(payload, image);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetImageMemoryRequirements, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkMemoryRequirements(r, pMemoryRequirements, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetImageMemoryRequirements -> result_code={}", result_code);
}

VkResult lsw_vkCreateBuffer(::lsw::protocol::Transport& t, VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
    LSW_TRACE("client vkCreateBuffer(device={}, pCreateInfo=ptr, pAllocator=ptr, pBuffer=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBufferCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateBuffer, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkBuffer(r, *pBuffer)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateBuffer -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyBuffer(::lsw::protocol::Transport& t, VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyBuffer(device={}, buffer={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(buffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBuffer(payload, buffer);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyBuffer, payload.bytes());
    LSW_TRACE("client vkDestroyBuffer -> (async, no reply expected)");
}

VkResult lsw_vkCreateImage(::lsw::protocol::Transport& t, VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    LSW_TRACE("client vkCreateImage(device={}, pCreateInfo=ptr, pAllocator=ptr, pImage=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImageCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateImage, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkImage(r, *pImage)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateImage -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyImage(::lsw::protocol::Transport& t, VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyImage(device={}, image={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(image));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImage(payload, image);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyImage, payload.bytes());
    LSW_TRACE("client vkDestroyImage -> (async, no reply expected)");
}

VkResult lsw_vkCreateImageView(::lsw::protocol::Transport& t, VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
    LSW_TRACE("client vkCreateImageView(device={}, pCreateInfo=ptr, pAllocator=ptr, pView=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImageViewCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateImageView, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkImageView(r, *pView)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateImageView -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyImageView(::lsw::protocol::Transport& t, VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyImageView(device={}, imageView={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(imageView));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImageView(payload, imageView);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyImageView, payload.bytes());
    LSW_TRACE("client vkDestroyImageView -> (async, no reply expected)");
}

VkResult lsw_vkCreateSampler(::lsw::protocol::Transport& t, VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) {
    LSW_TRACE("client vkCreateSampler(device={}, pCreateInfo=ptr, pAllocator=ptr, pSampler=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSamplerCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateSampler, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkSampler(r, *pSampler)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateSampler -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroySampler(::lsw::protocol::Transport& t, VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroySampler(device={}, sampler={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(sampler));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSampler(payload, sampler);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroySampler, payload.bytes());
    LSW_TRACE("client vkDestroySampler -> (async, no reply expected)");
}

VkResult lsw_vkCreateSemaphore(::lsw::protocol::Transport& t, VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) {
    LSW_TRACE("client vkCreateSemaphore(device={}, pCreateInfo=ptr, pAllocator=ptr, pSemaphore=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSemaphoreCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateSemaphore, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkSemaphore(r, *pSemaphore)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateSemaphore -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroySemaphore(::lsw::protocol::Transport& t, VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroySemaphore(device={}, semaphore={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(semaphore));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSemaphore(payload, semaphore);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroySemaphore, payload.bytes());
    LSW_TRACE("client vkDestroySemaphore -> (async, no reply expected)");
}

VkResult lsw_vkCreateFence(::lsw::protocol::Transport& t, VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) {
    LSW_TRACE("client vkCreateFence(device={}, pCreateInfo=ptr, pAllocator=ptr, pFence=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkFenceCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateFence, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkFence(r, *pFence)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateFence -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyFence(::lsw::protocol::Transport& t, VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyFence(device={}, fence={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(fence));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkFence(payload, fence);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyFence, payload.bytes());
    LSW_TRACE("client vkDestroyFence -> (async, no reply expected)");
}

VkResult lsw_vkResetFences(::lsw::protocol::Transport& t, VkDevice device, uint32_t fenceCount, const VkFence* pFences) {
    LSW_TRACE("client vkResetFences(device={}, fenceCount={}, pFences=ptr)",static_cast<const void*>(device), static_cast<long long>(fenceCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(fenceCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(fenceCount); ++__i) {
        serialize_VkFence(payload, pFences[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkResetFences, payload.bytes());
    LSW_TRACE("client vkResetFences -> (async, no reply expected)");
    return VK_SUCCESS;
}

VkResult lsw_vkWaitForFences(::lsw::protocol::Transport& t, VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) {
    LSW_TRACE("client vkWaitForFences(device={}, fenceCount={}, pFences=ptr, waitAll={}, timeout={})",static_cast<const void*>(device), static_cast<long long>(fenceCount), static_cast<long long>(waitAll), static_cast<long long>(timeout));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(fenceCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(fenceCount); ++__i) {
        serialize_VkFence(payload, pFences[__i]);
    }
    payload.write_pod(waitAll);
    payload.write_pod(timeout);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkWaitForFences, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkWaitForFences -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkGetFenceStatus(::lsw::protocol::Transport& t, VkDevice device, VkFence fence) {
    LSW_TRACE("client vkGetFenceStatus(device={}, fence={})",static_cast<const void*>(device), static_cast<const void*>(fence));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkFence(payload, fence);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetFenceStatus, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetFenceStatus -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateCommandPool(::lsw::protocol::Transport& t, VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) {
    LSW_TRACE("client vkCreateCommandPool(device={}, pCreateInfo=ptr, pAllocator=ptr, pCommandPool=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkCommandPoolCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateCommandPool, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkCommandPool(r, *pCommandPool)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateCommandPool -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyCommandPool(::lsw::protocol::Transport& t, VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyCommandPool(device={}, commandPool={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(commandPool));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkCommandPool(payload, commandPool);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyCommandPool, payload.bytes());
    LSW_TRACE("client vkDestroyCommandPool -> (async, no reply expected)");
}

VkResult lsw_vkResetCommandPool(::lsw::protocol::Transport& t, VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
    LSW_TRACE("client vkResetCommandPool(device={}, commandPool={}, flags={})",static_cast<const void*>(device), static_cast<const void*>(commandPool), static_cast<long long>(flags));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkCommandPool(payload, commandPool);
    payload.write_pod(flags);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkResetCommandPool, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkResetCommandPool -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkBeginCommandBuffer(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) {
    LSW_TRACE("client vkBeginCommandBuffer(commandBuffer={}, pBeginInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkCommandBufferBeginInfo(payload, *pBeginInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkBeginCommandBuffer, payload.bytes());
    LSW_TRACE("client vkBeginCommandBuffer -> (async, no reply expected)");
    return VK_SUCCESS;
}

VkResult lsw_vkEndCommandBuffer(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer) {
    LSW_TRACE("client vkEndCommandBuffer(commandBuffer={})",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    (void)t.send_async(::lsw::vkproto::kCmd_vkEndCommandBuffer, payload.bytes());
    LSW_TRACE("client vkEndCommandBuffer -> (async, no reply expected)");
    return VK_SUCCESS;
}

VkResult lsw_vkResetCommandBuffer(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    LSW_TRACE("client vkResetCommandBuffer(commandBuffer={}, flags={})",static_cast<const void*>(commandBuffer), static_cast<long long>(flags));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(flags);
    (void)t.send_async(::lsw::vkproto::kCmd_vkResetCommandBuffer, payload.bytes());
    LSW_TRACE("client vkResetCommandBuffer -> (async, no reply expected)");
    return VK_SUCCESS;
}

void lsw_vkDestroyShaderModule(::lsw::protocol::Transport& t, VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyShaderModule(device={}, shaderModule={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(shaderModule));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkShaderModule(payload, shaderModule);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyShaderModule, payload.bytes());
    LSW_TRACE("client vkDestroyShaderModule -> (async, no reply expected)");
}

VkResult lsw_vkCreateShaderModule(::lsw::protocol::Transport& t, VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) {
    LSW_TRACE("client vkCreateShaderModule(device={}, pCreateInfo=ptr, pAllocator=ptr, pShaderModule=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkShaderModuleCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateShaderModule, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkShaderModule(r, *pShaderModule)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateShaderModule -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreatePipelineLayout(::lsw::protocol::Transport& t, VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) {
    LSW_TRACE("client vkCreatePipelineLayout(device={}, pCreateInfo=ptr, pAllocator=ptr, pPipelineLayout=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPipelineLayoutCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreatePipelineLayout, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkPipelineLayout(r, *pPipelineLayout)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreatePipelineLayout -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyPipelineLayout(::lsw::protocol::Transport& t, VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyPipelineLayout(device={}, pipelineLayout={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(pipelineLayout));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPipelineLayout(payload, pipelineLayout);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyPipelineLayout, payload.bytes());
    LSW_TRACE("client vkDestroyPipelineLayout -> (async, no reply expected)");
}

void lsw_vkDestroyPipeline(::lsw::protocol::Transport& t, VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyPipeline(device={}, pipeline={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(pipeline));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPipeline(payload, pipeline);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyPipeline, payload.bytes());
    LSW_TRACE("client vkDestroyPipeline -> (async, no reply expected)");
}

VkResult lsw_vkCreatePipelineCache(::lsw::protocol::Transport& t, VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) {
    LSW_TRACE("client vkCreatePipelineCache(device={}, pCreateInfo=ptr, pAllocator=ptr, pPipelineCache=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPipelineCacheCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreatePipelineCache, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkPipelineCache(r, *pPipelineCache)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreatePipelineCache -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyPipelineCache(::lsw::protocol::Transport& t, VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyPipelineCache(device={}, pipelineCache={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(pipelineCache));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPipelineCache(payload, pipelineCache);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyPipelineCache, payload.bytes());
    LSW_TRACE("client vkDestroyPipelineCache -> (async, no reply expected)");
}

VkResult lsw_vkCreateRenderPass(::lsw::protocol::Transport& t, VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
    LSW_TRACE("client vkCreateRenderPass(device={}, pCreateInfo=ptr, pAllocator=ptr, pRenderPass=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkRenderPassCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateRenderPass, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkRenderPass(r, *pRenderPass)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateRenderPass -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyRenderPass(::lsw::protocol::Transport& t, VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyRenderPass(device={}, renderPass={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(renderPass));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkRenderPass(payload, renderPass);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyRenderPass, payload.bytes());
    LSW_TRACE("client vkDestroyRenderPass -> (async, no reply expected)");
}

VkResult lsw_vkCreateFramebuffer(::lsw::protocol::Transport& t, VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) {
    LSW_TRACE("client vkCreateFramebuffer(device={}, pCreateInfo=ptr, pAllocator=ptr, pFramebuffer=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkFramebufferCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateFramebuffer, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkFramebuffer(r, *pFramebuffer)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateFramebuffer -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyFramebuffer(::lsw::protocol::Transport& t, VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyFramebuffer(device={}, framebuffer={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(framebuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkFramebuffer(payload, framebuffer);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyFramebuffer, payload.bytes());
    LSW_TRACE("client vkDestroyFramebuffer -> (async, no reply expected)");
}

VkResult lsw_vkCreateDescriptorSetLayout(::lsw::protocol::Transport& t, VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) {
    LSW_TRACE("client vkCreateDescriptorSetLayout(device={}, pCreateInfo=ptr, pAllocator=ptr, pSetLayout=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorSetLayoutCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateDescriptorSetLayout, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkDescriptorSetLayout(r, *pSetLayout)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateDescriptorSetLayout -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyDescriptorSetLayout(::lsw::protocol::Transport& t, VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyDescriptorSetLayout(device={}, descriptorSetLayout={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(descriptorSetLayout));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorSetLayout(payload, descriptorSetLayout);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyDescriptorSetLayout, payload.bytes());
    LSW_TRACE("client vkDestroyDescriptorSetLayout -> (async, no reply expected)");
}

VkResult lsw_vkCreateDescriptorPool(::lsw::protocol::Transport& t, VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) {
    LSW_TRACE("client vkCreateDescriptorPool(device={}, pCreateInfo=ptr, pAllocator=ptr, pDescriptorPool=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorPoolCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateDescriptorPool, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkDescriptorPool(r, *pDescriptorPool)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateDescriptorPool -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyDescriptorPool(::lsw::protocol::Transport& t, VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyDescriptorPool(device={}, descriptorPool={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(descriptorPool));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorPool(payload, descriptorPool);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyDescriptorPool, payload.bytes());
    LSW_TRACE("client vkDestroyDescriptorPool -> (async, no reply expected)");
}

void lsw_vkFreeCommandBuffers(::lsw::protocol::Transport& t, VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
    LSW_TRACE("client vkFreeCommandBuffers(device={}, commandPool={}, commandBufferCount={}, pCommandBuffers=ptr)",static_cast<const void*>(device), static_cast<const void*>(commandPool), static_cast<long long>(commandBufferCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkCommandPool(payload, commandPool);
    payload.write_pod(commandBufferCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(commandBufferCount); ++__i) {
        serialize_VkCommandBuffer(payload, pCommandBuffers[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkFreeCommandBuffers, payload.bytes());
    LSW_TRACE("client vkFreeCommandBuffers -> (async, no reply expected)");
}

VkResult lsw_vkQueueSubmit(::lsw::protocol::Transport& t, VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
    LSW_TRACE("client vkQueueSubmit(queue={}, submitCount={}, pSubmits=ptr, fence={})",static_cast<const void*>(queue), static_cast<long long>(submitCount), static_cast<const void*>(fence));
    ::lsw::protocol::Buffer payload;
    serialize_VkQueue(payload, queue);
    payload.write_pod(submitCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(submitCount); ++__i) {
        serialize_VkSubmitInfo(payload, pSubmits[__i]);
    }
    serialize_VkFence(payload, fence);
    (void)t.send_async(::lsw::vkproto::kCmd_vkQueueSubmit, payload.bytes());
    LSW_TRACE("client vkQueueSubmit -> (async, no reply expected)");
    return VK_SUCCESS;
}

void lsw_vkCmdBindPipeline(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    LSW_TRACE("client vkCmdBindPipeline(commandBuffer={}, pipelineBindPoint={}, pipeline={})",static_cast<const void*>(commandBuffer), static_cast<long long>(pipelineBindPoint), static_cast<const void*>(pipeline));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(pipelineBindPoint);
    serialize_VkPipeline(payload, pipeline);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBindPipeline, payload.bytes());
    LSW_TRACE("client vkCmdBindPipeline -> (async, no reply expected)");
}

void lsw_vkCmdSetViewport(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) {
    LSW_TRACE("client vkCmdSetViewport(commandBuffer={}, firstViewport={}, viewportCount={}, pViewports=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(firstViewport), static_cast<long long>(viewportCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(firstViewport);
    payload.write_pod(viewportCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(viewportCount); ++__i) {
        serialize_VkViewport(payload, pViewports[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetViewport, payload.bytes());
    LSW_TRACE("client vkCmdSetViewport -> (async, no reply expected)");
}

void lsw_vkCmdSetScissor(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) {
    LSW_TRACE("client vkCmdSetScissor(commandBuffer={}, firstScissor={}, scissorCount={}, pScissors=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(firstScissor), static_cast<long long>(scissorCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(firstScissor);
    payload.write_pod(scissorCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(scissorCount); ++__i) {
        serialize_VkRect2D(payload, pScissors[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetScissor, payload.bytes());
    LSW_TRACE("client vkCmdSetScissor -> (async, no reply expected)");
}

void lsw_vkCmdBindVertexBuffers(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) {
    LSW_TRACE("client vkCmdBindVertexBuffers(commandBuffer={}, firstBinding={}, bindingCount={}, pBuffers=ptr, pOffsets=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(firstBinding), static_cast<long long>(bindingCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(firstBinding);
    payload.write_pod(bindingCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
        serialize_VkBuffer(payload, pBuffers[__i]);
    }
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
        payload.write_pod(pOffsets[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBindVertexBuffers, payload.bytes());
    LSW_TRACE("client vkCmdBindVertexBuffers -> (async, no reply expected)");
}

void lsw_vkCmdBindIndexBuffer(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    LSW_TRACE("client vkCmdBindIndexBuffer(commandBuffer={}, buffer={}, offset={}, indexType={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer), static_cast<long long>(offset), static_cast<long long>(indexType));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, buffer);
    payload.write_pod(offset);
    payload.write_pod(indexType);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBindIndexBuffer, payload.bytes());
    LSW_TRACE("client vkCmdBindIndexBuffer -> (async, no reply expected)");
}

void lsw_vkCmdBindDescriptorSets(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) {
    LSW_TRACE("client vkCmdBindDescriptorSets(commandBuffer={}, pipelineBindPoint={}, layout={}, firstSet={}, descriptorSetCount={}, pDescriptorSets=ptr, dynamicOffsetCount={}, pDynamicOffsets=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(pipelineBindPoint), static_cast<const void*>(layout), static_cast<long long>(firstSet), static_cast<long long>(descriptorSetCount), static_cast<long long>(dynamicOffsetCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(pipelineBindPoint);
    serialize_VkPipelineLayout(payload, layout);
    payload.write_pod(firstSet);
    payload.write_pod(descriptorSetCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(descriptorSetCount); ++__i) {
        serialize_VkDescriptorSet(payload, pDescriptorSets[__i]);
    }
    payload.write_pod(dynamicOffsetCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(dynamicOffsetCount); ++__i) {
        payload.write_pod(pDynamicOffsets[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBindDescriptorSets, payload.bytes());
    LSW_TRACE("client vkCmdBindDescriptorSets -> (async, no reply expected)");
}

void lsw_vkCmdDraw(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    LSW_TRACE("client vkCmdDraw(commandBuffer={}, vertexCount={}, instanceCount={}, firstVertex={}, firstInstance={})",static_cast<const void*>(commandBuffer), static_cast<long long>(vertexCount), static_cast<long long>(instanceCount), static_cast<long long>(firstVertex), static_cast<long long>(firstInstance));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(vertexCount);
    payload.write_pod(instanceCount);
    payload.write_pod(firstVertex);
    payload.write_pod(firstInstance);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDraw, payload.bytes());
    LSW_TRACE("client vkCmdDraw -> (async, no reply expected)");
}

void lsw_vkCmdDrawIndexed(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    LSW_TRACE("client vkCmdDrawIndexed(commandBuffer={}, indexCount={}, instanceCount={}, firstIndex={}, vertexOffset={}, firstInstance={})",static_cast<const void*>(commandBuffer), static_cast<long long>(indexCount), static_cast<long long>(instanceCount), static_cast<long long>(firstIndex), static_cast<long long>(vertexOffset), static_cast<long long>(firstInstance));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(indexCount);
    payload.write_pod(instanceCount);
    payload.write_pod(firstIndex);
    payload.write_pod(vertexOffset);
    payload.write_pod(firstInstance);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDrawIndexed, payload.bytes());
    LSW_TRACE("client vkCmdDrawIndexed -> (async, no reply expected)");
}

void lsw_vkCmdPipelineBarrier(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    LSW_TRACE("client vkCmdPipelineBarrier(commandBuffer={}, srcStageMask={}, dstStageMask={}, dependencyFlags={}, memoryBarrierCount={}, pMemoryBarriers=ptr, bufferMemoryBarrierCount={}, pBufferMemoryBarriers=ptr, imageMemoryBarrierCount={}, pImageMemoryBarriers=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(srcStageMask), static_cast<long long>(dstStageMask), static_cast<long long>(dependencyFlags), static_cast<long long>(memoryBarrierCount), static_cast<long long>(bufferMemoryBarrierCount), static_cast<long long>(imageMemoryBarrierCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(srcStageMask);
    payload.write_pod(dstStageMask);
    payload.write_pod(dependencyFlags);
    payload.write_pod(memoryBarrierCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(memoryBarrierCount); ++__i) {
        serialize_VkMemoryBarrier(payload, pMemoryBarriers[__i]);
    }
    payload.write_pod(bufferMemoryBarrierCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bufferMemoryBarrierCount); ++__i) {
        serialize_VkBufferMemoryBarrier(payload, pBufferMemoryBarriers[__i]);
    }
    payload.write_pod(imageMemoryBarrierCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(imageMemoryBarrierCount); ++__i) {
        serialize_VkImageMemoryBarrier(payload, pImageMemoryBarriers[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdPipelineBarrier, payload.bytes());
    LSW_TRACE("client vkCmdPipelineBarrier -> (async, no reply expected)");
}

void lsw_vkCmdCopyBuffer(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) {
    LSW_TRACE("client vkCmdCopyBuffer(commandBuffer={}, srcBuffer={}, dstBuffer={}, regionCount={}, pRegions=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(srcBuffer), static_cast<const void*>(dstBuffer), static_cast<long long>(regionCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, srcBuffer);
    serialize_VkBuffer(payload, dstBuffer);
    payload.write_pod(regionCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
        serialize_VkBufferCopy(payload, pRegions[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyBuffer, payload.bytes());
    LSW_TRACE("client vkCmdCopyBuffer -> (async, no reply expected)");
}

void lsw_vkCmdBeginRenderPass(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) {
    LSW_TRACE("client vkCmdBeginRenderPass(commandBuffer={}, pRenderPassBegin=ptr, contents={})",static_cast<const void*>(commandBuffer), static_cast<long long>(contents));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkRenderPassBeginInfo(payload, *pRenderPassBegin);
    payload.write_pod(contents);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBeginRenderPass, payload.bytes());
    LSW_TRACE("client vkCmdBeginRenderPass -> (async, no reply expected)");
}

void lsw_vkCmdEndRenderPass(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer) {
    LSW_TRACE("client vkCmdEndRenderPass(commandBuffer={})",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdEndRenderPass, payload.bytes());
    LSW_TRACE("client vkCmdEndRenderPass -> (async, no reply expected)");
}

void lsw_vkCmdNextSubpass(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkSubpassContents contents) {
    LSW_TRACE("client vkCmdNextSubpass(commandBuffer={}, contents={})",static_cast<const void*>(commandBuffer), static_cast<long long>(contents));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(contents);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdNextSubpass, payload.bytes());
    LSW_TRACE("client vkCmdNextSubpass -> (async, no reply expected)");
}

void lsw_vkCmdPushConstants(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) {
    LSW_TRACE("client vkCmdPushConstants(commandBuffer={}, layout={}, stageFlags={}, offset={}, size={}, pValues=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(layout), static_cast<long long>(stageFlags), static_cast<long long>(offset), static_cast<long long>(size));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkPipelineLayout(payload, layout);
    payload.write_pod(stageFlags);
    payload.write_pod(offset);
    payload.write_pod(size);
    if (pValues != nullptr && size > 0) {
        payload.write(pValues, static_cast<std::size_t>(size));
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdPushConstants, payload.bytes());
    LSW_TRACE("client vkCmdPushConstants -> (async, no reply expected)");
}

VkResult lsw_vkGetPhysicalDeviceSurfaceSupportKHR(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) {
    LSW_TRACE("client vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice={}, queueFamilyIndex={}, surface={}, pSupported=ptr)",static_cast<const void*>(physicalDevice), static_cast<long long>(queueFamilyIndex), static_cast<const void*>(surface));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    payload.write_pod(queueFamilyIndex);
    serialize_VkSurfaceKHR(payload, surface);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceSurfaceSupportKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pSupported)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetPhysicalDeviceSurfaceSupportKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {
    LSW_TRACE("client vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice={}, surface={}, pSurfaceCapabilities=ptr)",static_cast<const void*>(physicalDevice), static_cast<const void*>(surface));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkSurfaceKHR(payload, surface);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceSurfaceCapabilitiesKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkSurfaceCapabilitiesKHR(r, pSurfaceCapabilities, arena_out)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetPhysicalDeviceSurfaceCapabilitiesKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkGetPhysicalDeviceSurfaceFormatsKHR(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) {
    LSW_TRACE("client vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice={}, surface={}, pSurfaceFormatCount=ptr, pSurfaceFormats=ptr)",static_cast<const void*>(physicalDevice), static_cast<const void*>(surface));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkSurfaceKHR(payload, surface);
    const std::uint8_t array_present = pSurfaceFormats != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pSurfaceFormatCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceSurfaceFormatsKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pSurfaceFormatCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pSurfaceFormatCount; ++k) {
            if (!deserialize_VkSurfaceFormatKHR(r, &pSurfaceFormats[k], arena_arr)) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkGetPhysicalDeviceSurfaceFormatsKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkGetPhysicalDeviceSurfacePresentModesKHR(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) {
    LSW_TRACE("client vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice={}, surface={}, pPresentModeCount=ptr, pPresentModes=ptr)",static_cast<const void*>(physicalDevice), static_cast<const void*>(surface));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkSurfaceKHR(payload, surface);
    const std::uint8_t array_present = pPresentModes != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pPresentModeCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceSurfacePresentModesKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pPresentModeCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (array_present) {
        for (std::uint32_t k = 0; k < *pPresentModeCount; ++k) {
            if (!r.read_pod(pPresentModes[k])) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkGetPhysicalDeviceSurfacePresentModesKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateSwapchainKHR(::lsw::protocol::Transport& t, VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    LSW_TRACE("client vkCreateSwapchainKHR(device={}, pCreateInfo=ptr, pAllocator=ptr, pSwapchain=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSwapchainCreateInfoKHR(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateSwapchainKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkSwapchainKHR(r, *pSwapchain)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateSwapchainKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroySwapchainKHR(::lsw::protocol::Transport& t, VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroySwapchainKHR(device={}, swapchain={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(swapchain));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSwapchainKHR(payload, swapchain);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroySwapchainKHR, payload.bytes());
    LSW_TRACE("client vkDestroySwapchainKHR -> (async, no reply expected)");
}

VkResult lsw_vkGetSwapchainImagesKHR(::lsw::protocol::Transport& t, VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) {
    LSW_TRACE("client vkGetSwapchainImagesKHR(device={}, swapchain={}, pSwapchainImageCount=ptr, pSwapchainImages=ptr)",static_cast<const void*>(device), static_cast<const void*>(swapchain));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSwapchainKHR(payload, swapchain);
    const std::uint8_t array_present = pSwapchainImages != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pSwapchainImageCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetSwapchainImagesKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pSwapchainImageCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (array_present) {
        for (std::uint32_t k = 0; k < *pSwapchainImageCount; ++k) {
            if (!deserialize_VkImage(r, pSwapchainImages[k])) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkGetSwapchainImagesKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkAcquireNextImageKHR(::lsw::protocol::Transport& t, VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) {
    LSW_TRACE("client vkAcquireNextImageKHR(device={}, swapchain={}, timeout={}, semaphore={}, fence={}, pImageIndex=ptr)",static_cast<const void*>(device), static_cast<const void*>(swapchain), static_cast<long long>(timeout), static_cast<const void*>(semaphore), static_cast<const void*>(fence));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSwapchainKHR(payload, swapchain);
    payload.write_pod(timeout);
    serialize_VkSemaphore(payload, semaphore);
    serialize_VkFence(payload, fence);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkAcquireNextImageKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pImageIndex)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkAcquireNextImageKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkQueuePresentKHR(::lsw::protocol::Transport& t, VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    LSW_TRACE("client vkQueuePresentKHR(queue={}, pPresentInfo=ptr)",static_cast<const void*>(queue));
    ::lsw::protocol::Buffer payload;
    serialize_VkQueue(payload, queue);
    serialize_VkPresentInfoKHR(payload, *pPresentInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkQueuePresentKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkQueuePresentKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateGraphicsPipelines(::lsw::protocol::Transport& t, VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    LSW_TRACE("client vkCreateGraphicsPipelines(device={}, pipelineCache={}, createInfoCount={}, pCreateInfos=ptr, pAllocator=ptr, pPipelines=ptr)",static_cast<const void*>(device), static_cast<const void*>(pipelineCache), static_cast<long long>(createInfoCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPipelineCache(payload, pipelineCache);
    payload.write_pod(createInfoCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(createInfoCount); ++__i) {
        serialize_VkGraphicsPipelineCreateInfo(payload, pCreateInfos[__i]);
    }
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateGraphicsPipelines, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    for (std::uint32_t __k = 0; __k < static_cast<std::uint32_t>(createInfoCount); ++__k) {
        if (!deserialize_VkPipeline(r, pPipelines[__k])) {
        return VK_ERROR_DEVICE_LOST;
        }
    }
    LSW_TRACE("client vkCreateGraphicsPipelines -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkUpdateDescriptorSets(::lsw::protocol::Transport& t, VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) {
    LSW_TRACE("client vkUpdateDescriptorSets(device={}, descriptorWriteCount={}, pDescriptorWrites=ptr, descriptorCopyCount={}, pDescriptorCopies=ptr)",static_cast<const void*>(device), static_cast<long long>(descriptorWriteCount), static_cast<long long>(descriptorCopyCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(descriptorWriteCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(descriptorWriteCount); ++__i) {
        serialize_VkWriteDescriptorSet(payload, pDescriptorWrites[__i]);
    }
    payload.write_pod(descriptorCopyCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(descriptorCopyCount); ++__i) {
        serialize_VkCopyDescriptorSet(payload, pDescriptorCopies[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkUpdateDescriptorSets, payload.bytes());
    LSW_TRACE("client vkUpdateDescriptorSets -> (async, no reply expected)");
}

VkResult lsw_vkAllocateCommandBuffers(::lsw::protocol::Transport& t, VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) {
    LSW_TRACE("client vkAllocateCommandBuffers(device={}, pAllocateInfo=ptr, pCommandBuffers=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkCommandBufferAllocateInfo(payload, *pAllocateInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkAllocateCommandBuffers, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    for (std::uint32_t __k = 0; __k < static_cast<std::uint32_t>(pAllocateInfo->commandBufferCount); ++__k) {
        if (!deserialize_VkCommandBuffer(r, pCommandBuffers[__k])) {
        return VK_ERROR_DEVICE_LOST;
        }
    }
    LSW_TRACE("client vkAllocateCommandBuffers -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkAllocateDescriptorSets(::lsw::protocol::Transport& t, VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) {
    LSW_TRACE("client vkAllocateDescriptorSets(device={}, pAllocateInfo=ptr, pDescriptorSets=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorSetAllocateInfo(payload, *pAllocateInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkAllocateDescriptorSets, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    for (std::uint32_t __k = 0; __k < static_cast<std::uint32_t>(pAllocateInfo->descriptorSetCount); ++__k) {
        if (!deserialize_VkDescriptorSet(r, pDescriptorSets[__k])) {
        return VK_ERROR_DEVICE_LOST;
        }
    }
    LSW_TRACE("client vkAllocateDescriptorSets -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkFreeDescriptorSets(::lsw::protocol::Transport& t, VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) {
    LSW_TRACE("client vkFreeDescriptorSets(device={}, descriptorPool={}, descriptorSetCount={}, pDescriptorSets=ptr)",static_cast<const void*>(device), static_cast<const void*>(descriptorPool), static_cast<long long>(descriptorSetCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorPool(payload, descriptorPool);
    payload.write_pod(descriptorSetCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(descriptorSetCount); ++__i) {
        serialize_VkDescriptorSet(payload, pDescriptorSets[__i]);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkFreeDescriptorSets, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkFreeDescriptorSets -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateComputePipelines(::lsw::protocol::Transport& t, VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    LSW_TRACE("client vkCreateComputePipelines(device={}, pipelineCache={}, createInfoCount={}, pCreateInfos=ptr, pAllocator=ptr, pPipelines=ptr)",static_cast<const void*>(device), static_cast<const void*>(pipelineCache), static_cast<long long>(createInfoCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPipelineCache(payload, pipelineCache);
    payload.write_pod(createInfoCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(createInfoCount); ++__i) {
        serialize_VkComputePipelineCreateInfo(payload, pCreateInfos[__i]);
    }
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateComputePipelines, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    for (std::uint32_t __k = 0; __k < static_cast<std::uint32_t>(createInfoCount); ++__k) {
        if (!deserialize_VkPipeline(r, pPipelines[__k])) {
        return VK_ERROR_DEVICE_LOST;
        }
    }
    LSW_TRACE("client vkCreateComputePipelines -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkResetDescriptorPool(::lsw::protocol::Transport& t, VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) {
    LSW_TRACE("client vkResetDescriptorPool(device={}, descriptorPool={}, flags={})",static_cast<const void*>(device), static_cast<const void*>(descriptorPool), static_cast<long long>(flags));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorPool(payload, descriptorPool);
    payload.write_pod(flags);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkResetDescriptorPool, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkResetDescriptorPool -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateEvent(::lsw::protocol::Transport& t, VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) {
    LSW_TRACE("client vkCreateEvent(device={}, pCreateInfo=ptr, pAllocator=ptr, pEvent=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkEventCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateEvent, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkEvent(r, *pEvent)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateEvent -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyEvent(::lsw::protocol::Transport& t, VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyEvent(device={}, event={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(event));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkEvent(payload, event);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyEvent, payload.bytes());
    LSW_TRACE("client vkDestroyEvent -> (async, no reply expected)");
}

VkResult lsw_vkGetEventStatus(::lsw::protocol::Transport& t, VkDevice device, VkEvent event) {
    LSW_TRACE("client vkGetEventStatus(device={}, event={})",static_cast<const void*>(device), static_cast<const void*>(event));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkEvent(payload, event);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetEventStatus, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetEventStatus -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkSetEvent(::lsw::protocol::Transport& t, VkDevice device, VkEvent event) {
    LSW_TRACE("client vkSetEvent(device={}, event={})",static_cast<const void*>(device), static_cast<const void*>(event));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkEvent(payload, event);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkSetEvent, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkSetEvent -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkResetEvent(::lsw::protocol::Transport& t, VkDevice device, VkEvent event) {
    LSW_TRACE("client vkResetEvent(device={}, event={})",static_cast<const void*>(device), static_cast<const void*>(event));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkEvent(payload, event);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkResetEvent, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkResetEvent -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkCmdSetEvent(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) {
    LSW_TRACE("client vkCmdSetEvent(commandBuffer={}, event={}, stageMask={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(event), static_cast<long long>(stageMask));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkEvent(payload, event);
    payload.write_pod(stageMask);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetEvent, payload.bytes());
    LSW_TRACE("client vkCmdSetEvent -> (async, no reply expected)");
}

void lsw_vkCmdResetEvent(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) {
    LSW_TRACE("client vkCmdResetEvent(commandBuffer={}, event={}, stageMask={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(event), static_cast<long long>(stageMask));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkEvent(payload, event);
    payload.write_pod(stageMask);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdResetEvent, payload.bytes());
    LSW_TRACE("client vkCmdResetEvent -> (async, no reply expected)");
}

void lsw_vkCmdWaitEvents(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    LSW_TRACE("client vkCmdWaitEvents(commandBuffer={}, eventCount={}, pEvents=ptr, srcStageMask={}, dstStageMask={}, memoryBarrierCount={}, pMemoryBarriers=ptr, bufferMemoryBarrierCount={}, pBufferMemoryBarriers=ptr, imageMemoryBarrierCount={}, pImageMemoryBarriers=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(eventCount), static_cast<long long>(srcStageMask), static_cast<long long>(dstStageMask), static_cast<long long>(memoryBarrierCount), static_cast<long long>(bufferMemoryBarrierCount), static_cast<long long>(imageMemoryBarrierCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(eventCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(eventCount); ++__i) {
        serialize_VkEvent(payload, pEvents[__i]);
    }
    payload.write_pod(srcStageMask);
    payload.write_pod(dstStageMask);
    payload.write_pod(memoryBarrierCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(memoryBarrierCount); ++__i) {
        serialize_VkMemoryBarrier(payload, pMemoryBarriers[__i]);
    }
    payload.write_pod(bufferMemoryBarrierCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bufferMemoryBarrierCount); ++__i) {
        serialize_VkBufferMemoryBarrier(payload, pBufferMemoryBarriers[__i]);
    }
    payload.write_pod(imageMemoryBarrierCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(imageMemoryBarrierCount); ++__i) {
        serialize_VkImageMemoryBarrier(payload, pImageMemoryBarriers[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdWaitEvents, payload.bytes());
    LSW_TRACE("client vkCmdWaitEvents -> (async, no reply expected)");
}

void lsw_vkCmdCopyImage(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) {
    LSW_TRACE("client vkCmdCopyImage(commandBuffer={}, srcImage={}, srcImageLayout={}, dstImage={}, dstImageLayout={}, regionCount={}, pRegions=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(srcImage), static_cast<long long>(srcImageLayout), static_cast<const void*>(dstImage), static_cast<long long>(dstImageLayout), static_cast<long long>(regionCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkImage(payload, srcImage);
    payload.write_pod(srcImageLayout);
    serialize_VkImage(payload, dstImage);
    payload.write_pod(dstImageLayout);
    payload.write_pod(regionCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
        serialize_VkImageCopy(payload, pRegions[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyImage, payload.bytes());
    LSW_TRACE("client vkCmdCopyImage -> (async, no reply expected)");
}

void lsw_vkCmdCopyBufferToImage(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    LSW_TRACE("client vkCmdCopyBufferToImage(commandBuffer={}, srcBuffer={}, dstImage={}, dstImageLayout={}, regionCount={}, pRegions=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(srcBuffer), static_cast<const void*>(dstImage), static_cast<long long>(dstImageLayout), static_cast<long long>(regionCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, srcBuffer);
    serialize_VkImage(payload, dstImage);
    payload.write_pod(dstImageLayout);
    payload.write_pod(regionCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
        serialize_VkBufferImageCopy(payload, pRegions[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyBufferToImage, payload.bytes());
    LSW_TRACE("client vkCmdCopyBufferToImage -> (async, no reply expected)");
}

void lsw_vkCmdCopyImageToBuffer(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    LSW_TRACE("client vkCmdCopyImageToBuffer(commandBuffer={}, srcImage={}, srcImageLayout={}, dstBuffer={}, regionCount={}, pRegions=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(srcImage), static_cast<long long>(srcImageLayout), static_cast<const void*>(dstBuffer), static_cast<long long>(regionCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkImage(payload, srcImage);
    payload.write_pod(srcImageLayout);
    serialize_VkBuffer(payload, dstBuffer);
    payload.write_pod(regionCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
        serialize_VkBufferImageCopy(payload, pRegions[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyImageToBuffer, payload.bytes());
    LSW_TRACE("client vkCmdCopyImageToBuffer -> (async, no reply expected)");
}

void lsw_vkCmdBlitImage(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) {
    LSW_TRACE("client vkCmdBlitImage(commandBuffer={}, srcImage={}, srcImageLayout={}, dstImage={}, dstImageLayout={}, regionCount={}, pRegions=ptr, filter={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(srcImage), static_cast<long long>(srcImageLayout), static_cast<const void*>(dstImage), static_cast<long long>(dstImageLayout), static_cast<long long>(regionCount), static_cast<long long>(filter));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkImage(payload, srcImage);
    payload.write_pod(srcImageLayout);
    serialize_VkImage(payload, dstImage);
    payload.write_pod(dstImageLayout);
    payload.write_pod(regionCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
        serialize_VkImageBlit(payload, pRegions[__i]);
    }
    payload.write_pod(filter);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBlitImage, payload.bytes());
    LSW_TRACE("client vkCmdBlitImage -> (async, no reply expected)");
}

void lsw_vkCmdClearColorImage(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    LSW_TRACE("client vkCmdClearColorImage(commandBuffer={}, image={}, imageLayout={}, pColor=ptr, rangeCount={}, pRanges=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(image), static_cast<long long>(imageLayout), static_cast<long long>(rangeCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkImage(payload, image);
    payload.write_pod(imageLayout);
    serialize_VkClearColorValue(payload, *pColor);
    payload.write_pod(rangeCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(rangeCount); ++__i) {
        serialize_VkImageSubresourceRange(payload, pRanges[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdClearColorImage, payload.bytes());
    LSW_TRACE("client vkCmdClearColorImage -> (async, no reply expected)");
}

void lsw_vkCmdClearDepthStencilImage(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    LSW_TRACE("client vkCmdClearDepthStencilImage(commandBuffer={}, image={}, imageLayout={}, pDepthStencil=ptr, rangeCount={}, pRanges=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(image), static_cast<long long>(imageLayout), static_cast<long long>(rangeCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkImage(payload, image);
    payload.write_pod(imageLayout);
    serialize_VkClearDepthStencilValue(payload, *pDepthStencil);
    payload.write_pod(rangeCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(rangeCount); ++__i) {
        serialize_VkImageSubresourceRange(payload, pRanges[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdClearDepthStencilImage, payload.bytes());
    LSW_TRACE("client vkCmdClearDepthStencilImage -> (async, no reply expected)");
}

void lsw_vkCmdClearAttachments(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) {
    LSW_TRACE("client vkCmdClearAttachments(commandBuffer={}, attachmentCount={}, pAttachments=ptr, rectCount={}, pRects=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(attachmentCount), static_cast<long long>(rectCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(attachmentCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(attachmentCount); ++__i) {
        serialize_VkClearAttachment(payload, pAttachments[__i]);
    }
    payload.write_pod(rectCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(rectCount); ++__i) {
        serialize_VkClearRect(payload, pRects[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdClearAttachments, payload.bytes());
    LSW_TRACE("client vkCmdClearAttachments -> (async, no reply expected)");
}

void lsw_vkCmdFillBuffer(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) {
    LSW_TRACE("client vkCmdFillBuffer(commandBuffer={}, dstBuffer={}, dstOffset={}, size={}, data={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(dstBuffer), static_cast<long long>(dstOffset), static_cast<long long>(size), static_cast<long long>(data));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, dstBuffer);
    payload.write_pod(dstOffset);
    payload.write_pod(size);
    payload.write_pod(data);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdFillBuffer, payload.bytes());
    LSW_TRACE("client vkCmdFillBuffer -> (async, no reply expected)");
}

void lsw_vkCmdUpdateBuffer(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) {
    LSW_TRACE("client vkCmdUpdateBuffer(commandBuffer={}, dstBuffer={}, dstOffset={}, dataSize={}, pData=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(dstBuffer), static_cast<long long>(dstOffset), static_cast<long long>(dataSize));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, dstBuffer);
    payload.write_pod(dstOffset);
    payload.write_pod(dataSize);
    if (pData != nullptr && dataSize > 0) {
        payload.write(pData, static_cast<std::size_t>(dataSize));
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdUpdateBuffer, payload.bytes());
    LSW_TRACE("client vkCmdUpdateBuffer -> (async, no reply expected)");
}

void lsw_vkCmdSetLineWidth(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, float lineWidth) {
    LSW_TRACE("client vkCmdSetLineWidth(commandBuffer={}, lineWidth={})",static_cast<const void*>(commandBuffer), lineWidth);
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(lineWidth);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetLineWidth, payload.bytes());
    LSW_TRACE("client vkCmdSetLineWidth -> (async, no reply expected)");
}

void lsw_vkCmdSetDepthBias(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) {
    LSW_TRACE("client vkCmdSetDepthBias(commandBuffer={}, depthBiasConstantFactor={}, depthBiasClamp={}, depthBiasSlopeFactor={})",static_cast<const void*>(commandBuffer), depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(depthBiasConstantFactor);
    payload.write_pod(depthBiasClamp);
    payload.write_pod(depthBiasSlopeFactor);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetDepthBias, payload.bytes());
    LSW_TRACE("client vkCmdSetDepthBias -> (async, no reply expected)");
}

void lsw_vkCmdSetBlendConstants(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const float* blendConstants) {
    LSW_TRACE("client vkCmdSetBlendConstants(commandBuffer={}, blendConstants=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    for (std::uint32_t __i = 0; __i < 4u; ++__i) {
        payload.write_pod(blendConstants[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetBlendConstants, payload.bytes());
    LSW_TRACE("client vkCmdSetBlendConstants -> (async, no reply expected)");
}

void lsw_vkCmdSetDepthBounds(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) {
    LSW_TRACE("client vkCmdSetDepthBounds(commandBuffer={}, minDepthBounds={}, maxDepthBounds={})",static_cast<const void*>(commandBuffer), minDepthBounds, maxDepthBounds);
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(minDepthBounds);
    payload.write_pod(maxDepthBounds);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetDepthBounds, payload.bytes());
    LSW_TRACE("client vkCmdSetDepthBounds -> (async, no reply expected)");
}

void lsw_vkCmdSetStencilCompareMask(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) {
    LSW_TRACE("client vkCmdSetStencilCompareMask(commandBuffer={}, faceMask={}, compareMask={})",static_cast<const void*>(commandBuffer), static_cast<long long>(faceMask), static_cast<long long>(compareMask));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(faceMask);
    payload.write_pod(compareMask);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetStencilCompareMask, payload.bytes());
    LSW_TRACE("client vkCmdSetStencilCompareMask -> (async, no reply expected)");
}

void lsw_vkCmdSetStencilWriteMask(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) {
    LSW_TRACE("client vkCmdSetStencilWriteMask(commandBuffer={}, faceMask={}, writeMask={})",static_cast<const void*>(commandBuffer), static_cast<long long>(faceMask), static_cast<long long>(writeMask));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(faceMask);
    payload.write_pod(writeMask);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetStencilWriteMask, payload.bytes());
    LSW_TRACE("client vkCmdSetStencilWriteMask -> (async, no reply expected)");
}

void lsw_vkCmdSetStencilReference(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) {
    LSW_TRACE("client vkCmdSetStencilReference(commandBuffer={}, faceMask={}, reference={})",static_cast<const void*>(commandBuffer), static_cast<long long>(faceMask), static_cast<long long>(reference));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(faceMask);
    payload.write_pod(reference);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetStencilReference, payload.bytes());
    LSW_TRACE("client vkCmdSetStencilReference -> (async, no reply expected)");
}

void lsw_vkCmdDispatch(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    LSW_TRACE("client vkCmdDispatch(commandBuffer={}, groupCountX={}, groupCountY={}, groupCountZ={})",static_cast<const void*>(commandBuffer), static_cast<long long>(groupCountX), static_cast<long long>(groupCountY), static_cast<long long>(groupCountZ));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(groupCountX);
    payload.write_pod(groupCountY);
    payload.write_pod(groupCountZ);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDispatch, payload.bytes());
    LSW_TRACE("client vkCmdDispatch -> (async, no reply expected)");
}

void lsw_vkCmdDispatchIndirect(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) {
    LSW_TRACE("client vkCmdDispatchIndirect(commandBuffer={}, buffer={}, offset={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer), static_cast<long long>(offset));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, buffer);
    payload.write_pod(offset);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDispatchIndirect, payload.bytes());
    LSW_TRACE("client vkCmdDispatchIndirect -> (async, no reply expected)");
}

void lsw_vkCmdDrawIndirect(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    LSW_TRACE("client vkCmdDrawIndirect(commandBuffer={}, buffer={}, offset={}, drawCount={}, stride={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer), static_cast<long long>(offset), static_cast<long long>(drawCount), static_cast<long long>(stride));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, buffer);
    payload.write_pod(offset);
    payload.write_pod(drawCount);
    payload.write_pod(stride);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDrawIndirect, payload.bytes());
    LSW_TRACE("client vkCmdDrawIndirect -> (async, no reply expected)");
}

void lsw_vkCmdDrawIndexedIndirect(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    LSW_TRACE("client vkCmdDrawIndexedIndirect(commandBuffer={}, buffer={}, offset={}, drawCount={}, stride={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer), static_cast<long long>(offset), static_cast<long long>(drawCount), static_cast<long long>(stride));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, buffer);
    payload.write_pod(offset);
    payload.write_pod(drawCount);
    payload.write_pod(stride);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDrawIndexedIndirect, payload.bytes());
    LSW_TRACE("client vkCmdDrawIndexedIndirect -> (async, no reply expected)");
}

void lsw_vkCmdExecuteCommands(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
    LSW_TRACE("client vkCmdExecuteCommands(commandBuffer={}, commandBufferCount={}, pCommandBuffers=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(commandBufferCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(commandBufferCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(commandBufferCount); ++__i) {
        serialize_VkCommandBuffer(payload, pCommandBuffers[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdExecuteCommands, payload.bytes());
    LSW_TRACE("client vkCmdExecuteCommands -> (async, no reply expected)");
}

VkResult lsw_vkAcquireNextImage2KHR(::lsw::protocol::Transport& t, VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) {
    LSW_TRACE("client vkAcquireNextImage2KHR(device={}, pAcquireInfo=ptr, pImageIndex=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkAcquireNextImageInfoKHR(payload, *pAcquireInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkAcquireNextImage2KHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pImageIndex)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkAcquireNextImage2KHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkBindBufferMemory2(::lsw::protocol::Transport& t, VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    LSW_TRACE("client vkBindBufferMemory2(device={}, bindInfoCount={}, pBindInfos=ptr)",static_cast<const void*>(device), static_cast<long long>(bindInfoCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(bindInfoCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindInfoCount); ++__i) {
        serialize_VkBindBufferMemoryInfo(payload, pBindInfos[__i]);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkBindBufferMemory2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkBindBufferMemory2 -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkBindImageMemory2(::lsw::protocol::Transport& t, VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    LSW_TRACE("client vkBindImageMemory2(device={}, bindInfoCount={}, pBindInfos=ptr)",static_cast<const void*>(device), static_cast<long long>(bindInfoCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(bindInfoCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindInfoCount); ++__i) {
        serialize_VkBindImageMemoryInfo(payload, pBindInfos[__i]);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkBindImageMemory2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkBindImageMemory2 -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkCmdBeginQuery(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) {
    LSW_TRACE("client vkCmdBeginQuery(commandBuffer={}, queryPool={}, query={}, flags={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(queryPool), static_cast<long long>(query), static_cast<long long>(flags));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkQueryPool(payload, queryPool);
    payload.write_pod(query);
    payload.write_pod(flags);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBeginQuery, payload.bytes());
    LSW_TRACE("client vkCmdBeginQuery -> (async, no reply expected)");
}

void lsw_vkCmdBeginRenderPass2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo) {
    LSW_TRACE("client vkCmdBeginRenderPass2(commandBuffer={}, pRenderPassBegin=ptr, pSubpassBeginInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkRenderPassBeginInfo(payload, *pRenderPassBegin);
    serialize_VkSubpassBeginInfo(payload, *pSubpassBeginInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBeginRenderPass2, payload.bytes());
    LSW_TRACE("client vkCmdBeginRenderPass2 -> (async, no reply expected)");
}

void lsw_vkCmdBeginRendering(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo) {
    LSW_TRACE("client vkCmdBeginRendering(commandBuffer={}, pRenderingInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkRenderingInfo(payload, *pRenderingInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBeginRendering, payload.bytes());
    LSW_TRACE("client vkCmdBeginRendering -> (async, no reply expected)");
}

void lsw_vkCmdBindVertexBuffers2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides) {
    LSW_TRACE("client vkCmdBindVertexBuffers2(commandBuffer={}, firstBinding={}, bindingCount={}, pBuffers=ptr, pOffsets=ptr, pSizes=ptr, pStrides=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(firstBinding), static_cast<long long>(bindingCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(firstBinding);
    payload.write_pod(bindingCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
        serialize_VkBuffer(payload, pBuffers[__i]);
    }
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
        payload.write_pod(pOffsets[__i]);
    }
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
        payload.write_pod(pSizes[__i]);
    }
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
        payload.write_pod(pStrides[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBindVertexBuffers2, payload.bytes());
    LSW_TRACE("client vkCmdBindVertexBuffers2 -> (async, no reply expected)");
}

void lsw_vkCmdBlitImage2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo) {
    LSW_TRACE("client vkCmdBlitImage2(commandBuffer={}, pBlitImageInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBlitImageInfo2(payload, *pBlitImageInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdBlitImage2, payload.bytes());
    LSW_TRACE("client vkCmdBlitImage2 -> (async, no reply expected)");
}

void lsw_vkCmdCopyBuffer2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo) {
    LSW_TRACE("client vkCmdCopyBuffer2(commandBuffer={}, pCopyBufferInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkCopyBufferInfo2(payload, *pCopyBufferInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyBuffer2, payload.bytes());
    LSW_TRACE("client vkCmdCopyBuffer2 -> (async, no reply expected)");
}

void lsw_vkCmdCopyBufferToImage2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    LSW_TRACE("client vkCmdCopyBufferToImage2(commandBuffer={}, pCopyBufferToImageInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkCopyBufferToImageInfo2(payload, *pCopyBufferToImageInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyBufferToImage2, payload.bytes());
    LSW_TRACE("client vkCmdCopyBufferToImage2 -> (async, no reply expected)");
}

void lsw_vkCmdCopyImage2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo) {
    LSW_TRACE("client vkCmdCopyImage2(commandBuffer={}, pCopyImageInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkCopyImageInfo2(payload, *pCopyImageInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyImage2, payload.bytes());
    LSW_TRACE("client vkCmdCopyImage2 -> (async, no reply expected)");
}

void lsw_vkCmdCopyImageToBuffer2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) {
    LSW_TRACE("client vkCmdCopyImageToBuffer2(commandBuffer={}, pCopyImageToBufferInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkCopyImageToBufferInfo2(payload, *pCopyImageToBufferInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyImageToBuffer2, payload.bytes());
    LSW_TRACE("client vkCmdCopyImageToBuffer2 -> (async, no reply expected)");
}

void lsw_vkCmdCopyQueryPoolResults(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) {
    LSW_TRACE("client vkCmdCopyQueryPoolResults(commandBuffer={}, queryPool={}, firstQuery={}, queryCount={}, dstBuffer={}, dstOffset={}, stride={}, flags={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(queryPool), static_cast<long long>(firstQuery), static_cast<long long>(queryCount), static_cast<const void*>(dstBuffer), static_cast<long long>(dstOffset), static_cast<long long>(stride), static_cast<long long>(flags));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkQueryPool(payload, queryPool);
    payload.write_pod(firstQuery);
    payload.write_pod(queryCount);
    serialize_VkBuffer(payload, dstBuffer);
    payload.write_pod(dstOffset);
    payload.write_pod(stride);
    payload.write_pod(flags);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdCopyQueryPoolResults, payload.bytes());
    LSW_TRACE("client vkCmdCopyQueryPoolResults -> (async, no reply expected)");
}

void lsw_vkCmdDispatchBase(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    LSW_TRACE("client vkCmdDispatchBase(commandBuffer={}, baseGroupX={}, baseGroupY={}, baseGroupZ={}, groupCountX={}, groupCountY={}, groupCountZ={})",static_cast<const void*>(commandBuffer), static_cast<long long>(baseGroupX), static_cast<long long>(baseGroupY), static_cast<long long>(baseGroupZ), static_cast<long long>(groupCountX), static_cast<long long>(groupCountY), static_cast<long long>(groupCountZ));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(baseGroupX);
    payload.write_pod(baseGroupY);
    payload.write_pod(baseGroupZ);
    payload.write_pod(groupCountX);
    payload.write_pod(groupCountY);
    payload.write_pod(groupCountZ);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDispatchBase, payload.bytes());
    LSW_TRACE("client vkCmdDispatchBase -> (async, no reply expected)");
}

void lsw_vkCmdDrawIndexedIndirectCount(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    LSW_TRACE("client vkCmdDrawIndexedIndirectCount(commandBuffer={}, buffer={}, offset={}, countBuffer={}, countBufferOffset={}, maxDrawCount={}, stride={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer), static_cast<long long>(offset), static_cast<const void*>(countBuffer), static_cast<long long>(countBufferOffset), static_cast<long long>(maxDrawCount), static_cast<long long>(stride));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, buffer);
    payload.write_pod(offset);
    serialize_VkBuffer(payload, countBuffer);
    payload.write_pod(countBufferOffset);
    payload.write_pod(maxDrawCount);
    payload.write_pod(stride);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDrawIndexedIndirectCount, payload.bytes());
    LSW_TRACE("client vkCmdDrawIndexedIndirectCount -> (async, no reply expected)");
}

void lsw_vkCmdDrawIndirectCount(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    LSW_TRACE("client vkCmdDrawIndirectCount(commandBuffer={}, buffer={}, offset={}, countBuffer={}, countBufferOffset={}, maxDrawCount={}, stride={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer), static_cast<long long>(offset), static_cast<const void*>(countBuffer), static_cast<long long>(countBufferOffset), static_cast<long long>(maxDrawCount), static_cast<long long>(stride));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkBuffer(payload, buffer);
    payload.write_pod(offset);
    serialize_VkBuffer(payload, countBuffer);
    payload.write_pod(countBufferOffset);
    payload.write_pod(maxDrawCount);
    payload.write_pod(stride);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdDrawIndirectCount, payload.bytes());
    LSW_TRACE("client vkCmdDrawIndirectCount -> (async, no reply expected)");
}

void lsw_vkCmdEndQuery(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) {
    LSW_TRACE("client vkCmdEndQuery(commandBuffer={}, queryPool={}, query={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(queryPool), static_cast<long long>(query));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkQueryPool(payload, queryPool);
    payload.write_pod(query);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdEndQuery, payload.bytes());
    LSW_TRACE("client vkCmdEndQuery -> (async, no reply expected)");
}

void lsw_vkCmdEndRenderPass2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo) {
    LSW_TRACE("client vkCmdEndRenderPass2(commandBuffer={}, pSubpassEndInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkSubpassEndInfo(payload, *pSubpassEndInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdEndRenderPass2, payload.bytes());
    LSW_TRACE("client vkCmdEndRenderPass2 -> (async, no reply expected)");
}

void lsw_vkCmdEndRendering(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer) {
    LSW_TRACE("client vkCmdEndRendering(commandBuffer={})",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdEndRendering, payload.bytes());
    LSW_TRACE("client vkCmdEndRendering -> (async, no reply expected)");
}

void lsw_vkCmdNextSubpass2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo) {
    LSW_TRACE("client vkCmdNextSubpass2(commandBuffer={}, pSubpassBeginInfo=ptr, pSubpassEndInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkSubpassBeginInfo(payload, *pSubpassBeginInfo);
    serialize_VkSubpassEndInfo(payload, *pSubpassEndInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdNextSubpass2, payload.bytes());
    LSW_TRACE("client vkCmdNextSubpass2 -> (async, no reply expected)");
}

void lsw_vkCmdPipelineBarrier2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo) {
    LSW_TRACE("client vkCmdPipelineBarrier2(commandBuffer={}, pDependencyInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkDependencyInfo(payload, *pDependencyInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdPipelineBarrier2, payload.bytes());
    LSW_TRACE("client vkCmdPipelineBarrier2 -> (async, no reply expected)");
}

void lsw_vkCmdResetEvent2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask) {
    LSW_TRACE("client vkCmdResetEvent2(commandBuffer={}, event={}, stageMask={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(event), static_cast<long long>(stageMask));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkEvent(payload, event);
    payload.write_pod(stageMask);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdResetEvent2, payload.bytes());
    LSW_TRACE("client vkCmdResetEvent2 -> (async, no reply expected)");
}

void lsw_vkCmdResetQueryPool(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
    LSW_TRACE("client vkCmdResetQueryPool(commandBuffer={}, queryPool={}, firstQuery={}, queryCount={})",static_cast<const void*>(commandBuffer), static_cast<const void*>(queryPool), static_cast<long long>(firstQuery), static_cast<long long>(queryCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkQueryPool(payload, queryPool);
    payload.write_pod(firstQuery);
    payload.write_pod(queryCount);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdResetQueryPool, payload.bytes());
    LSW_TRACE("client vkCmdResetQueryPool -> (async, no reply expected)");
}

void lsw_vkCmdResolveImage(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) {
    LSW_TRACE("client vkCmdResolveImage(commandBuffer={}, srcImage={}, srcImageLayout={}, dstImage={}, dstImageLayout={}, regionCount={}, pRegions=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(srcImage), static_cast<long long>(srcImageLayout), static_cast<const void*>(dstImage), static_cast<long long>(dstImageLayout), static_cast<long long>(regionCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkImage(payload, srcImage);
    payload.write_pod(srcImageLayout);
    serialize_VkImage(payload, dstImage);
    payload.write_pod(dstImageLayout);
    payload.write_pod(regionCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
        serialize_VkImageResolve(payload, pRegions[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdResolveImage, payload.bytes());
    LSW_TRACE("client vkCmdResolveImage -> (async, no reply expected)");
}

void lsw_vkCmdResolveImage2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo) {
    LSW_TRACE("client vkCmdResolveImage2(commandBuffer={}, pResolveImageInfo=ptr)",static_cast<const void*>(commandBuffer));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkResolveImageInfo2(payload, *pResolveImageInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdResolveImage2, payload.bytes());
    LSW_TRACE("client vkCmdResolveImage2 -> (async, no reply expected)");
}

void lsw_vkCmdSetCullMode(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkCullModeFlags cullMode) {
    LSW_TRACE("client vkCmdSetCullMode(commandBuffer={}, cullMode={})",static_cast<const void*>(commandBuffer), static_cast<long long>(cullMode));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(cullMode);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetCullMode, payload.bytes());
    LSW_TRACE("client vkCmdSetCullMode -> (async, no reply expected)");
}

void lsw_vkCmdSetDepthBiasEnable(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable) {
    LSW_TRACE("client vkCmdSetDepthBiasEnable(commandBuffer={}, depthBiasEnable={})",static_cast<const void*>(commandBuffer), static_cast<long long>(depthBiasEnable));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(depthBiasEnable);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetDepthBiasEnable, payload.bytes());
    LSW_TRACE("client vkCmdSetDepthBiasEnable -> (async, no reply expected)");
}

void lsw_vkCmdSetDepthBoundsTestEnable(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable) {
    LSW_TRACE("client vkCmdSetDepthBoundsTestEnable(commandBuffer={}, depthBoundsTestEnable={})",static_cast<const void*>(commandBuffer), static_cast<long long>(depthBoundsTestEnable));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(depthBoundsTestEnable);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetDepthBoundsTestEnable, payload.bytes());
    LSW_TRACE("client vkCmdSetDepthBoundsTestEnable -> (async, no reply expected)");
}

void lsw_vkCmdSetDepthCompareOp(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp) {
    LSW_TRACE("client vkCmdSetDepthCompareOp(commandBuffer={}, depthCompareOp={})",static_cast<const void*>(commandBuffer), static_cast<long long>(depthCompareOp));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(depthCompareOp);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetDepthCompareOp, payload.bytes());
    LSW_TRACE("client vkCmdSetDepthCompareOp -> (async, no reply expected)");
}

void lsw_vkCmdSetDepthTestEnable(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBool32 depthTestEnable) {
    LSW_TRACE("client vkCmdSetDepthTestEnable(commandBuffer={}, depthTestEnable={})",static_cast<const void*>(commandBuffer), static_cast<long long>(depthTestEnable));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(depthTestEnable);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetDepthTestEnable, payload.bytes());
    LSW_TRACE("client vkCmdSetDepthTestEnable -> (async, no reply expected)");
}

void lsw_vkCmdSetDepthWriteEnable(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable) {
    LSW_TRACE("client vkCmdSetDepthWriteEnable(commandBuffer={}, depthWriteEnable={})",static_cast<const void*>(commandBuffer), static_cast<long long>(depthWriteEnable));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(depthWriteEnable);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetDepthWriteEnable, payload.bytes());
    LSW_TRACE("client vkCmdSetDepthWriteEnable -> (async, no reply expected)");
}

void lsw_vkCmdSetDeviceMask(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t deviceMask) {
    LSW_TRACE("client vkCmdSetDeviceMask(commandBuffer={}, deviceMask={})",static_cast<const void*>(commandBuffer), static_cast<long long>(deviceMask));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(deviceMask);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetDeviceMask, payload.bytes());
    LSW_TRACE("client vkCmdSetDeviceMask -> (async, no reply expected)");
}

void lsw_vkCmdSetEvent2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo) {
    LSW_TRACE("client vkCmdSetEvent2(commandBuffer={}, event={}, pDependencyInfo=ptr)",static_cast<const void*>(commandBuffer), static_cast<const void*>(event));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    serialize_VkEvent(payload, event);
    serialize_VkDependencyInfo(payload, *pDependencyInfo);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetEvent2, payload.bytes());
    LSW_TRACE("client vkCmdSetEvent2 -> (async, no reply expected)");
}

void lsw_vkCmdSetFrontFace(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkFrontFace frontFace) {
    LSW_TRACE("client vkCmdSetFrontFace(commandBuffer={}, frontFace={})",static_cast<const void*>(commandBuffer), static_cast<long long>(frontFace));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(frontFace);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetFrontFace, payload.bytes());
    LSW_TRACE("client vkCmdSetFrontFace -> (async, no reply expected)");
}

void lsw_vkCmdSetPrimitiveRestartEnable(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable) {
    LSW_TRACE("client vkCmdSetPrimitiveRestartEnable(commandBuffer={}, primitiveRestartEnable={})",static_cast<const void*>(commandBuffer), static_cast<long long>(primitiveRestartEnable));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(primitiveRestartEnable);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetPrimitiveRestartEnable, payload.bytes());
    LSW_TRACE("client vkCmdSetPrimitiveRestartEnable -> (async, no reply expected)");
}

void lsw_vkCmdSetPrimitiveTopology(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology) {
    LSW_TRACE("client vkCmdSetPrimitiveTopology(commandBuffer={}, primitiveTopology={})",static_cast<const void*>(commandBuffer), static_cast<long long>(primitiveTopology));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(primitiveTopology);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetPrimitiveTopology, payload.bytes());
    LSW_TRACE("client vkCmdSetPrimitiveTopology -> (async, no reply expected)");
}

void lsw_vkCmdSetRasterizerDiscardEnable(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable) {
    LSW_TRACE("client vkCmdSetRasterizerDiscardEnable(commandBuffer={}, rasterizerDiscardEnable={})",static_cast<const void*>(commandBuffer), static_cast<long long>(rasterizerDiscardEnable));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(rasterizerDiscardEnable);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetRasterizerDiscardEnable, payload.bytes());
    LSW_TRACE("client vkCmdSetRasterizerDiscardEnable -> (async, no reply expected)");
}

void lsw_vkCmdSetScissorWithCount(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D* pScissors) {
    LSW_TRACE("client vkCmdSetScissorWithCount(commandBuffer={}, scissorCount={}, pScissors=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(scissorCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(scissorCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(scissorCount); ++__i) {
        serialize_VkRect2D(payload, pScissors[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetScissorWithCount, payload.bytes());
    LSW_TRACE("client vkCmdSetScissorWithCount -> (async, no reply expected)");
}

void lsw_vkCmdSetStencilOp(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp) {
    LSW_TRACE("client vkCmdSetStencilOp(commandBuffer={}, faceMask={}, failOp={}, passOp={}, depthFailOp={}, compareOp={})",static_cast<const void*>(commandBuffer), static_cast<long long>(faceMask), static_cast<long long>(failOp), static_cast<long long>(passOp), static_cast<long long>(depthFailOp), static_cast<long long>(compareOp));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(faceMask);
    payload.write_pod(failOp);
    payload.write_pod(passOp);
    payload.write_pod(depthFailOp);
    payload.write_pod(compareOp);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetStencilOp, payload.bytes());
    LSW_TRACE("client vkCmdSetStencilOp -> (async, no reply expected)");
}

void lsw_vkCmdSetStencilTestEnable(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable) {
    LSW_TRACE("client vkCmdSetStencilTestEnable(commandBuffer={}, stencilTestEnable={})",static_cast<const void*>(commandBuffer), static_cast<long long>(stencilTestEnable));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(stencilTestEnable);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetStencilTestEnable, payload.bytes());
    LSW_TRACE("client vkCmdSetStencilTestEnable -> (async, no reply expected)");
}

void lsw_vkCmdSetViewportWithCount(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport* pViewports) {
    LSW_TRACE("client vkCmdSetViewportWithCount(commandBuffer={}, viewportCount={}, pViewports=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(viewportCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(viewportCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(viewportCount); ++__i) {
        serialize_VkViewport(payload, pViewports[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdSetViewportWithCount, payload.bytes());
    LSW_TRACE("client vkCmdSetViewportWithCount -> (async, no reply expected)");
}

void lsw_vkCmdWaitEvents2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos) {
    LSW_TRACE("client vkCmdWaitEvents2(commandBuffer={}, eventCount={}, pEvents=ptr, pDependencyInfos=ptr)",static_cast<const void*>(commandBuffer), static_cast<long long>(eventCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(eventCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(eventCount); ++__i) {
        serialize_VkEvent(payload, pEvents[__i]);
    }
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(eventCount); ++__i) {
        serialize_VkDependencyInfo(payload, pDependencyInfos[__i]);
    }
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdWaitEvents2, payload.bytes());
    LSW_TRACE("client vkCmdWaitEvents2 -> (async, no reply expected)");
}

void lsw_vkCmdWriteTimestamp(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) {
    LSW_TRACE("client vkCmdWriteTimestamp(commandBuffer={}, pipelineStage={}, queryPool={}, query={})",static_cast<const void*>(commandBuffer), static_cast<long long>(pipelineStage), static_cast<const void*>(queryPool), static_cast<long long>(query));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(pipelineStage);
    serialize_VkQueryPool(payload, queryPool);
    payload.write_pod(query);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdWriteTimestamp, payload.bytes());
    LSW_TRACE("client vkCmdWriteTimestamp -> (async, no reply expected)");
}

void lsw_vkCmdWriteTimestamp2(::lsw::protocol::Transport& t, VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query) {
    LSW_TRACE("client vkCmdWriteTimestamp2(commandBuffer={}, stage={}, queryPool={}, query={})",static_cast<const void*>(commandBuffer), static_cast<long long>(stage), static_cast<const void*>(queryPool), static_cast<long long>(query));
    ::lsw::protocol::Buffer payload;
    serialize_VkCommandBuffer(payload, commandBuffer);
    payload.write_pod(stage);
    serialize_VkQueryPool(payload, queryPool);
    payload.write_pod(query);
    (void)t.send_async(::lsw::vkproto::kCmd_vkCmdWriteTimestamp2, payload.bytes());
    LSW_TRACE("client vkCmdWriteTimestamp2 -> (async, no reply expected)");
}

VkResult lsw_vkCreateBufferView(::lsw::protocol::Transport& t, VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) {
    LSW_TRACE("client vkCreateBufferView(device={}, pCreateInfo=ptr, pAllocator=ptr, pView=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBufferViewCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateBufferView, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkBufferView(r, *pView)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateBufferView -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateDescriptorUpdateTemplate(::lsw::protocol::Transport& t, VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) {
    LSW_TRACE("client vkCreateDescriptorUpdateTemplate(device={}, pCreateInfo=ptr, pAllocator=ptr, pDescriptorUpdateTemplate=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorUpdateTemplateCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateDescriptorUpdateTemplate, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkDescriptorUpdateTemplate(r, *pDescriptorUpdateTemplate)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateDescriptorUpdateTemplate -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreatePrivateDataSlot(::lsw::protocol::Transport& t, VkDevice device, const VkPrivateDataSlotCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPrivateDataSlot* pPrivateDataSlot) {
    LSW_TRACE("client vkCreatePrivateDataSlot(device={}, pCreateInfo=ptr, pAllocator=ptr, pPrivateDataSlot=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPrivateDataSlotCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreatePrivateDataSlot, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkPrivateDataSlot(r, *pPrivateDataSlot)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreatePrivateDataSlot -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateQueryPool(::lsw::protocol::Transport& t, VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) {
    LSW_TRACE("client vkCreateQueryPool(device={}, pCreateInfo=ptr, pAllocator=ptr, pQueryPool=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkQueryPoolCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateQueryPool, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkQueryPool(r, *pQueryPool)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateQueryPool -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateRenderPass2(::lsw::protocol::Transport& t, VkDevice device, const VkRenderPassCreateInfo2* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
    LSW_TRACE("client vkCreateRenderPass2(device={}, pCreateInfo=ptr, pAllocator=ptr, pRenderPass=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkRenderPassCreateInfo2(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateRenderPass2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkRenderPass(r, *pRenderPass)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateRenderPass2 -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkCreateSamplerYcbcrConversion(::lsw::protocol::Transport& t, VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) {
    LSW_TRACE("client vkCreateSamplerYcbcrConversion(device={}, pCreateInfo=ptr, pAllocator=ptr, pYcbcrConversion=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSamplerYcbcrConversionCreateInfo(payload, *pCreateInfo);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkCreateSamplerYcbcrConversion, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!deserialize_VkSamplerYcbcrConversion(r, *pYcbcrConversion)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkCreateSamplerYcbcrConversion -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkDestroyBufferView(::lsw::protocol::Transport& t, VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyBufferView(device={}, bufferView={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(bufferView));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBufferView(payload, bufferView);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyBufferView, payload.bytes());
    LSW_TRACE("client vkDestroyBufferView -> (async, no reply expected)");
}

void lsw_vkDestroyDescriptorUpdateTemplate(::lsw::protocol::Transport& t, VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyDescriptorUpdateTemplate(device={}, descriptorUpdateTemplate={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(descriptorUpdateTemplate));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorUpdateTemplate(payload, descriptorUpdateTemplate);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyDescriptorUpdateTemplate, payload.bytes());
    LSW_TRACE("client vkDestroyDescriptorUpdateTemplate -> (async, no reply expected)");
}

void lsw_vkDestroyPrivateDataSlot(::lsw::protocol::Transport& t, VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyPrivateDataSlot(device={}, privateDataSlot={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(privateDataSlot));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPrivateDataSlot(payload, privateDataSlot);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyPrivateDataSlot, payload.bytes());
    LSW_TRACE("client vkDestroyPrivateDataSlot -> (async, no reply expected)");
}

void lsw_vkDestroyQueryPool(::lsw::protocol::Transport& t, VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroyQueryPool(device={}, queryPool={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(queryPool));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkQueryPool(payload, queryPool);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroyQueryPool, payload.bytes());
    LSW_TRACE("client vkDestroyQueryPool -> (async, no reply expected)");
}

void lsw_vkDestroySamplerYcbcrConversion(::lsw::protocol::Transport& t, VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) {
    LSW_TRACE("client vkDestroySamplerYcbcrConversion(device={}, ycbcrConversion={}, pAllocator=ptr)",static_cast<const void*>(device), static_cast<const void*>(ycbcrConversion));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSamplerYcbcrConversion(payload, ycbcrConversion);
    (void)pAllocator;  // VkAllocationCallbacks not crossed over the wire
    (void)t.send_async(::lsw::vkproto::kCmd_vkDestroySamplerYcbcrConversion, payload.bytes());
    LSW_TRACE("client vkDestroySamplerYcbcrConversion -> (async, no reply expected)");
}

VkResult lsw_vkEnumeratePhysicalDeviceGroups(::lsw::protocol::Transport& t, VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) {
    LSW_TRACE("client vkEnumeratePhysicalDeviceGroups(instance={}, pPhysicalDeviceGroupCount=ptr, pPhysicalDeviceGroupProperties=ptr)",static_cast<const void*>(instance));
    ::lsw::protocol::Buffer payload;
    serialize_VkInstance(payload, instance);
    const std::uint8_t array_present = pPhysicalDeviceGroupProperties != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pPhysicalDeviceGroupCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkEnumeratePhysicalDeviceGroups, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pPhysicalDeviceGroupCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pPhysicalDeviceGroupCount; ++k) {
            if (!deserialize_VkPhysicalDeviceGroupProperties(r, &pPhysicalDeviceGroupProperties[k], arena_arr)) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkEnumeratePhysicalDeviceGroups -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkFlushMappedMemoryRanges(::lsw::protocol::Transport& t, VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) {
    LSW_TRACE("client vkFlushMappedMemoryRanges(device={}, memoryRangeCount={}, pMemoryRanges=ptr)",static_cast<const void*>(device), static_cast<long long>(memoryRangeCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(memoryRangeCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(memoryRangeCount); ++__i) {
        serialize_VkMappedMemoryRange(payload, pMemoryRanges[__i]);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkFlushMappedMemoryRanges, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkFlushMappedMemoryRanges -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkDeviceAddress lsw_vkGetBufferDeviceAddress(::lsw::protocol::Transport& t, VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    LSW_TRACE("client vkGetBufferDeviceAddress(device={}, pInfo=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBufferDeviceAddressInfo(payload, *pInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetBufferDeviceAddress, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetBufferDeviceAddress -> result_code={}", result_code);
    return static_cast<VkDeviceAddress>(result_code);
}

void lsw_vkGetBufferMemoryRequirements2(::lsw::protocol::Transport& t, VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    LSW_TRACE("client vkGetBufferMemoryRequirements2(device={}, pInfo=ptr, pMemoryRequirements=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBufferMemoryRequirementsInfo2(payload, *pInfo);
    payload.write_pod(pMemoryRequirements->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetBufferMemoryRequirements2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkMemoryRequirements2(r, pMemoryRequirements, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetBufferMemoryRequirements2 -> result_code={}", result_code);
}

uint64_t lsw_vkGetBufferOpaqueCaptureAddress(::lsw::protocol::Transport& t, VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    LSW_TRACE("client vkGetBufferOpaqueCaptureAddress(device={}, pInfo=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkBufferDeviceAddressInfo(payload, *pInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetBufferOpaqueCaptureAddress, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetBufferOpaqueCaptureAddress -> result_code={}", result_code);
    return static_cast<uint64_t>(result_code);
}

void lsw_vkGetDescriptorSetLayoutSupport(::lsw::protocol::Transport& t, VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) {
    LSW_TRACE("client vkGetDescriptorSetLayoutSupport(device={}, pCreateInfo=ptr, pSupport=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDescriptorSetLayoutCreateInfo(payload, *pCreateInfo);
    payload.write_pod(pSupport->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDescriptorSetLayoutSupport, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkDescriptorSetLayoutSupport(r, pSupport, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetDescriptorSetLayoutSupport -> result_code={}", result_code);
}

void lsw_vkGetDeviceBufferMemoryRequirements(::lsw::protocol::Transport& t, VkDevice device, const VkDeviceBufferMemoryRequirements* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    LSW_TRACE("client vkGetDeviceBufferMemoryRequirements(device={}, pInfo=ptr, pMemoryRequirements=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDeviceBufferMemoryRequirements(payload, *pInfo);
    payload.write_pod(pMemoryRequirements->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceBufferMemoryRequirements, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkMemoryRequirements2(r, pMemoryRequirements, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetDeviceBufferMemoryRequirements -> result_code={}", result_code);
}

void lsw_vkGetDeviceGroupPeerMemoryFeatures(::lsw::protocol::Transport& t, VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) {
    LSW_TRACE("client vkGetDeviceGroupPeerMemoryFeatures(device={}, heapIndex={}, localDeviceIndex={}, remoteDeviceIndex={}, pPeerMemoryFeatures=ptr)",static_cast<const void*>(device), static_cast<long long>(heapIndex), static_cast<long long>(localDeviceIndex), static_cast<long long>(remoteDeviceIndex));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(heapIndex);
    payload.write_pod(localDeviceIndex);
    payload.write_pod(remoteDeviceIndex);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceGroupPeerMemoryFeatures, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pPeerMemoryFeatures)) {
        return;
    }
    LSW_TRACE("client vkGetDeviceGroupPeerMemoryFeatures -> result_code={}", result_code);
}

VkResult lsw_vkGetDeviceGroupPresentCapabilitiesKHR(::lsw::protocol::Transport& t, VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) {
    LSW_TRACE("client vkGetDeviceGroupPresentCapabilitiesKHR(device={}, pDeviceGroupPresentCapabilities=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(pDeviceGroupPresentCapabilities->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceGroupPresentCapabilitiesKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkDeviceGroupPresentCapabilitiesKHR(r, pDeviceGroupPresentCapabilities, arena_out)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetDeviceGroupPresentCapabilitiesKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkGetDeviceGroupSurfacePresentModesKHR(::lsw::protocol::Transport& t, VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) {
    LSW_TRACE("client vkGetDeviceGroupSurfacePresentModesKHR(device={}, surface={}, pModes=ptr)",static_cast<const void*>(device), static_cast<const void*>(surface));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSurfaceKHR(payload, surface);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceGroupSurfacePresentModesKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pModes)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetDeviceGroupSurfacePresentModesKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkGetDeviceImageMemoryRequirements(::lsw::protocol::Transport& t, VkDevice device, const VkDeviceImageMemoryRequirements* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    LSW_TRACE("client vkGetDeviceImageMemoryRequirements(device={}, pInfo=ptr, pMemoryRequirements=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDeviceImageMemoryRequirements(payload, *pInfo);
    payload.write_pod(pMemoryRequirements->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceImageMemoryRequirements, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkMemoryRequirements2(r, pMemoryRequirements, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetDeviceImageMemoryRequirements -> result_code={}", result_code);
}

void lsw_vkGetDeviceImageSparseMemoryRequirements(::lsw::protocol::Transport& t, VkDevice device, const VkDeviceImageMemoryRequirements* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    LSW_TRACE("client vkGetDeviceImageSparseMemoryRequirements(device={}, pInfo=ptr, pSparseMemoryRequirementCount=ptr, pSparseMemoryRequirements=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDeviceImageMemoryRequirements(payload, *pInfo);
    const std::uint8_t array_present = pSparseMemoryRequirements != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pSparseMemoryRequirementCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceImageSparseMemoryRequirements, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pSparseMemoryRequirementCount)) {
        return;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pSparseMemoryRequirementCount; ++k) {
            if (!deserialize_VkSparseImageMemoryRequirements2(r, &pSparseMemoryRequirements[k], arena_arr)) {
            return;
            }
        }
    }
    LSW_TRACE("client vkGetDeviceImageSparseMemoryRequirements -> result_code={}", result_code);
}

void lsw_vkGetDeviceMemoryCommitment(::lsw::protocol::Transport& t, VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) {
    LSW_TRACE("client vkGetDeviceMemoryCommitment(device={}, memory={}, pCommittedMemoryInBytes=ptr)",static_cast<const void*>(device), static_cast<const void*>(memory));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDeviceMemory(payload, memory);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceMemoryCommitment, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pCommittedMemoryInBytes)) {
        return;
    }
    LSW_TRACE("client vkGetDeviceMemoryCommitment -> result_code={}", result_code);
}

uint64_t lsw_vkGetDeviceMemoryOpaqueCaptureAddress(::lsw::protocol::Transport& t, VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) {
    LSW_TRACE("client vkGetDeviceMemoryOpaqueCaptureAddress(device={}, pInfo=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDeviceMemoryOpaqueCaptureAddressInfo(payload, *pInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceMemoryOpaqueCaptureAddress, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetDeviceMemoryOpaqueCaptureAddress -> result_code={}", result_code);
    return static_cast<uint64_t>(result_code);
}

void lsw_vkGetDeviceQueue2(::lsw::protocol::Transport& t, VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) {
    LSW_TRACE("client vkGetDeviceQueue2(device={}, pQueueInfo=ptr, pQueue=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDeviceQueueInfo2(payload, *pQueueInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetDeviceQueue2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!deserialize_VkQueue(r, *pQueue)) {
        return;
    }
    LSW_TRACE("client vkGetDeviceQueue2 -> result_code={}", result_code);
}

void lsw_vkGetImageMemoryRequirements2(::lsw::protocol::Transport& t, VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    LSW_TRACE("client vkGetImageMemoryRequirements2(device={}, pInfo=ptr, pMemoryRequirements=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImageMemoryRequirementsInfo2(payload, *pInfo);
    payload.write_pod(pMemoryRequirements->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetImageMemoryRequirements2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkMemoryRequirements2(r, pMemoryRequirements, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetImageMemoryRequirements2 -> result_code={}", result_code);
}

void lsw_vkGetImageSparseMemoryRequirements(::lsw::protocol::Transport& t, VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) {
    LSW_TRACE("client vkGetImageSparseMemoryRequirements(device={}, image={}, pSparseMemoryRequirementCount=ptr, pSparseMemoryRequirements=ptr)",static_cast<const void*>(device), static_cast<const void*>(image));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImage(payload, image);
    const std::uint8_t array_present = pSparseMemoryRequirements != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pSparseMemoryRequirementCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetImageSparseMemoryRequirements, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pSparseMemoryRequirementCount)) {
        return;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pSparseMemoryRequirementCount; ++k) {
            if (!deserialize_VkSparseImageMemoryRequirements(r, &pSparseMemoryRequirements[k], arena_arr)) {
            return;
            }
        }
    }
    LSW_TRACE("client vkGetImageSparseMemoryRequirements -> result_code={}", result_code);
}

void lsw_vkGetImageSparseMemoryRequirements2(::lsw::protocol::Transport& t, VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    LSW_TRACE("client vkGetImageSparseMemoryRequirements2(device={}, pInfo=ptr, pSparseMemoryRequirementCount=ptr, pSparseMemoryRequirements=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImageSparseMemoryRequirementsInfo2(payload, *pInfo);
    const std::uint8_t array_present = pSparseMemoryRequirements != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pSparseMemoryRequirementCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetImageSparseMemoryRequirements2, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pSparseMemoryRequirementCount)) {
        return;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pSparseMemoryRequirementCount; ++k) {
            if (!deserialize_VkSparseImageMemoryRequirements2(r, &pSparseMemoryRequirements[k], arena_arr)) {
            return;
            }
        }
    }
    LSW_TRACE("client vkGetImageSparseMemoryRequirements2 -> result_code={}", result_code);
}

void lsw_vkGetImageSubresourceLayout(::lsw::protocol::Transport& t, VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) {
    LSW_TRACE("client vkGetImageSubresourceLayout(device={}, image={}, pSubresource=ptr, pLayout=ptr)",static_cast<const void*>(device), static_cast<const void*>(image));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkImage(payload, image);
    serialize_VkImageSubresource(payload, *pSubresource);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetImageSubresourceLayout, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkSubresourceLayout(r, pLayout, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetImageSubresourceLayout -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceExternalBufferProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceExternalBufferProperties(physicalDevice={}, pExternalBufferInfo=ptr, pExternalBufferProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkPhysicalDeviceExternalBufferInfo(payload, *pExternalBufferInfo);
    payload.write_pod(pExternalBufferProperties->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceExternalBufferProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkExternalBufferProperties(r, pExternalBufferProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceExternalBufferProperties -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceExternalFenceProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceExternalFenceProperties(physicalDevice={}, pExternalFenceInfo=ptr, pExternalFenceProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkPhysicalDeviceExternalFenceInfo(payload, *pExternalFenceInfo);
    payload.write_pod(pExternalFenceProperties->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceExternalFenceProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkExternalFenceProperties(r, pExternalFenceProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceExternalFenceProperties -> result_code={}", result_code);
}

void lsw_vkGetPhysicalDeviceExternalSemaphoreProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceExternalSemaphoreProperties(physicalDevice={}, pExternalSemaphoreInfo=ptr, pExternalSemaphoreProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkPhysicalDeviceExternalSemaphoreInfo(payload, *pExternalSemaphoreInfo);
    payload.write_pod(pExternalSemaphoreProperties->sType);
    payload.write_varint(0);  // pNext: empty chain (Phase 1)
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceExternalSemaphoreProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkExternalSemaphoreProperties(r, pExternalSemaphoreProperties, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetPhysicalDeviceExternalSemaphoreProperties -> result_code={}", result_code);
}

VkResult lsw_vkGetPhysicalDevicePresentRectanglesKHR(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) {
    LSW_TRACE("client vkGetPhysicalDevicePresentRectanglesKHR(physicalDevice={}, surface={}, pRectCount=ptr, pRects=ptr)",static_cast<const void*>(physicalDevice), static_cast<const void*>(surface));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    serialize_VkSurfaceKHR(payload, surface);
    const std::uint8_t array_present = pRects != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pRectCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDevicePresentRectanglesKHR, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pRectCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pRectCount; ++k) {
            if (!deserialize_VkRect2D(r, &pRects[k], arena_arr)) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkGetPhysicalDevicePresentRectanglesKHR -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkGetPhysicalDeviceToolProperties(::lsw::protocol::Transport& t, VkPhysicalDevice physicalDevice, uint32_t* pToolCount, VkPhysicalDeviceToolProperties* pToolProperties) {
    LSW_TRACE("client vkGetPhysicalDeviceToolProperties(physicalDevice={}, pToolCount=ptr, pToolProperties=ptr)",static_cast<const void*>(physicalDevice));
    ::lsw::protocol::Buffer payload;
    serialize_VkPhysicalDevice(payload, physicalDevice);
    const std::uint8_t array_present = pToolProperties != nullptr ? 1 : 0;
    payload.write_pod(array_present);
    if (array_present) {
        payload.write_pod<std::uint32_t>(*pToolCount);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPhysicalDeviceToolProperties, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pToolCount)) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::Arena arena_arr;
    if (array_present) {
        for (std::uint32_t k = 0; k < *pToolCount; ++k) {
            if (!deserialize_VkPhysicalDeviceToolProperties(r, &pToolProperties[k], arena_arr)) {
            return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    LSW_TRACE("client vkGetPhysicalDeviceToolProperties -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkGetPrivateData(::lsw::protocol::Transport& t, VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t* pData) {
    LSW_TRACE("client vkGetPrivateData(device={}, objectType={}, objectHandle={}, privateDataSlot={}, pData=ptr)",static_cast<const void*>(device), static_cast<long long>(objectType), static_cast<long long>(objectHandle), static_cast<const void*>(privateDataSlot));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(objectType);
    payload.write_pod(objectHandle);
    serialize_VkPrivateDataSlot(payload, privateDataSlot);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetPrivateData, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    if (!r.read_pod(*pData)) {
        return;
    }
    LSW_TRACE("client vkGetPrivateData -> result_code={}", result_code);
}

void lsw_vkGetRenderAreaGranularity(::lsw::protocol::Transport& t, VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) {
    LSW_TRACE("client vkGetRenderAreaGranularity(device={}, renderPass={}, pGranularity=ptr)",static_cast<const void*>(device), static_cast<const void*>(renderPass));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkRenderPass(payload, renderPass);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetRenderAreaGranularity, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return;
    }
    ::lsw::protocol::Arena arena_out;
    if (!deserialize_VkExtent2D(r, pGranularity, arena_out)) {
        return;
    }
    LSW_TRACE("client vkGetRenderAreaGranularity -> result_code={}", result_code);
}

VkResult lsw_vkGetSemaphoreCounterValue(::lsw::protocol::Transport& t, VkDevice device, VkSemaphore semaphore, uint64_t* pValue) {
    LSW_TRACE("client vkGetSemaphoreCounterValue(device={}, semaphore={}, pValue=ptr)",static_cast<const void*>(device), static_cast<const void*>(semaphore));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSemaphore(payload, semaphore);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkGetSemaphoreCounterValue, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (!r.read_pod(*pValue)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkGetSemaphoreCounterValue -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkInvalidateMappedMemoryRanges(::lsw::protocol::Transport& t, VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) {
    LSW_TRACE("client vkInvalidateMappedMemoryRanges(device={}, memoryRangeCount={}, pMemoryRanges=ptr)",static_cast<const void*>(device), static_cast<long long>(memoryRangeCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(memoryRangeCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(memoryRangeCount); ++__i) {
        serialize_VkMappedMemoryRange(payload, pMemoryRanges[__i]);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkInvalidateMappedMemoryRanges, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkInvalidateMappedMemoryRanges -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkMergePipelineCaches(::lsw::protocol::Transport& t, VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) {
    LSW_TRACE("client vkMergePipelineCaches(device={}, dstCache={}, srcCacheCount={}, pSrcCaches=ptr)",static_cast<const void*>(device), static_cast<const void*>(dstCache), static_cast<long long>(srcCacheCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkPipelineCache(payload, dstCache);
    payload.write_pod(srcCacheCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(srcCacheCount); ++__i) {
        serialize_VkPipelineCache(payload, pSrcCaches[__i]);
    }
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkMergePipelineCaches, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkMergePipelineCaches -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkQueueBindSparse(::lsw::protocol::Transport& t, VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) {
    LSW_TRACE("client vkQueueBindSparse(queue={}, bindInfoCount={}, pBindInfo=ptr, fence={})",static_cast<const void*>(queue), static_cast<long long>(bindInfoCount), static_cast<const void*>(fence));
    ::lsw::protocol::Buffer payload;
    serialize_VkQueue(payload, queue);
    payload.write_pod(bindInfoCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindInfoCount); ++__i) {
        serialize_VkBindSparseInfo(payload, pBindInfo[__i]);
    }
    serialize_VkFence(payload, fence);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkQueueBindSparse, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkQueueBindSparse -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkQueueSubmit2(::lsw::protocol::Transport& t, VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence) {
    LSW_TRACE("client vkQueueSubmit2(queue={}, submitCount={}, pSubmits=ptr, fence={})",static_cast<const void*>(queue), static_cast<long long>(submitCount), static_cast<const void*>(fence));
    ::lsw::protocol::Buffer payload;
    serialize_VkQueue(payload, queue);
    payload.write_pod(submitCount);
    for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(submitCount); ++__i) {
        serialize_VkSubmitInfo2(payload, pSubmits[__i]);
    }
    serialize_VkFence(payload, fence);
    (void)t.send_async(::lsw::vkproto::kCmd_vkQueueSubmit2, payload.bytes());
    LSW_TRACE("client vkQueueSubmit2 -> (async, no reply expected)");
    return VK_SUCCESS;
}

void lsw_vkResetQueryPool(::lsw::protocol::Transport& t, VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
    LSW_TRACE("client vkResetQueryPool(device={}, queryPool={}, firstQuery={}, queryCount={})",static_cast<const void*>(device), static_cast<const void*>(queryPool), static_cast<long long>(firstQuery), static_cast<long long>(queryCount));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkQueryPool(payload, queryPool);
    payload.write_pod(firstQuery);
    payload.write_pod(queryCount);
    (void)t.send_async(::lsw::vkproto::kCmd_vkResetQueryPool, payload.bytes());
    LSW_TRACE("client vkResetQueryPool -> (async, no reply expected)");
}

VkResult lsw_vkSetPrivateData(::lsw::protocol::Transport& t, VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data) {
    LSW_TRACE("client vkSetPrivateData(device={}, objectType={}, objectHandle={}, privateDataSlot={}, data={})",static_cast<const void*>(device), static_cast<long long>(objectType), static_cast<long long>(objectHandle), static_cast<const void*>(privateDataSlot), static_cast<long long>(data));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    payload.write_pod(objectType);
    payload.write_pod(objectHandle);
    serialize_VkPrivateDataSlot(payload, privateDataSlot);
    payload.write_pod(data);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkSetPrivateData, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkSetPrivateData -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

VkResult lsw_vkSignalSemaphore(::lsw::protocol::Transport& t, VkDevice device, const VkSemaphoreSignalInfo* pSignalInfo) {
    LSW_TRACE("client vkSignalSemaphore(device={}, pSignalInfo=ptr)",static_cast<const void*>(device));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSemaphoreSignalInfo(payload, *pSignalInfo);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkSignalSemaphore, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkSignalSemaphore -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

void lsw_vkTrimCommandPool(::lsw::protocol::Transport& t, VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) {
    LSW_TRACE("client vkTrimCommandPool(device={}, commandPool={}, flags={})",static_cast<const void*>(device), static_cast<const void*>(commandPool), static_cast<long long>(flags));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkCommandPool(payload, commandPool);
    payload.write_pod(flags);
    (void)t.send_async(::lsw::vkproto::kCmd_vkTrimCommandPool, payload.bytes());
    LSW_TRACE("client vkTrimCommandPool -> (async, no reply expected)");
}

void lsw_vkUnmapMemory(::lsw::protocol::Transport& t, VkDevice device, VkDeviceMemory memory) {
    LSW_TRACE("client vkUnmapMemory(device={}, memory={})",static_cast<const void*>(device), static_cast<const void*>(memory));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkDeviceMemory(payload, memory);
    (void)t.send_async(::lsw::vkproto::kCmd_vkUnmapMemory, payload.bytes());
    LSW_TRACE("client vkUnmapMemory -> (async, no reply expected)");
}

VkResult lsw_vkWaitSemaphores(::lsw::protocol::Transport& t, VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout) {
    LSW_TRACE("client vkWaitSemaphores(device={}, pWaitInfo=ptr, timeout={})",static_cast<const void*>(device), static_cast<long long>(timeout));
    ::lsw::protocol::Buffer payload;
    serialize_VkDevice(payload, device);
    serialize_VkSemaphoreWaitInfo(payload, *pWaitInfo);
    payload.write_pod(timeout);
    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::vkproto::kCmd_vkWaitSemaphores, payload.bytes(), &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t result_code = 0;
    if (!r.read_pod(result_code)) {
        return VK_ERROR_DEVICE_LOST;
    }
    LSW_TRACE("client vkWaitSemaphores -> result_code={}", result_code);
    return static_cast<VkResult>(result_code);
}

}  // namespace lsw::vkproto
