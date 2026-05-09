// THIS FILE IS GENERATED. DO NOT EDIT.
// Regenerate with: cmake --build build --target regenerate-vulkan
//                  (or: python -m lsw_codegen --xml ... --out generated/)

#include "vk_dispatch_server.h"

#include "lsw/log.hpp"
#include "vk_protocol_ids.h"
#include "vk_serialize.h"

#include <vector>

namespace lsw::vkproto {

bool dispatch_command(ServerDispatch& s, std::uint32_t cmd_id, ::lsw::protocol::BufferReader& r,
                      ::lsw::protocol::Buffer& reply) {
    switch (cmd_id) {
    case ::lsw::vkproto::kCmd_vkDestroyInstance: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkInstance instance = VK_NULL_HANDLE;
        if (!deserialize_VkInstance(r, instance))
            return false;
        LSW_TRACE("server vkDestroyInstance(instance={}, pAllocator=ptr)",
                  static_cast<const void*>(instance));
        s.on_vkDestroyInstance(instance, nullptr);
        LSW_TRACE("server vkDestroyInstance -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyDevice: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        LSW_TRACE("server vkDestroyDevice(device={}, pAllocator=ptr)",
                  static_cast<const void*>(device));
        s.on_vkDestroyDevice(device, nullptr);
        LSW_TRACE("server vkDestroyDevice -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceQueue: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t queueFamilyIndex{};
        uint32_t queueIndex{};
        VkQueue pQueue_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(queueFamilyIndex))
            return false;
        if (!r.read_pod(queueIndex))
            return false;
        LSW_TRACE(
            "server vkGetDeviceQueue(device={}, queueFamilyIndex={}, queueIndex={}, pQueue=ptr)",
            static_cast<const void*>(device), static_cast<long long>(queueFamilyIndex),
            static_cast<long long>(queueIndex));
        s.on_vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, &pQueue_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkQueue(reply, pQueue_out);
        LSW_TRACE("server vkGetDeviceQueue -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkEnumeratePhysicalDevices: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkInstance instance = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pPhysicalDeviceCount_local = 0;
        std::vector<VkPhysicalDevice> pPhysicalDevices_storage;
        if (!deserialize_VkInstance(r, instance))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pPhysicalDeviceCount_local))
                return false;
            pPhysicalDevices_storage.resize(pPhysicalDeviceCount_local);
        }
        LSW_TRACE("server vkEnumeratePhysicalDevices(instance={}, pPhysicalDeviceCount=ptr, "
                  "pPhysicalDevices=ptr)",
                  static_cast<const void*>(instance));
        VkResult result = s.on_vkEnumeratePhysicalDevices(
            instance, &pPhysicalDeviceCount_local,
            array_present ? pPhysicalDevices_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pPhysicalDeviceCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pPhysicalDeviceCount_local; ++k) {
                serialize_VkPhysicalDevice(reply, pPhysicalDevices_storage[k]);
            }
        }
        LSW_TRACE("server vkEnumeratePhysicalDevices -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceProperties2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties2 pProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(pProperties_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pProperties_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetPhysicalDeviceProperties2(physicalDevice={}, pProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceProperties2(physicalDevice, &pProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkPhysicalDeviceProperties2(reply, pProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceProperties2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateInstance: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkInstanceCreateInfo pCreateInfo_local{};
        VkInstance pInstance_out = VK_NULL_HANDLE;
        if (!deserialize_VkInstanceCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateInstance(pCreateInfo=ptr, pAllocator=ptr, pInstance=ptr)");
        VkResult result = s.on_vkCreateInstance(&pCreateInfo_local, nullptr, &pInstance_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkInstance(reply, pInstance_out);
        LSW_TRACE("server vkCreateInstance -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateDevice: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDeviceCreateInfo pCreateInfo_local{};
        VkDevice pDevice_out = VK_NULL_HANDLE;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkDeviceCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateDevice(physicalDevice={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pDevice=ptr)",
                  static_cast<const void*>(physicalDevice));
        VkResult result =
            s.on_vkCreateDevice(physicalDevice, &pCreateInfo_local, nullptr, &pDevice_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkDevice(reply, pDevice_out);
        LSW_TRACE("server vkCreateDevice -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties pProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        LSW_TRACE("server vkGetPhysicalDeviceProperties(physicalDevice={}, pProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceProperties(physicalDevice, &pProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkPhysicalDeviceProperties(reply, pProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceProperties -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceFeatures: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceFeatures pFeatures_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        LSW_TRACE("server vkGetPhysicalDeviceFeatures(physicalDevice={}, pFeatures=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceFeatures(physicalDevice, &pFeatures_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkPhysicalDeviceFeatures(reply, pFeatures_out);
        LSW_TRACE("server vkGetPhysicalDeviceFeatures -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceFormatProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat format{};
        VkFormatProperties pFormatProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(format))
            return false;
        LSW_TRACE("server vkGetPhysicalDeviceFormatProperties(physicalDevice={}, format={}, "
                  "pFormatProperties=ptr)",
                  static_cast<const void*>(physicalDevice), static_cast<long long>(format));
        s.on_vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &pFormatProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkFormatProperties(reply, pFormatProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceFormatProperties -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceImageFormatProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat format{};
        VkImageType type{};
        VkImageTiling tiling{};
        VkImageUsageFlags usage{};
        VkImageCreateFlags flags{};
        VkImageFormatProperties pImageFormatProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(format))
            return false;
        if (!r.read_pod(type))
            return false;
        if (!r.read_pod(tiling))
            return false;
        if (!r.read_pod(usage))
            return false;
        if (!r.read_pod(flags))
            return false;
        LSW_TRACE("server vkGetPhysicalDeviceImageFormatProperties(physicalDevice={}, format={}, "
                  "type={}, tiling={}, usage={}, flags={}, pImageFormatProperties=ptr)",
                  static_cast<const void*>(physicalDevice), static_cast<long long>(format),
                  static_cast<long long>(type), static_cast<long long>(tiling),
                  static_cast<long long>(usage), static_cast<long long>(flags));
        VkResult result = s.on_vkGetPhysicalDeviceImageFormatProperties(
            physicalDevice, format, type, tiling, usage, flags, &pImageFormatProperties_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkImageFormatProperties(reply, pImageFormatProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceImageFormatProperties -> {}",
                  static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceMemoryProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties pMemoryProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        LSW_TRACE(
            "server vkGetPhysicalDeviceMemoryProperties(physicalDevice={}, pMemoryProperties=ptr)",
            static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceMemoryProperties(physicalDevice, &pMemoryProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkPhysicalDeviceMemoryProperties(reply, pMemoryProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceMemoryProperties -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceQueueFamilyProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pQueueFamilyPropertyCount_local = 0;
        std::vector<VkQueueFamilyProperties> pQueueFamilyProperties_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pQueueFamilyPropertyCount_local))
                return false;
            pQueueFamilyProperties_storage.resize(pQueueFamilyPropertyCount_local);
        }
        LSW_TRACE("server vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice={}, "
                  "pQueueFamilyPropertyCount=ptr, pQueueFamilyProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &pQueueFamilyPropertyCount_local,
            array_present ? pQueueFamilyProperties_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pQueueFamilyPropertyCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pQueueFamilyPropertyCount_local; ++k) {
                serialize_VkQueueFamilyProperties(reply, pQueueFamilyProperties_storage[k]);
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceQueueFamilyProperties -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceSparseImageFormatProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat format{};
        VkImageType type{};
        VkSampleCountFlagBits samples{};
        VkImageUsageFlags usage{};
        VkImageTiling tiling{};
        std::uint8_t array_present = 0;
        std::uint32_t pPropertyCount_local = 0;
        std::vector<VkSparseImageFormatProperties> pProperties_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(format))
            return false;
        if (!r.read_pod(type))
            return false;
        if (!r.read_pod(samples))
            return false;
        if (!r.read_pod(usage))
            return false;
        if (!r.read_pod(tiling))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pPropertyCount_local))
                return false;
            pProperties_storage.resize(pPropertyCount_local);
        }
        LSW_TRACE(
            "server vkGetPhysicalDeviceSparseImageFormatProperties(physicalDevice={}, format={}, "
            "type={}, samples={}, usage={}, tiling={}, pPropertyCount=ptr, pProperties=ptr)",
            static_cast<const void*>(physicalDevice), static_cast<long long>(format),
            static_cast<long long>(type), static_cast<long long>(samples),
            static_cast<long long>(usage), static_cast<long long>(tiling));
        s.on_vkGetPhysicalDeviceSparseImageFormatProperties(
            physicalDevice, format, type, samples, usage, tiling, &pPropertyCount_local,
            array_present ? pProperties_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pPropertyCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pPropertyCount_local; ++k) {
                serialize_VkSparseImageFormatProperties(reply, pProperties_storage[k]);
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceSparseImageFormatProperties -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkEnumerateDeviceExtensionProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        const char* pLayerName = nullptr;
        std::uint8_t array_present = 0;
        std::uint32_t pPropertyCount_local = 0;
        std::vector<VkExtensionProperties> pProperties_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        {
            std::uint64_t n = 0;
            if (!r.read_varint(n))
                return false;
            if (n > 0) {
                char* tmp_str = arena.alloc_chars(static_cast<std::size_t>(n));
                if (!r.read(tmp_str, static_cast<std::size_t>(n)))
                    return false;
                pLayerName = tmp_str;
            }
        }
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pPropertyCount_local))
                return false;
            pProperties_storage.resize(pPropertyCount_local);
        }
        LSW_TRACE("server vkEnumerateDeviceExtensionProperties(physicalDevice={}, pLayerName=ptr, "
                  "pPropertyCount=ptr, pProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        VkResult result = s.on_vkEnumerateDeviceExtensionProperties(
            physicalDevice, pLayerName, &pPropertyCount_local,
            array_present ? pProperties_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pPropertyCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pPropertyCount_local; ++k) {
                serialize_VkExtensionProperties(reply, pProperties_storage[k]);
            }
        }
        LSW_TRACE("server vkEnumerateDeviceExtensionProperties -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkEnumerateDeviceLayerProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pPropertyCount_local = 0;
        std::vector<VkLayerProperties> pProperties_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pPropertyCount_local))
                return false;
            pProperties_storage.resize(pPropertyCount_local);
        }
        LSW_TRACE("server vkEnumerateDeviceLayerProperties(physicalDevice={}, pPropertyCount=ptr, "
                  "pProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        VkResult result = s.on_vkEnumerateDeviceLayerProperties(
            physicalDevice, &pPropertyCount_local,
            array_present ? pProperties_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pPropertyCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pPropertyCount_local; ++k) {
                serialize_VkLayerProperties(reply, pProperties_storage[k]);
            }
        }
        LSW_TRACE("server vkEnumerateDeviceLayerProperties -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceFeatures2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceFeatures2 pFeatures_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(pFeatures_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pFeatures_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetPhysicalDeviceFeatures2(physicalDevice={}, pFeatures=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceFeatures2(physicalDevice, &pFeatures_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkPhysicalDeviceFeatures2(reply, pFeatures_out);
        LSW_TRACE("server vkGetPhysicalDeviceFeatures2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceFormatProperties2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat format{};
        VkFormatProperties2 pFormatProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(format))
            return false;
        if (!r.read_pod(pFormatProperties_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pFormatProperties_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetPhysicalDeviceFormatProperties2(physicalDevice={}, format={}, "
                  "pFormatProperties=ptr)",
                  static_cast<const void*>(physicalDevice), static_cast<long long>(format));
        s.on_vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, &pFormatProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkFormatProperties2(reply, pFormatProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceFormatProperties2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceImageFormatProperties2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceImageFormatInfo2 pImageFormatInfo_local{};
        VkImageFormatProperties2 pImageFormatProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkPhysicalDeviceImageFormatInfo2(r, &pImageFormatInfo_local, arena))
            return false;
        if (!r.read_pod(pImageFormatProperties_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pImageFormatProperties_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetPhysicalDeviceImageFormatProperties2(physicalDevice={}, "
                  "pImageFormatInfo=ptr, pImageFormatProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        VkResult result = s.on_vkGetPhysicalDeviceImageFormatProperties2(
            physicalDevice, &pImageFormatInfo_local, &pImageFormatProperties_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkImageFormatProperties2(reply, pImageFormatProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceImageFormatProperties2 -> {}",
                  static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceMemoryProperties2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties2 pMemoryProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(pMemoryProperties_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pMemoryProperties_out.pNext = nullptr;
        }
        LSW_TRACE(
            "server vkGetPhysicalDeviceMemoryProperties2(physicalDevice={}, pMemoryProperties=ptr)",
            static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &pMemoryProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkPhysicalDeviceMemoryProperties2(reply, pMemoryProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceMemoryProperties2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceQueueFamilyProperties2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pQueueFamilyPropertyCount_local = 0;
        std::vector<VkQueueFamilyProperties2> pQueueFamilyProperties_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pQueueFamilyPropertyCount_local))
                return false;
            pQueueFamilyProperties_storage.resize(pQueueFamilyPropertyCount_local);
            for (auto& __e : pQueueFamilyProperties_storage) {
                __e.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
                __e.pNext = nullptr;
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice={}, "
                  "pQueueFamilyPropertyCount=ptr, pQueueFamilyProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceQueueFamilyProperties2(
            physicalDevice, &pQueueFamilyPropertyCount_local,
            array_present ? pQueueFamilyProperties_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pQueueFamilyPropertyCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pQueueFamilyPropertyCount_local; ++k) {
                serialize_VkQueueFamilyProperties2(reply, pQueueFamilyProperties_storage[k]);
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceQueueFamilyProperties2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceSparseImageFormatProperties2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceSparseImageFormatInfo2 pFormatInfo_local{};
        std::uint8_t array_present = 0;
        std::uint32_t pPropertyCount_local = 0;
        std::vector<VkSparseImageFormatProperties2> pProperties_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkPhysicalDeviceSparseImageFormatInfo2(r, &pFormatInfo_local, arena))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pPropertyCount_local))
                return false;
            pProperties_storage.resize(pPropertyCount_local);
            for (auto& __e : pProperties_storage) {
                __e.sType = VK_STRUCTURE_TYPE_SPARSE_IMAGE_FORMAT_PROPERTIES_2;
                __e.pNext = nullptr;
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceSparseImageFormatProperties2(physicalDevice={}, "
                  "pFormatInfo=ptr, pPropertyCount=ptr, pProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceSparseImageFormatProperties2(
            physicalDevice, &pFormatInfo_local, &pPropertyCount_local,
            array_present ? pProperties_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pPropertyCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pPropertyCount_local; ++k) {
                serialize_VkSparseImageFormatProperties2(reply, pProperties_storage[k]);
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceSparseImageFormatProperties2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDeviceWaitIdle: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        LSW_TRACE("server vkDeviceWaitIdle(device={})", static_cast<const void*>(device));
        VkResult result = s.on_vkDeviceWaitIdle(device);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkDeviceWaitIdle -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkQueueWaitIdle: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkQueue queue = VK_NULL_HANDLE;
        if (!deserialize_VkQueue(r, queue))
            return false;
        LSW_TRACE("server vkQueueWaitIdle(queue={})", static_cast<const void*>(queue));
        VkResult result = s.on_vkQueueWaitIdle(queue);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkQueueWaitIdle -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkAllocateMemory: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkMemoryAllocateInfo pAllocateInfo_local{};
        VkDeviceMemory pMemory_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkMemoryAllocateInfo(r, &pAllocateInfo_local, arena))
            return false;
        LSW_TRACE(
            "server vkAllocateMemory(device={}, pAllocateInfo=ptr, pAllocator=ptr, pMemory=ptr)",
            static_cast<const void*>(device));
        VkResult result =
            s.on_vkAllocateMemory(device, &pAllocateInfo_local, nullptr, &pMemory_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkDeviceMemory(reply, pMemory_out);
        LSW_TRACE("server vkAllocateMemory -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkFreeMemory: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDeviceMemory(r, memory))
            return false;
        LSW_TRACE("server vkFreeMemory(device={}, memory={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(memory));
        s.on_vkFreeMemory(device, memory, nullptr);
        LSW_TRACE("server vkFreeMemory -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkBindBufferMemory: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize memoryOffset{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        if (!deserialize_VkDeviceMemory(r, memory))
            return false;
        if (!r.read_pod(memoryOffset))
            return false;
        LSW_TRACE("server vkBindBufferMemory(device={}, buffer={}, memory={}, memoryOffset={})",
                  static_cast<const void*>(device), static_cast<const void*>(buffer),
                  static_cast<const void*>(memory), static_cast<long long>(memoryOffset));
        VkResult result = s.on_vkBindBufferMemory(device, buffer, memory, memoryOffset);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkBindBufferMemory -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkBindImageMemory: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize memoryOffset{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImage(r, image))
            return false;
        if (!deserialize_VkDeviceMemory(r, memory))
            return false;
        if (!r.read_pod(memoryOffset))
            return false;
        LSW_TRACE("server vkBindImageMemory(device={}, image={}, memory={}, memoryOffset={})",
                  static_cast<const void*>(device), static_cast<const void*>(image),
                  static_cast<const void*>(memory), static_cast<long long>(memoryOffset));
        VkResult result = s.on_vkBindImageMemory(device, image, memory, memoryOffset);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkBindImageMemory -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetBufferMemoryRequirements: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkMemoryRequirements pMemoryRequirements_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        LSW_TRACE(
            "server vkGetBufferMemoryRequirements(device={}, buffer={}, pMemoryRequirements=ptr)",
            static_cast<const void*>(device), static_cast<const void*>(buffer));
        s.on_vkGetBufferMemoryRequirements(device, buffer, &pMemoryRequirements_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkMemoryRequirements(reply, pMemoryRequirements_out);
        LSW_TRACE("server vkGetBufferMemoryRequirements -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetImageMemoryRequirements: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkMemoryRequirements pMemoryRequirements_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImage(r, image))
            return false;
        LSW_TRACE(
            "server vkGetImageMemoryRequirements(device={}, image={}, pMemoryRequirements=ptr)",
            static_cast<const void*>(device), static_cast<const void*>(image));
        s.on_vkGetImageMemoryRequirements(device, image, &pMemoryRequirements_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkMemoryRequirements(reply, pMemoryRequirements_out);
        LSW_TRACE("server vkGetImageMemoryRequirements -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBufferCreateInfo pCreateInfo_local{};
        VkBuffer pBuffer_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBufferCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateBuffer(device={}, pCreateInfo=ptr, pAllocator=ptr, pBuffer=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkCreateBuffer(device, &pCreateInfo_local, nullptr, &pBuffer_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkBuffer(reply, pBuffer_out);
        LSW_TRACE("server vkCreateBuffer -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        LSW_TRACE("server vkDestroyBuffer(device={}, buffer={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(buffer));
        s.on_vkDestroyBuffer(device, buffer, nullptr);
        LSW_TRACE("server vkDestroyBuffer -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateImage: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImageCreateInfo pCreateInfo_local{};
        VkImage pImage_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImageCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateImage(device={}, pCreateInfo=ptr, pAllocator=ptr, pImage=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkCreateImage(device, &pCreateInfo_local, nullptr, &pImage_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkImage(reply, pImage_out);
        LSW_TRACE("server vkCreateImage -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyImage: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImage(r, image))
            return false;
        LSW_TRACE("server vkDestroyImage(device={}, image={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(image));
        s.on_vkDestroyImage(device, image, nullptr);
        LSW_TRACE("server vkDestroyImage -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateImageView: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImageViewCreateInfo pCreateInfo_local{};
        VkImageView pView_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImageViewCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateImageView(device={}, pCreateInfo=ptr, pAllocator=ptr, pView=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkCreateImageView(device, &pCreateInfo_local, nullptr, &pView_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkImageView(reply, pView_out);
        LSW_TRACE("server vkCreateImageView -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyImageView: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImageView(r, imageView))
            return false;
        LSW_TRACE("server vkDestroyImageView(device={}, imageView={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(imageView));
        s.on_vkDestroyImageView(device, imageView, nullptr);
        LSW_TRACE("server vkDestroyImageView -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateSampler: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSamplerCreateInfo pCreateInfo_local{};
        VkSampler pSampler_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSamplerCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE(
            "server vkCreateSampler(device={}, pCreateInfo=ptr, pAllocator=ptr, pSampler=ptr)",
            static_cast<const void*>(device));
        VkResult result = s.on_vkCreateSampler(device, &pCreateInfo_local, nullptr, &pSampler_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkSampler(reply, pSampler_out);
        LSW_TRACE("server vkCreateSampler -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroySampler: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSampler(r, sampler))
            return false;
        LSW_TRACE("server vkDestroySampler(device={}, sampler={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(sampler));
        s.on_vkDestroySampler(device, sampler, nullptr);
        LSW_TRACE("server vkDestroySampler -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateSemaphore: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSemaphoreCreateInfo pCreateInfo_local{};
        VkSemaphore pSemaphore_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSemaphoreCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE(
            "server vkCreateSemaphore(device={}, pCreateInfo=ptr, pAllocator=ptr, pSemaphore=ptr)",
            static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateSemaphore(device, &pCreateInfo_local, nullptr, &pSemaphore_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkSemaphore(reply, pSemaphore_out);
        LSW_TRACE("server vkCreateSemaphore -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroySemaphore: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSemaphore(r, semaphore))
            return false;
        LSW_TRACE("server vkDestroySemaphore(device={}, semaphore={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(semaphore));
        s.on_vkDestroySemaphore(device, semaphore, nullptr);
        LSW_TRACE("server vkDestroySemaphore -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateFence: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkFenceCreateInfo pCreateInfo_local{};
        VkFence pFence_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkFenceCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateFence(device={}, pCreateInfo=ptr, pAllocator=ptr, pFence=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkCreateFence(device, &pCreateInfo_local, nullptr, &pFence_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkFence(reply, pFence_out);
        LSW_TRACE("server vkCreateFence -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyFence: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkFence(r, fence))
            return false;
        LSW_TRACE("server vkDestroyFence(device={}, fence={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(fence));
        s.on_vkDestroyFence(device, fence, nullptr);
        LSW_TRACE("server vkDestroyFence -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkResetFences: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t fenceCount{};
        std::vector<VkFence> pFences_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(fenceCount))
            return false;
        pFences_storage.resize(static_cast<std::size_t>(fenceCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(fenceCount); ++__i) {
            if (!deserialize_VkFence(r, pFences_storage[__i]))
                return false;
        }
        LSW_TRACE("server vkResetFences(device={}, fenceCount={}, pFences=ptr)",
                  static_cast<const void*>(device), static_cast<long long>(fenceCount));
        VkResult result = s.on_vkResetFences(device, fenceCount, pFences_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkResetFences -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkWaitForFences: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t fenceCount{};
        std::vector<VkFence> pFences_storage;
        VkBool32 waitAll{};
        uint64_t timeout{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(fenceCount))
            return false;
        pFences_storage.resize(static_cast<std::size_t>(fenceCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(fenceCount); ++__i) {
            if (!deserialize_VkFence(r, pFences_storage[__i]))
                return false;
        }
        if (!r.read_pod(waitAll))
            return false;
        if (!r.read_pod(timeout))
            return false;
        LSW_TRACE(
            "server vkWaitForFences(device={}, fenceCount={}, pFences=ptr, waitAll={}, timeout={})",
            static_cast<const void*>(device), static_cast<long long>(fenceCount),
            static_cast<long long>(waitAll), static_cast<long long>(timeout));
        VkResult result =
            s.on_vkWaitForFences(device, fenceCount, pFences_storage.data(), waitAll, timeout);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkWaitForFences -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetFenceStatus: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkFence(r, fence))
            return false;
        LSW_TRACE("server vkGetFenceStatus(device={}, fence={})", static_cast<const void*>(device),
                  static_cast<const void*>(fence));
        VkResult result = s.on_vkGetFenceStatus(device, fence);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkGetFenceStatus -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateCommandPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo pCreateInfo_local{};
        VkCommandPool pCommandPool_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkCommandPoolCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateCommandPool(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pCommandPool=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateCommandPool(device, &pCreateInfo_local, nullptr, &pCommandPool_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkCommandPool(reply, pCommandPool_out);
        LSW_TRACE("server vkCreateCommandPool -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyCommandPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkCommandPool(r, commandPool))
            return false;
        LSW_TRACE("server vkDestroyCommandPool(device={}, commandPool={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(commandPool));
        s.on_vkDestroyCommandPool(device, commandPool, nullptr);
        LSW_TRACE("server vkDestroyCommandPool -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkResetCommandPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandPoolResetFlags flags{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkCommandPool(r, commandPool))
            return false;
        if (!r.read_pod(flags))
            return false;
        LSW_TRACE("server vkResetCommandPool(device={}, commandPool={}, flags={})",
                  static_cast<const void*>(device), static_cast<const void*>(commandPool),
                  static_cast<long long>(flags));
        VkResult result = s.on_vkResetCommandPool(device, commandPool, flags);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkResetCommandPool -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkBeginCommandBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCommandBufferBeginInfo pBeginInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkCommandBufferBeginInfo(r, &pBeginInfo_local, arena))
            return false;
        LSW_TRACE("server vkBeginCommandBuffer(commandBuffer={}, pBeginInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        VkResult result = s.on_vkBeginCommandBuffer(commandBuffer, &pBeginInfo_local);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkBeginCommandBuffer -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkEndCommandBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        LSW_TRACE("server vkEndCommandBuffer(commandBuffer={})",
                  static_cast<const void*>(commandBuffer));
        VkResult result = s.on_vkEndCommandBuffer(commandBuffer);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkEndCommandBuffer -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkResetCommandBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCommandBufferResetFlags flags{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(flags))
            return false;
        LSW_TRACE("server vkResetCommandBuffer(commandBuffer={}, flags={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(flags));
        VkResult result = s.on_vkResetCommandBuffer(commandBuffer, flags);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkResetCommandBuffer -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyShaderModule: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkShaderModule(r, shaderModule))
            return false;
        LSW_TRACE("server vkDestroyShaderModule(device={}, shaderModule={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(shaderModule));
        s.on_vkDestroyShaderModule(device, shaderModule, nullptr);
        LSW_TRACE("server vkDestroyShaderModule -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateShaderModule: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo pCreateInfo_local{};
        VkShaderModule pShaderModule_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkShaderModuleCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateShaderModule(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pShaderModule=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateShaderModule(device, &pCreateInfo_local, nullptr, &pShaderModule_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkShaderModule(reply, pShaderModule_out);
        LSW_TRACE("server vkCreateShaderModule -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreatePipelineLayout: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineLayoutCreateInfo pCreateInfo_local{};
        VkPipelineLayout pPipelineLayout_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPipelineLayoutCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreatePipelineLayout(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pPipelineLayout=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreatePipelineLayout(device, &pCreateInfo_local, nullptr, &pPipelineLayout_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkPipelineLayout(reply, pPipelineLayout_out);
        LSW_TRACE("server vkCreatePipelineLayout -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyPipelineLayout: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPipelineLayout(r, pipelineLayout))
            return false;
        LSW_TRACE("server vkDestroyPipelineLayout(device={}, pipelineLayout={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(pipelineLayout));
        s.on_vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        LSW_TRACE("server vkDestroyPipelineLayout -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyPipeline: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPipeline(r, pipeline))
            return false;
        LSW_TRACE("server vkDestroyPipeline(device={}, pipeline={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(pipeline));
        s.on_vkDestroyPipeline(device, pipeline, nullptr);
        LSW_TRACE("server vkDestroyPipeline -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreatePipelineCache: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineCacheCreateInfo pCreateInfo_local{};
        VkPipelineCache pPipelineCache_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPipelineCacheCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreatePipelineCache(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pPipelineCache=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreatePipelineCache(device, &pCreateInfo_local, nullptr, &pPipelineCache_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkPipelineCache(reply, pPipelineCache_out);
        LSW_TRACE("server vkCreatePipelineCache -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyPipelineCache: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPipelineCache(r, pipelineCache))
            return false;
        LSW_TRACE("server vkDestroyPipelineCache(device={}, pipelineCache={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(pipelineCache));
        s.on_vkDestroyPipelineCache(device, pipelineCache, nullptr);
        LSW_TRACE("server vkDestroyPipelineCache -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateRenderPass: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkRenderPassCreateInfo pCreateInfo_local{};
        VkRenderPass pRenderPass_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkRenderPassCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateRenderPass(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pRenderPass=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateRenderPass(device, &pCreateInfo_local, nullptr, &pRenderPass_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkRenderPass(reply, pRenderPass_out);
        LSW_TRACE("server vkCreateRenderPass -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyRenderPass: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkRenderPass(r, renderPass))
            return false;
        LSW_TRACE("server vkDestroyRenderPass(device={}, renderPass={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(renderPass));
        s.on_vkDestroyRenderPass(device, renderPass, nullptr);
        LSW_TRACE("server vkDestroyRenderPass -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateFramebuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkFramebufferCreateInfo pCreateInfo_local{};
        VkFramebuffer pFramebuffer_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkFramebufferCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateFramebuffer(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pFramebuffer=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateFramebuffer(device, &pCreateInfo_local, nullptr, &pFramebuffer_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkFramebuffer(reply, pFramebuffer_out);
        LSW_TRACE("server vkCreateFramebuffer -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyFramebuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkFramebuffer(r, framebuffer))
            return false;
        LSW_TRACE("server vkDestroyFramebuffer(device={}, framebuffer={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(framebuffer));
        s.on_vkDestroyFramebuffer(device, framebuffer, nullptr);
        LSW_TRACE("server vkDestroyFramebuffer -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateDescriptorSetLayout: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorSetLayoutCreateInfo pCreateInfo_local{};
        VkDescriptorSetLayout pSetLayout_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorSetLayoutCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateDescriptorSetLayout(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pSetLayout=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateDescriptorSetLayout(device, &pCreateInfo_local, nullptr, &pSetLayout_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkDescriptorSetLayout(reply, pSetLayout_out);
        LSW_TRACE("server vkCreateDescriptorSetLayout -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyDescriptorSetLayout: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorSetLayout(r, descriptorSetLayout))
            return false;
        LSW_TRACE("server vkDestroyDescriptorSetLayout(device={}, descriptorSetLayout={}, "
                  "pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(descriptorSetLayout));
        s.on_vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        LSW_TRACE("server vkDestroyDescriptorSetLayout -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateDescriptorPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorPoolCreateInfo pCreateInfo_local{};
        VkDescriptorPool pDescriptorPool_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorPoolCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateDescriptorPool(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pDescriptorPool=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateDescriptorPool(device, &pCreateInfo_local, nullptr, &pDescriptorPool_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkDescriptorPool(reply, pDescriptorPool_out);
        LSW_TRACE("server vkCreateDescriptorPool -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyDescriptorPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorPool(r, descriptorPool))
            return false;
        LSW_TRACE("server vkDestroyDescriptorPool(device={}, descriptorPool={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(descriptorPool));
        s.on_vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        LSW_TRACE("server vkDestroyDescriptorPool -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkFreeCommandBuffers: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        uint32_t commandBufferCount{};
        std::vector<VkCommandBuffer> pCommandBuffers_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkCommandPool(r, commandPool))
            return false;
        if (!r.read_pod(commandBufferCount))
            return false;
        pCommandBuffers_storage.resize(static_cast<std::size_t>(commandBufferCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(commandBufferCount); ++__i) {
            if (!deserialize_VkCommandBuffer(r, pCommandBuffers_storage[__i]))
                return false;
        }
        LSW_TRACE("server vkFreeCommandBuffers(device={}, commandPool={}, commandBufferCount={}, "
                  "pCommandBuffers=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(commandPool),
                  static_cast<long long>(commandBufferCount));
        s.on_vkFreeCommandBuffers(device, commandPool, commandBufferCount,
                                  pCommandBuffers_storage.data());
        LSW_TRACE("server vkFreeCommandBuffers -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkQueueSubmit: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t submitCount{};
        std::vector<VkSubmitInfo> pSubmits_storage;
        VkFence fence = VK_NULL_HANDLE;
        if (!deserialize_VkQueue(r, queue))
            return false;
        if (!r.read_pod(submitCount))
            return false;
        pSubmits_storage.resize(static_cast<std::size_t>(submitCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(submitCount); ++__i) {
            if (!deserialize_VkSubmitInfo(r, &pSubmits_storage[__i], arena))
                return false;
        }
        if (!deserialize_VkFence(r, fence))
            return false;
        LSW_TRACE("server vkQueueSubmit(queue={}, submitCount={}, pSubmits=ptr, fence={})",
                  static_cast<const void*>(queue), static_cast<long long>(submitCount),
                  static_cast<const void*>(fence));
        VkResult result = s.on_vkQueueSubmit(queue, submitCount, pSubmits_storage.data(), fence);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkQueueSubmit -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBindPipeline: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipelineBindPoint pipelineBindPoint{};
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(pipelineBindPoint))
            return false;
        if (!deserialize_VkPipeline(r, pipeline))
            return false;
        LSW_TRACE("server vkCmdBindPipeline(commandBuffer={}, pipelineBindPoint={}, pipeline={})",
                  static_cast<const void*>(commandBuffer),
                  static_cast<long long>(pipelineBindPoint), static_cast<const void*>(pipeline));
        s.on_vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
        LSW_TRACE("server vkCmdBindPipeline -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetViewport: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t firstViewport{};
        uint32_t viewportCount{};
        std::vector<VkViewport> pViewports_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(firstViewport))
            return false;
        if (!r.read_pod(viewportCount))
            return false;
        pViewports_storage.resize(static_cast<std::size_t>(viewportCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(viewportCount); ++__i) {
            if (!deserialize_VkViewport(r, &pViewports_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdSetViewport(commandBuffer={}, firstViewport={}, viewportCount={}, "
                  "pViewports=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(firstViewport),
                  static_cast<long long>(viewportCount));
        s.on_vkCmdSetViewport(commandBuffer, firstViewport, viewportCount,
                              pViewports_storage.data());
        LSW_TRACE("server vkCmdSetViewport -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetScissor: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t firstScissor{};
        uint32_t scissorCount{};
        std::vector<VkRect2D> pScissors_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(firstScissor))
            return false;
        if (!r.read_pod(scissorCount))
            return false;
        pScissors_storage.resize(static_cast<std::size_t>(scissorCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(scissorCount); ++__i) {
            if (!deserialize_VkRect2D(r, &pScissors_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdSetScissor(commandBuffer={}, firstScissor={}, scissorCount={}, "
                  "pScissors=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(firstScissor),
                  static_cast<long long>(scissorCount));
        s.on_vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors_storage.data());
        LSW_TRACE("server vkCmdSetScissor -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBindVertexBuffers: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t firstBinding{};
        uint32_t bindingCount{};
        std::vector<VkBuffer> pBuffers_storage;
        std::vector<VkDeviceSize> pOffsets_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(firstBinding))
            return false;
        if (!r.read_pod(bindingCount))
            return false;
        pBuffers_storage.resize(static_cast<std::size_t>(bindingCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
            if (!deserialize_VkBuffer(r, pBuffers_storage[__i]))
                return false;
        }
        pOffsets_storage.resize(static_cast<std::size_t>(bindingCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
            if (!r.read_pod(pOffsets_storage[__i]))
                return false;
        }
        LSW_TRACE("server vkCmdBindVertexBuffers(commandBuffer={}, firstBinding={}, "
                  "bindingCount={}, pBuffers=ptr, pOffsets=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(firstBinding),
                  static_cast<long long>(bindingCount));
        s.on_vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount,
                                    pBuffers_storage.data(), pOffsets_storage.data());
        LSW_TRACE("server vkCmdBindVertexBuffers -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBindIndexBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset{};
        VkIndexType indexType{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        if (!r.read_pod(offset))
            return false;
        if (!r.read_pod(indexType))
            return false;
        LSW_TRACE(
            "server vkCmdBindIndexBuffer(commandBuffer={}, buffer={}, offset={}, indexType={})",
            static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer),
            static_cast<long long>(offset), static_cast<long long>(indexType));
        s.on_vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
        LSW_TRACE("server vkCmdBindIndexBuffer -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBindDescriptorSets: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipelineBindPoint pipelineBindPoint{};
        VkPipelineLayout layout = VK_NULL_HANDLE;
        uint32_t firstSet{};
        uint32_t descriptorSetCount{};
        std::vector<VkDescriptorSet> pDescriptorSets_storage;
        uint32_t dynamicOffsetCount{};
        std::vector<uint32_t> pDynamicOffsets_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(pipelineBindPoint))
            return false;
        if (!deserialize_VkPipelineLayout(r, layout))
            return false;
        if (!r.read_pod(firstSet))
            return false;
        if (!r.read_pod(descriptorSetCount))
            return false;
        pDescriptorSets_storage.resize(static_cast<std::size_t>(descriptorSetCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(descriptorSetCount); ++__i) {
            if (!deserialize_VkDescriptorSet(r, pDescriptorSets_storage[__i]))
                return false;
        }
        if (!r.read_pod(dynamicOffsetCount))
            return false;
        pDynamicOffsets_storage.resize(static_cast<std::size_t>(dynamicOffsetCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(dynamicOffsetCount); ++__i) {
            if (!r.read_pod(pDynamicOffsets_storage[__i]))
                return false;
        }
        LSW_TRACE("server vkCmdBindDescriptorSets(commandBuffer={}, pipelineBindPoint={}, "
                  "layout={}, firstSet={}, descriptorSetCount={}, pDescriptorSets=ptr, "
                  "dynamicOffsetCount={}, pDynamicOffsets=ptr)",
                  static_cast<const void*>(commandBuffer),
                  static_cast<long long>(pipelineBindPoint), static_cast<const void*>(layout),
                  static_cast<long long>(firstSet), static_cast<long long>(descriptorSetCount),
                  static_cast<long long>(dynamicOffsetCount));
        s.on_vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet,
                                     descriptorSetCount, pDescriptorSets_storage.data(),
                                     dynamicOffsetCount, pDynamicOffsets_storage.data());
        LSW_TRACE("server vkCmdBindDescriptorSets -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDraw: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t vertexCount{};
        uint32_t instanceCount{};
        uint32_t firstVertex{};
        uint32_t firstInstance{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(vertexCount))
            return false;
        if (!r.read_pod(instanceCount))
            return false;
        if (!r.read_pod(firstVertex))
            return false;
        if (!r.read_pod(firstInstance))
            return false;
        LSW_TRACE("server vkCmdDraw(commandBuffer={}, vertexCount={}, instanceCount={}, "
                  "firstVertex={}, firstInstance={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(vertexCount),
                  static_cast<long long>(instanceCount), static_cast<long long>(firstVertex),
                  static_cast<long long>(firstInstance));
        s.on_vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
        LSW_TRACE("server vkCmdDraw -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDrawIndexed: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t indexCount{};
        uint32_t instanceCount{};
        uint32_t firstIndex{};
        int32_t vertexOffset{};
        uint32_t firstInstance{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(indexCount))
            return false;
        if (!r.read_pod(instanceCount))
            return false;
        if (!r.read_pod(firstIndex))
            return false;
        if (!r.read_pod(vertexOffset))
            return false;
        if (!r.read_pod(firstInstance))
            return false;
        LSW_TRACE("server vkCmdDrawIndexed(commandBuffer={}, indexCount={}, instanceCount={}, "
                  "firstIndex={}, vertexOffset={}, firstInstance={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(indexCount),
                  static_cast<long long>(instanceCount), static_cast<long long>(firstIndex),
                  static_cast<long long>(vertexOffset), static_cast<long long>(firstInstance));
        s.on_vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset,
                              firstInstance);
        LSW_TRACE("server vkCmdDrawIndexed -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdPipelineBarrier: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipelineStageFlags srcStageMask{};
        VkPipelineStageFlags dstStageMask{};
        VkDependencyFlags dependencyFlags{};
        uint32_t memoryBarrierCount{};
        std::vector<VkMemoryBarrier> pMemoryBarriers_storage;
        uint32_t bufferMemoryBarrierCount{};
        std::vector<VkBufferMemoryBarrier> pBufferMemoryBarriers_storage;
        uint32_t imageMemoryBarrierCount{};
        std::vector<VkImageMemoryBarrier> pImageMemoryBarriers_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(srcStageMask))
            return false;
        if (!r.read_pod(dstStageMask))
            return false;
        if (!r.read_pod(dependencyFlags))
            return false;
        if (!r.read_pod(memoryBarrierCount))
            return false;
        pMemoryBarriers_storage.resize(static_cast<std::size_t>(memoryBarrierCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(memoryBarrierCount); ++__i) {
            if (!deserialize_VkMemoryBarrier(r, &pMemoryBarriers_storage[__i], arena))
                return false;
        }
        if (!r.read_pod(bufferMemoryBarrierCount))
            return false;
        pBufferMemoryBarriers_storage.resize(static_cast<std::size_t>(bufferMemoryBarrierCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bufferMemoryBarrierCount);
             ++__i) {
            if (!deserialize_VkBufferMemoryBarrier(r, &pBufferMemoryBarriers_storage[__i], arena))
                return false;
        }
        if (!r.read_pod(imageMemoryBarrierCount))
            return false;
        pImageMemoryBarriers_storage.resize(static_cast<std::size_t>(imageMemoryBarrierCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(imageMemoryBarrierCount);
             ++__i) {
            if (!deserialize_VkImageMemoryBarrier(r, &pImageMemoryBarriers_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdPipelineBarrier(commandBuffer={}, srcStageMask={}, dstStageMask={}, "
                  "dependencyFlags={}, memoryBarrierCount={}, pMemoryBarriers=ptr, "
                  "bufferMemoryBarrierCount={}, pBufferMemoryBarriers=ptr, "
                  "imageMemoryBarrierCount={}, pImageMemoryBarriers=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(srcStageMask),
                  static_cast<long long>(dstStageMask), static_cast<long long>(dependencyFlags),
                  static_cast<long long>(memoryBarrierCount),
                  static_cast<long long>(bufferMemoryBarrierCount),
                  static_cast<long long>(imageMemoryBarrierCount));
        s.on_vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
                                  memoryBarrierCount, pMemoryBarriers_storage.data(),
                                  bufferMemoryBarrierCount, pBufferMemoryBarriers_storage.data(),
                                  imageMemoryBarrierCount, pImageMemoryBarriers_storage.data());
        LSW_TRACE("server vkCmdPipelineBarrier -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer srcBuffer = VK_NULL_HANDLE;
        VkBuffer dstBuffer = VK_NULL_HANDLE;
        uint32_t regionCount{};
        std::vector<VkBufferCopy> pRegions_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, srcBuffer))
            return false;
        if (!deserialize_VkBuffer(r, dstBuffer))
            return false;
        if (!r.read_pod(regionCount))
            return false;
        pRegions_storage.resize(static_cast<std::size_t>(regionCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
            if (!deserialize_VkBufferCopy(r, &pRegions_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdCopyBuffer(commandBuffer={}, srcBuffer={}, dstBuffer={}, "
                  "regionCount={}, pRegions=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(srcBuffer),
                  static_cast<const void*>(dstBuffer), static_cast<long long>(regionCount));
        s.on_vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount,
                             pRegions_storage.data());
        LSW_TRACE("server vkCmdCopyBuffer -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBeginRenderPass: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkRenderPassBeginInfo pRenderPassBegin_local{};
        VkSubpassContents contents{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkRenderPassBeginInfo(r, &pRenderPassBegin_local, arena))
            return false;
        if (!r.read_pod(contents))
            return false;
        LSW_TRACE(
            "server vkCmdBeginRenderPass(commandBuffer={}, pRenderPassBegin=ptr, contents={})",
            static_cast<const void*>(commandBuffer), static_cast<long long>(contents));
        s.on_vkCmdBeginRenderPass(commandBuffer, &pRenderPassBegin_local, contents);
        LSW_TRACE("server vkCmdBeginRenderPass -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdEndRenderPass: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        LSW_TRACE("server vkCmdEndRenderPass(commandBuffer={})",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdEndRenderPass(commandBuffer);
        LSW_TRACE("server vkCmdEndRenderPass -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdNextSubpass: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSubpassContents contents{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(contents))
            return false;
        LSW_TRACE("server vkCmdNextSubpass(commandBuffer={}, contents={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(contents));
        s.on_vkCmdNextSubpass(commandBuffer, contents);
        LSW_TRACE("server vkCmdNextSubpass -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdPushConstants: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkShaderStageFlags stageFlags{};
        uint32_t offset{};
        uint32_t size{};
        std::vector<std::uint8_t> pValues_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkPipelineLayout(r, layout))
            return false;
        if (!r.read_pod(stageFlags))
            return false;
        if (!r.read_pod(offset))
            return false;
        if (!r.read_pod(size))
            return false;
        if (size > 0) {
            pValues_storage.resize(static_cast<std::size_t>(size));
            if (!r.read(pValues_storage.data(), static_cast<std::size_t>(size)))
                return false;
        }
        LSW_TRACE("server vkCmdPushConstants(commandBuffer={}, layout={}, stageFlags={}, "
                  "offset={}, size={}, pValues=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(layout),
                  static_cast<long long>(stageFlags), static_cast<long long>(offset),
                  static_cast<long long>(size));
        s.on_vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size,
                                pValues_storage.data());
        LSW_TRACE("server vkCmdPushConstants -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceSurfaceSupportKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        uint32_t queueFamilyIndex{};
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkBool32 pSupported_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(queueFamilyIndex))
            return false;
        if (!deserialize_VkSurfaceKHR(r, surface))
            return false;
        LSW_TRACE("server vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice={}, "
                  "queueFamilyIndex={}, surface={}, pSupported=ptr)",
                  static_cast<const void*>(physicalDevice),
                  static_cast<long long>(queueFamilyIndex), static_cast<const void*>(surface));
        VkResult result = s.on_vkGetPhysicalDeviceSurfaceSupportKHR(
            physicalDevice, queueFamilyIndex, surface, &pSupported_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pSupported_out);
        LSW_TRACE("server vkGetPhysicalDeviceSurfaceSupportKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceSurfaceCapabilitiesKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSurfaceCapabilitiesKHR pSurfaceCapabilities_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkSurfaceKHR(r, surface))
            return false;
        LSW_TRACE("server vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice={}, surface={}, "
                  "pSurfaceCapabilities=ptr)",
                  static_cast<const void*>(physicalDevice), static_cast<const void*>(surface));
        VkResult result = s.on_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                                                         &pSurfaceCapabilities_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkSurfaceCapabilitiesKHR(reply, pSurfaceCapabilities_out);
        LSW_TRACE("server vkGetPhysicalDeviceSurfaceCapabilitiesKHR -> {}",
                  static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceSurfaceFormatsKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pSurfaceFormatCount_local = 0;
        std::vector<VkSurfaceFormatKHR> pSurfaceFormats_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkSurfaceKHR(r, surface))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pSurfaceFormatCount_local))
                return false;
            pSurfaceFormats_storage.resize(pSurfaceFormatCount_local);
        }
        LSW_TRACE("server vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice={}, surface={}, "
                  "pSurfaceFormatCount=ptr, pSurfaceFormats=ptr)",
                  static_cast<const void*>(physicalDevice), static_cast<const void*>(surface));
        VkResult result = s.on_vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice, surface, &pSurfaceFormatCount_local,
            array_present ? pSurfaceFormats_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pSurfaceFormatCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pSurfaceFormatCount_local; ++k) {
                serialize_VkSurfaceFormatKHR(reply, pSurfaceFormats_storage[k]);
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceSurfaceFormatsKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceSurfacePresentModesKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pPresentModeCount_local = 0;
        std::vector<VkPresentModeKHR> pPresentModes_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkSurfaceKHR(r, surface))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pPresentModeCount_local))
                return false;
            pPresentModes_storage.resize(pPresentModeCount_local);
        }
        LSW_TRACE("server vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice={}, surface={}, "
                  "pPresentModeCount=ptr, pPresentModes=ptr)",
                  static_cast<const void*>(physicalDevice), static_cast<const void*>(surface));
        VkResult result = s.on_vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice, surface, &pPresentModeCount_local,
            array_present ? pPresentModes_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pPresentModeCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pPresentModeCount_local; ++k) {
                reply.write_pod(pPresentModes_storage[k]);
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceSurfacePresentModesKHR -> {}",
                  static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateSwapchainKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSwapchainCreateInfoKHR pCreateInfo_local{};
        VkSwapchainKHR pSwapchain_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSwapchainCreateInfoKHR(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateSwapchainKHR(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pSwapchain=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateSwapchainKHR(device, &pCreateInfo_local, nullptr, &pSwapchain_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkSwapchainKHR(reply, pSwapchain_out);
        LSW_TRACE("server vkCreateSwapchainKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroySwapchainKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSwapchainKHR(r, swapchain))
            return false;
        LSW_TRACE("server vkDestroySwapchainKHR(device={}, swapchain={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(swapchain));
        s.on_vkDestroySwapchainKHR(device, swapchain, nullptr);
        LSW_TRACE("server vkDestroySwapchainKHR -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetSwapchainImagesKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pSwapchainImageCount_local = 0;
        std::vector<VkImage> pSwapchainImages_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSwapchainKHR(r, swapchain))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pSwapchainImageCount_local))
                return false;
            pSwapchainImages_storage.resize(pSwapchainImageCount_local);
        }
        LSW_TRACE("server vkGetSwapchainImagesKHR(device={}, swapchain={}, "
                  "pSwapchainImageCount=ptr, pSwapchainImages=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(swapchain));
        VkResult result =
            s.on_vkGetSwapchainImagesKHR(device, swapchain, &pSwapchainImageCount_local,
                                         array_present ? pSwapchainImages_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pSwapchainImageCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pSwapchainImageCount_local; ++k) {
                serialize_VkImage(reply, pSwapchainImages_storage[k]);
            }
        }
        LSW_TRACE("server vkGetSwapchainImagesKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkAcquireNextImageKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        uint64_t timeout{};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        uint32_t pImageIndex_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSwapchainKHR(r, swapchain))
            return false;
        if (!r.read_pod(timeout))
            return false;
        if (!deserialize_VkSemaphore(r, semaphore))
            return false;
        if (!deserialize_VkFence(r, fence))
            return false;
        LSW_TRACE("server vkAcquireNextImageKHR(device={}, swapchain={}, timeout={}, semaphore={}, "
                  "fence={}, pImageIndex=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(swapchain),
                  static_cast<long long>(timeout), static_cast<const void*>(semaphore),
                  static_cast<const void*>(fence));
        VkResult result = s.on_vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence,
                                                     &pImageIndex_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pImageIndex_out);
        LSW_TRACE("server vkAcquireNextImageKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkQueuePresentKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkQueue queue = VK_NULL_HANDLE;
        VkPresentInfoKHR pPresentInfo_local{};
        if (!deserialize_VkQueue(r, queue))
            return false;
        if (!deserialize_VkPresentInfoKHR(r, &pPresentInfo_local, arena))
            return false;
        LSW_TRACE("server vkQueuePresentKHR(queue={}, pPresentInfo=ptr)",
                  static_cast<const void*>(queue));
        VkResult result = s.on_vkQueuePresentKHR(queue, &pPresentInfo_local);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkQueuePresentKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateGraphicsPipelines: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        uint32_t createInfoCount{};
        std::vector<VkGraphicsPipelineCreateInfo> pCreateInfos_storage;
        std::vector<VkPipeline> pPipelines_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPipelineCache(r, pipelineCache))
            return false;
        if (!r.read_pod(createInfoCount))
            return false;
        pCreateInfos_storage.resize(static_cast<std::size_t>(createInfoCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(createInfoCount); ++__i) {
            if (!deserialize_VkGraphicsPipelineCreateInfo(r, &pCreateInfos_storage[__i], arena))
                return false;
        }
        pPipelines_storage.resize(static_cast<std::size_t>(createInfoCount));
        LSW_TRACE("server vkCreateGraphicsPipelines(device={}, pipelineCache={}, "
                  "createInfoCount={}, pCreateInfos=ptr, pAllocator=ptr, pPipelines=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(pipelineCache),
                  static_cast<long long>(createInfoCount));
        VkResult result = s.on_vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount,
                                                         pCreateInfos_storage.data(), nullptr,
                                                         pPipelines_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        for (std::uint32_t __k = 0; __k < pPipelines_storage.size(); ++__k) {
            serialize_VkPipeline(reply, pPipelines_storage[__k]);
        }
        LSW_TRACE("server vkCreateGraphicsPipelines -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkUpdateDescriptorSets: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t descriptorWriteCount{};
        std::vector<VkWriteDescriptorSet> pDescriptorWrites_storage;
        uint32_t descriptorCopyCount{};
        std::vector<VkCopyDescriptorSet> pDescriptorCopies_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(descriptorWriteCount))
            return false;
        pDescriptorWrites_storage.resize(static_cast<std::size_t>(descriptorWriteCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(descriptorWriteCount); ++__i) {
            if (!deserialize_VkWriteDescriptorSet(r, &pDescriptorWrites_storage[__i], arena))
                return false;
        }
        if (!r.read_pod(descriptorCopyCount))
            return false;
        pDescriptorCopies_storage.resize(static_cast<std::size_t>(descriptorCopyCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(descriptorCopyCount); ++__i) {
            if (!deserialize_VkCopyDescriptorSet(r, &pDescriptorCopies_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkUpdateDescriptorSets(device={}, descriptorWriteCount={}, "
                  "pDescriptorWrites=ptr, descriptorCopyCount={}, pDescriptorCopies=ptr)",
                  static_cast<const void*>(device), static_cast<long long>(descriptorWriteCount),
                  static_cast<long long>(descriptorCopyCount));
        s.on_vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites_storage.data(),
                                    descriptorCopyCount, pDescriptorCopies_storage.data());
        LSW_TRACE("server vkUpdateDescriptorSets -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkAllocateCommandBuffers: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo pAllocateInfo_local{};
        std::vector<VkCommandBuffer> pCommandBuffers_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkCommandBufferAllocateInfo(r, &pAllocateInfo_local, arena))
            return false;
        pCommandBuffers_storage.resize(
            static_cast<std::size_t>(pAllocateInfo_local.commandBufferCount));
        LSW_TRACE(
            "server vkAllocateCommandBuffers(device={}, pAllocateInfo=ptr, pCommandBuffers=ptr)",
            static_cast<const void*>(device));
        VkResult result = s.on_vkAllocateCommandBuffers(device, &pAllocateInfo_local,
                                                        pCommandBuffers_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        for (std::uint32_t __k = 0; __k < pCommandBuffers_storage.size(); ++__k) {
            serialize_VkCommandBuffer(reply, pCommandBuffers_storage[__k]);
        }
        LSW_TRACE("server vkAllocateCommandBuffers -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkAllocateDescriptorSets: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo pAllocateInfo_local{};
        std::vector<VkDescriptorSet> pDescriptorSets_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorSetAllocateInfo(r, &pAllocateInfo_local, arena))
            return false;
        pDescriptorSets_storage.resize(
            static_cast<std::size_t>(pAllocateInfo_local.descriptorSetCount));
        LSW_TRACE(
            "server vkAllocateDescriptorSets(device={}, pAllocateInfo=ptr, pDescriptorSets=ptr)",
            static_cast<const void*>(device));
        VkResult result = s.on_vkAllocateDescriptorSets(device, &pAllocateInfo_local,
                                                        pDescriptorSets_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        for (std::uint32_t __k = 0; __k < pDescriptorSets_storage.size(); ++__k) {
            serialize_VkDescriptorSet(reply, pDescriptorSets_storage[__k]);
        }
        LSW_TRACE("server vkAllocateDescriptorSets -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkFreeDescriptorSets: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        uint32_t descriptorSetCount{};
        std::vector<VkDescriptorSet> pDescriptorSets_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorPool(r, descriptorPool))
            return false;
        if (!r.read_pod(descriptorSetCount))
            return false;
        pDescriptorSets_storage.resize(static_cast<std::size_t>(descriptorSetCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(descriptorSetCount); ++__i) {
            if (!deserialize_VkDescriptorSet(r, pDescriptorSets_storage[__i]))
                return false;
        }
        LSW_TRACE("server vkFreeDescriptorSets(device={}, descriptorPool={}, "
                  "descriptorSetCount={}, pDescriptorSets=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(descriptorPool),
                  static_cast<long long>(descriptorSetCount));
        VkResult result = s.on_vkFreeDescriptorSets(device, descriptorPool, descriptorSetCount,
                                                    pDescriptorSets_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkFreeDescriptorSets -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateComputePipelines: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        uint32_t createInfoCount{};
        std::vector<VkComputePipelineCreateInfo> pCreateInfos_storage;
        std::vector<VkPipeline> pPipelines_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPipelineCache(r, pipelineCache))
            return false;
        if (!r.read_pod(createInfoCount))
            return false;
        pCreateInfos_storage.resize(static_cast<std::size_t>(createInfoCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(createInfoCount); ++__i) {
            if (!deserialize_VkComputePipelineCreateInfo(r, &pCreateInfos_storage[__i], arena))
                return false;
        }
        pPipelines_storage.resize(static_cast<std::size_t>(createInfoCount));
        LSW_TRACE("server vkCreateComputePipelines(device={}, pipelineCache={}, "
                  "createInfoCount={}, pCreateInfos=ptr, pAllocator=ptr, pPipelines=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(pipelineCache),
                  static_cast<long long>(createInfoCount));
        VkResult result = s.on_vkCreateComputePipelines(device, pipelineCache, createInfoCount,
                                                        pCreateInfos_storage.data(), nullptr,
                                                        pPipelines_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        for (std::uint32_t __k = 0; __k < pPipelines_storage.size(); ++__k) {
            serialize_VkPipeline(reply, pPipelines_storage[__k]);
        }
        LSW_TRACE("server vkCreateComputePipelines -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkResetDescriptorPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorPoolResetFlags flags{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorPool(r, descriptorPool))
            return false;
        if (!r.read_pod(flags))
            return false;
        LSW_TRACE("server vkResetDescriptorPool(device={}, descriptorPool={}, flags={})",
                  static_cast<const void*>(device), static_cast<const void*>(descriptorPool),
                  static_cast<long long>(flags));
        VkResult result = s.on_vkResetDescriptorPool(device, descriptorPool, flags);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkResetDescriptorPool -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateEvent: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkEventCreateInfo pCreateInfo_local{};
        VkEvent pEvent_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkEventCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateEvent(device={}, pCreateInfo=ptr, pAllocator=ptr, pEvent=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkCreateEvent(device, &pCreateInfo_local, nullptr, &pEvent_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkEvent(reply, pEvent_out);
        LSW_TRACE("server vkCreateEvent -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyEvent: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkEvent event = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkEvent(r, event))
            return false;
        LSW_TRACE("server vkDestroyEvent(device={}, event={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(event));
        s.on_vkDestroyEvent(device, event, nullptr);
        LSW_TRACE("server vkDestroyEvent -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetEventStatus: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkEvent event = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkEvent(r, event))
            return false;
        LSW_TRACE("server vkGetEventStatus(device={}, event={})", static_cast<const void*>(device),
                  static_cast<const void*>(event));
        VkResult result = s.on_vkGetEventStatus(device, event);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkGetEventStatus -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkSetEvent: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkEvent event = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkEvent(r, event))
            return false;
        LSW_TRACE("server vkSetEvent(device={}, event={})", static_cast<const void*>(device),
                  static_cast<const void*>(event));
        VkResult result = s.on_vkSetEvent(device, event);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkSetEvent -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkResetEvent: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkEvent event = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkEvent(r, event))
            return false;
        LSW_TRACE("server vkResetEvent(device={}, event={})", static_cast<const void*>(device),
                  static_cast<const void*>(event));
        VkResult result = s.on_vkResetEvent(device, event);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkResetEvent -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetEvent: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkEvent event = VK_NULL_HANDLE;
        VkPipelineStageFlags stageMask{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkEvent(r, event))
            return false;
        if (!r.read_pod(stageMask))
            return false;
        LSW_TRACE("server vkCmdSetEvent(commandBuffer={}, event={}, stageMask={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(event),
                  static_cast<long long>(stageMask));
        s.on_vkCmdSetEvent(commandBuffer, event, stageMask);
        LSW_TRACE("server vkCmdSetEvent -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdResetEvent: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkEvent event = VK_NULL_HANDLE;
        VkPipelineStageFlags stageMask{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkEvent(r, event))
            return false;
        if (!r.read_pod(stageMask))
            return false;
        LSW_TRACE("server vkCmdResetEvent(commandBuffer={}, event={}, stageMask={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(event),
                  static_cast<long long>(stageMask));
        s.on_vkCmdResetEvent(commandBuffer, event, stageMask);
        LSW_TRACE("server vkCmdResetEvent -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdWaitEvents: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t eventCount{};
        std::vector<VkEvent> pEvents_storage;
        VkPipelineStageFlags srcStageMask{};
        VkPipelineStageFlags dstStageMask{};
        uint32_t memoryBarrierCount{};
        std::vector<VkMemoryBarrier> pMemoryBarriers_storage;
        uint32_t bufferMemoryBarrierCount{};
        std::vector<VkBufferMemoryBarrier> pBufferMemoryBarriers_storage;
        uint32_t imageMemoryBarrierCount{};
        std::vector<VkImageMemoryBarrier> pImageMemoryBarriers_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(eventCount))
            return false;
        pEvents_storage.resize(static_cast<std::size_t>(eventCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(eventCount); ++__i) {
            if (!deserialize_VkEvent(r, pEvents_storage[__i]))
                return false;
        }
        if (!r.read_pod(srcStageMask))
            return false;
        if (!r.read_pod(dstStageMask))
            return false;
        if (!r.read_pod(memoryBarrierCount))
            return false;
        pMemoryBarriers_storage.resize(static_cast<std::size_t>(memoryBarrierCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(memoryBarrierCount); ++__i) {
            if (!deserialize_VkMemoryBarrier(r, &pMemoryBarriers_storage[__i], arena))
                return false;
        }
        if (!r.read_pod(bufferMemoryBarrierCount))
            return false;
        pBufferMemoryBarriers_storage.resize(static_cast<std::size_t>(bufferMemoryBarrierCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bufferMemoryBarrierCount);
             ++__i) {
            if (!deserialize_VkBufferMemoryBarrier(r, &pBufferMemoryBarriers_storage[__i], arena))
                return false;
        }
        if (!r.read_pod(imageMemoryBarrierCount))
            return false;
        pImageMemoryBarriers_storage.resize(static_cast<std::size_t>(imageMemoryBarrierCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(imageMemoryBarrierCount);
             ++__i) {
            if (!deserialize_VkImageMemoryBarrier(r, &pImageMemoryBarriers_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdWaitEvents(commandBuffer={}, eventCount={}, pEvents=ptr, "
                  "srcStageMask={}, dstStageMask={}, memoryBarrierCount={}, pMemoryBarriers=ptr, "
                  "bufferMemoryBarrierCount={}, pBufferMemoryBarriers=ptr, "
                  "imageMemoryBarrierCount={}, pImageMemoryBarriers=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(eventCount),
                  static_cast<long long>(srcStageMask), static_cast<long long>(dstStageMask),
                  static_cast<long long>(memoryBarrierCount),
                  static_cast<long long>(bufferMemoryBarrierCount),
                  static_cast<long long>(imageMemoryBarrierCount));
        s.on_vkCmdWaitEvents(commandBuffer, eventCount, pEvents_storage.data(), srcStageMask,
                             dstStageMask, memoryBarrierCount, pMemoryBarriers_storage.data(),
                             bufferMemoryBarrierCount, pBufferMemoryBarriers_storage.data(),
                             imageMemoryBarrierCount, pImageMemoryBarriers_storage.data());
        LSW_TRACE("server vkCmdWaitEvents -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyImage: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkImage srcImage = VK_NULL_HANDLE;
        VkImageLayout srcImageLayout{};
        VkImage dstImage = VK_NULL_HANDLE;
        VkImageLayout dstImageLayout{};
        uint32_t regionCount{};
        std::vector<VkImageCopy> pRegions_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkImage(r, srcImage))
            return false;
        if (!r.read_pod(srcImageLayout))
            return false;
        if (!deserialize_VkImage(r, dstImage))
            return false;
        if (!r.read_pod(dstImageLayout))
            return false;
        if (!r.read_pod(regionCount))
            return false;
        pRegions_storage.resize(static_cast<std::size_t>(regionCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
            if (!deserialize_VkImageCopy(r, &pRegions_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdCopyImage(commandBuffer={}, srcImage={}, srcImageLayout={}, "
                  "dstImage={}, dstImageLayout={}, regionCount={}, pRegions=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(srcImage),
                  static_cast<long long>(srcImageLayout), static_cast<const void*>(dstImage),
                  static_cast<long long>(dstImageLayout), static_cast<long long>(regionCount));
        s.on_vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                            regionCount, pRegions_storage.data());
        LSW_TRACE("server vkCmdCopyImage -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyBufferToImage: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer srcBuffer = VK_NULL_HANDLE;
        VkImage dstImage = VK_NULL_HANDLE;
        VkImageLayout dstImageLayout{};
        uint32_t regionCount{};
        std::vector<VkBufferImageCopy> pRegions_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, srcBuffer))
            return false;
        if (!deserialize_VkImage(r, dstImage))
            return false;
        if (!r.read_pod(dstImageLayout))
            return false;
        if (!r.read_pod(regionCount))
            return false;
        pRegions_storage.resize(static_cast<std::size_t>(regionCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
            if (!deserialize_VkBufferImageCopy(r, &pRegions_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdCopyBufferToImage(commandBuffer={}, srcBuffer={}, dstImage={}, "
                  "dstImageLayout={}, regionCount={}, pRegions=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(srcBuffer),
                  static_cast<const void*>(dstImage), static_cast<long long>(dstImageLayout),
                  static_cast<long long>(regionCount));
        s.on_vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount,
                                    pRegions_storage.data());
        LSW_TRACE("server vkCmdCopyBufferToImage -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyImageToBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkImage srcImage = VK_NULL_HANDLE;
        VkImageLayout srcImageLayout{};
        VkBuffer dstBuffer = VK_NULL_HANDLE;
        uint32_t regionCount{};
        std::vector<VkBufferImageCopy> pRegions_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkImage(r, srcImage))
            return false;
        if (!r.read_pod(srcImageLayout))
            return false;
        if (!deserialize_VkBuffer(r, dstBuffer))
            return false;
        if (!r.read_pod(regionCount))
            return false;
        pRegions_storage.resize(static_cast<std::size_t>(regionCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
            if (!deserialize_VkBufferImageCopy(r, &pRegions_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdCopyImageToBuffer(commandBuffer={}, srcImage={}, srcImageLayout={}, "
                  "dstBuffer={}, regionCount={}, pRegions=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(srcImage),
                  static_cast<long long>(srcImageLayout), static_cast<const void*>(dstBuffer),
                  static_cast<long long>(regionCount));
        s.on_vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount,
                                    pRegions_storage.data());
        LSW_TRACE("server vkCmdCopyImageToBuffer -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBlitImage: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkImage srcImage = VK_NULL_HANDLE;
        VkImageLayout srcImageLayout{};
        VkImage dstImage = VK_NULL_HANDLE;
        VkImageLayout dstImageLayout{};
        uint32_t regionCount{};
        std::vector<VkImageBlit> pRegions_storage;
        VkFilter filter{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkImage(r, srcImage))
            return false;
        if (!r.read_pod(srcImageLayout))
            return false;
        if (!deserialize_VkImage(r, dstImage))
            return false;
        if (!r.read_pod(dstImageLayout))
            return false;
        if (!r.read_pod(regionCount))
            return false;
        pRegions_storage.resize(static_cast<std::size_t>(regionCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
            if (!deserialize_VkImageBlit(r, &pRegions_storage[__i], arena))
                return false;
        }
        if (!r.read_pod(filter))
            return false;
        LSW_TRACE("server vkCmdBlitImage(commandBuffer={}, srcImage={}, srcImageLayout={}, "
                  "dstImage={}, dstImageLayout={}, regionCount={}, pRegions=ptr, filter={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(srcImage),
                  static_cast<long long>(srcImageLayout), static_cast<const void*>(dstImage),
                  static_cast<long long>(dstImageLayout), static_cast<long long>(regionCount),
                  static_cast<long long>(filter));
        s.on_vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                            regionCount, pRegions_storage.data(), filter);
        LSW_TRACE("server vkCmdBlitImage -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdClearColorImage: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageLayout imageLayout{};
        VkClearColorValue pColor_local{};
        uint32_t rangeCount{};
        std::vector<VkImageSubresourceRange> pRanges_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkImage(r, image))
            return false;
        if (!r.read_pod(imageLayout))
            return false;
        if (!deserialize_VkClearColorValue(r, &pColor_local, arena))
            return false;
        if (!r.read_pod(rangeCount))
            return false;
        pRanges_storage.resize(static_cast<std::size_t>(rangeCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(rangeCount); ++__i) {
            if (!deserialize_VkImageSubresourceRange(r, &pRanges_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdClearColorImage(commandBuffer={}, image={}, imageLayout={}, "
                  "pColor=ptr, rangeCount={}, pRanges=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(image),
                  static_cast<long long>(imageLayout), static_cast<long long>(rangeCount));
        s.on_vkCmdClearColorImage(commandBuffer, image, imageLayout, &pColor_local, rangeCount,
                                  pRanges_storage.data());
        LSW_TRACE("server vkCmdClearColorImage -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdClearDepthStencilImage: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageLayout imageLayout{};
        VkClearDepthStencilValue pDepthStencil_local{};
        uint32_t rangeCount{};
        std::vector<VkImageSubresourceRange> pRanges_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkImage(r, image))
            return false;
        if (!r.read_pod(imageLayout))
            return false;
        if (!deserialize_VkClearDepthStencilValue(r, &pDepthStencil_local, arena))
            return false;
        if (!r.read_pod(rangeCount))
            return false;
        pRanges_storage.resize(static_cast<std::size_t>(rangeCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(rangeCount); ++__i) {
            if (!deserialize_VkImageSubresourceRange(r, &pRanges_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdClearDepthStencilImage(commandBuffer={}, image={}, imageLayout={}, "
                  "pDepthStencil=ptr, rangeCount={}, pRanges=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(image),
                  static_cast<long long>(imageLayout), static_cast<long long>(rangeCount));
        s.on_vkCmdClearDepthStencilImage(commandBuffer, image, imageLayout, &pDepthStencil_local,
                                         rangeCount, pRanges_storage.data());
        LSW_TRACE("server vkCmdClearDepthStencilImage -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdClearAttachments: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t attachmentCount{};
        std::vector<VkClearAttachment> pAttachments_storage;
        uint32_t rectCount{};
        std::vector<VkClearRect> pRects_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(attachmentCount))
            return false;
        pAttachments_storage.resize(static_cast<std::size_t>(attachmentCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(attachmentCount); ++__i) {
            if (!deserialize_VkClearAttachment(r, &pAttachments_storage[__i], arena))
                return false;
        }
        if (!r.read_pod(rectCount))
            return false;
        pRects_storage.resize(static_cast<std::size_t>(rectCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(rectCount); ++__i) {
            if (!deserialize_VkClearRect(r, &pRects_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdClearAttachments(commandBuffer={}, attachmentCount={}, "
                  "pAttachments=ptr, rectCount={}, pRects=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(attachmentCount),
                  static_cast<long long>(rectCount));
        s.on_vkCmdClearAttachments(commandBuffer, attachmentCount, pAttachments_storage.data(),
                                   rectCount, pRects_storage.data());
        LSW_TRACE("server vkCmdClearAttachments -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdFillBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer dstBuffer = VK_NULL_HANDLE;
        VkDeviceSize dstOffset{};
        VkDeviceSize size{};
        uint32_t data{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, dstBuffer))
            return false;
        if (!r.read_pod(dstOffset))
            return false;
        if (!r.read_pod(size))
            return false;
        if (!r.read_pod(data))
            return false;
        LSW_TRACE("server vkCmdFillBuffer(commandBuffer={}, dstBuffer={}, dstOffset={}, size={}, "
                  "data={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(dstBuffer),
                  static_cast<long long>(dstOffset), static_cast<long long>(size),
                  static_cast<long long>(data));
        s.on_vkCmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
        LSW_TRACE("server vkCmdFillBuffer -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdUpdateBuffer: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer dstBuffer = VK_NULL_HANDLE;
        VkDeviceSize dstOffset{};
        VkDeviceSize dataSize{};
        std::vector<std::uint8_t> pData_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, dstBuffer))
            return false;
        if (!r.read_pod(dstOffset))
            return false;
        if (!r.read_pod(dataSize))
            return false;
        if (dataSize > 0) {
            pData_storage.resize(static_cast<std::size_t>(dataSize));
            if (!r.read(pData_storage.data(), static_cast<std::size_t>(dataSize)))
                return false;
        }
        LSW_TRACE("server vkCmdUpdateBuffer(commandBuffer={}, dstBuffer={}, dstOffset={}, "
                  "dataSize={}, pData=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(dstBuffer),
                  static_cast<long long>(dstOffset), static_cast<long long>(dataSize));
        s.on_vkCmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, dataSize, pData_storage.data());
        LSW_TRACE("server vkCmdUpdateBuffer -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetLineWidth: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        float lineWidth{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(lineWidth))
            return false;
        LSW_TRACE("server vkCmdSetLineWidth(commandBuffer={}, lineWidth={})",
                  static_cast<const void*>(commandBuffer), lineWidth);
        s.on_vkCmdSetLineWidth(commandBuffer, lineWidth);
        LSW_TRACE("server vkCmdSetLineWidth -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetDepthBias: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        float depthBiasConstantFactor{};
        float depthBiasClamp{};
        float depthBiasSlopeFactor{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(depthBiasConstantFactor))
            return false;
        if (!r.read_pod(depthBiasClamp))
            return false;
        if (!r.read_pod(depthBiasSlopeFactor))
            return false;
        LSW_TRACE("server vkCmdSetDepthBias(commandBuffer={}, depthBiasConstantFactor={}, "
                  "depthBiasClamp={}, depthBiasSlopeFactor={})",
                  static_cast<const void*>(commandBuffer), depthBiasConstantFactor, depthBiasClamp,
                  depthBiasSlopeFactor);
        s.on_vkCmdSetDepthBias(commandBuffer, depthBiasConstantFactor, depthBiasClamp,
                               depthBiasSlopeFactor);
        LSW_TRACE("server vkCmdSetDepthBias -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetBlendConstants: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        float blendConstants_local[4]{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        for (std::uint32_t __i = 0; __i < 4u; ++__i) {
            if (!r.read_pod(blendConstants_local[__i]))
                return false;
        }
        LSW_TRACE("server vkCmdSetBlendConstants(commandBuffer={}, blendConstants=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdSetBlendConstants(commandBuffer, blendConstants_local);
        LSW_TRACE("server vkCmdSetBlendConstants -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetDepthBounds: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        float minDepthBounds{};
        float maxDepthBounds{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(minDepthBounds))
            return false;
        if (!r.read_pod(maxDepthBounds))
            return false;
        LSW_TRACE(
            "server vkCmdSetDepthBounds(commandBuffer={}, minDepthBounds={}, maxDepthBounds={})",
            static_cast<const void*>(commandBuffer), minDepthBounds, maxDepthBounds);
        s.on_vkCmdSetDepthBounds(commandBuffer, minDepthBounds, maxDepthBounds);
        LSW_TRACE("server vkCmdSetDepthBounds -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetStencilCompareMask: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkStencilFaceFlags faceMask{};
        uint32_t compareMask{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(faceMask))
            return false;
        if (!r.read_pod(compareMask))
            return false;
        LSW_TRACE(
            "server vkCmdSetStencilCompareMask(commandBuffer={}, faceMask={}, compareMask={})",
            static_cast<const void*>(commandBuffer), static_cast<long long>(faceMask),
            static_cast<long long>(compareMask));
        s.on_vkCmdSetStencilCompareMask(commandBuffer, faceMask, compareMask);
        LSW_TRACE("server vkCmdSetStencilCompareMask -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetStencilWriteMask: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkStencilFaceFlags faceMask{};
        uint32_t writeMask{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(faceMask))
            return false;
        if (!r.read_pod(writeMask))
            return false;
        LSW_TRACE("server vkCmdSetStencilWriteMask(commandBuffer={}, faceMask={}, writeMask={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(faceMask),
                  static_cast<long long>(writeMask));
        s.on_vkCmdSetStencilWriteMask(commandBuffer, faceMask, writeMask);
        LSW_TRACE("server vkCmdSetStencilWriteMask -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetStencilReference: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkStencilFaceFlags faceMask{};
        uint32_t reference{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(faceMask))
            return false;
        if (!r.read_pod(reference))
            return false;
        LSW_TRACE("server vkCmdSetStencilReference(commandBuffer={}, faceMask={}, reference={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(faceMask),
                  static_cast<long long>(reference));
        s.on_vkCmdSetStencilReference(commandBuffer, faceMask, reference);
        LSW_TRACE("server vkCmdSetStencilReference -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDispatch: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t groupCountX{};
        uint32_t groupCountY{};
        uint32_t groupCountZ{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(groupCountX))
            return false;
        if (!r.read_pod(groupCountY))
            return false;
        if (!r.read_pod(groupCountZ))
            return false;
        LSW_TRACE("server vkCmdDispatch(commandBuffer={}, groupCountX={}, groupCountY={}, "
                  "groupCountZ={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(groupCountX),
                  static_cast<long long>(groupCountY), static_cast<long long>(groupCountZ));
        s.on_vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
        LSW_TRACE("server vkCmdDispatch -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDispatchIndirect: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        if (!r.read_pod(offset))
            return false;
        LSW_TRACE("server vkCmdDispatchIndirect(commandBuffer={}, buffer={}, offset={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer),
                  static_cast<long long>(offset));
        s.on_vkCmdDispatchIndirect(commandBuffer, buffer, offset);
        LSW_TRACE("server vkCmdDispatchIndirect -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDrawIndirect: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset{};
        uint32_t drawCount{};
        uint32_t stride{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        if (!r.read_pod(offset))
            return false;
        if (!r.read_pod(drawCount))
            return false;
        if (!r.read_pod(stride))
            return false;
        LSW_TRACE("server vkCmdDrawIndirect(commandBuffer={}, buffer={}, offset={}, drawCount={}, "
                  "stride={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer),
                  static_cast<long long>(offset), static_cast<long long>(drawCount),
                  static_cast<long long>(stride));
        s.on_vkCmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
        LSW_TRACE("server vkCmdDrawIndirect -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDrawIndexedIndirect: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset{};
        uint32_t drawCount{};
        uint32_t stride{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        if (!r.read_pod(offset))
            return false;
        if (!r.read_pod(drawCount))
            return false;
        if (!r.read_pod(stride))
            return false;
        LSW_TRACE("server vkCmdDrawIndexedIndirect(commandBuffer={}, buffer={}, offset={}, "
                  "drawCount={}, stride={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer),
                  static_cast<long long>(offset), static_cast<long long>(drawCount),
                  static_cast<long long>(stride));
        s.on_vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
        LSW_TRACE("server vkCmdDrawIndexedIndirect -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdExecuteCommands: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t commandBufferCount{};
        std::vector<VkCommandBuffer> pCommandBuffers_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(commandBufferCount))
            return false;
        pCommandBuffers_storage.resize(static_cast<std::size_t>(commandBufferCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(commandBufferCount); ++__i) {
            if (!deserialize_VkCommandBuffer(r, pCommandBuffers_storage[__i]))
                return false;
        }
        LSW_TRACE("server vkCmdExecuteCommands(commandBuffer={}, commandBufferCount={}, "
                  "pCommandBuffers=ptr)",
                  static_cast<const void*>(commandBuffer),
                  static_cast<long long>(commandBufferCount));
        s.on_vkCmdExecuteCommands(commandBuffer, commandBufferCount,
                                  pCommandBuffers_storage.data());
        LSW_TRACE("server vkCmdExecuteCommands -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkAcquireNextImage2KHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkAcquireNextImageInfoKHR pAcquireInfo_local{};
        uint32_t pImageIndex_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkAcquireNextImageInfoKHR(r, &pAcquireInfo_local, arena))
            return false;
        LSW_TRACE("server vkAcquireNextImage2KHR(device={}, pAcquireInfo=ptr, pImageIndex=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkAcquireNextImage2KHR(device, &pAcquireInfo_local, &pImageIndex_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pImageIndex_out);
        LSW_TRACE("server vkAcquireNextImage2KHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkBindBufferMemory2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t bindInfoCount{};
        std::vector<VkBindBufferMemoryInfo> pBindInfos_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(bindInfoCount))
            return false;
        pBindInfos_storage.resize(static_cast<std::size_t>(bindInfoCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindInfoCount); ++__i) {
            if (!deserialize_VkBindBufferMemoryInfo(r, &pBindInfos_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkBindBufferMemory2(device={}, bindInfoCount={}, pBindInfos=ptr)",
                  static_cast<const void*>(device), static_cast<long long>(bindInfoCount));
        VkResult result =
            s.on_vkBindBufferMemory2(device, bindInfoCount, pBindInfos_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkBindBufferMemory2 -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkBindImageMemory2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t bindInfoCount{};
        std::vector<VkBindImageMemoryInfo> pBindInfos_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(bindInfoCount))
            return false;
        pBindInfos_storage.resize(static_cast<std::size_t>(bindInfoCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindInfoCount); ++__i) {
            if (!deserialize_VkBindImageMemoryInfo(r, &pBindInfos_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkBindImageMemory2(device={}, bindInfoCount={}, pBindInfos=ptr)",
                  static_cast<const void*>(device), static_cast<long long>(bindInfoCount));
        VkResult result = s.on_vkBindImageMemory2(device, bindInfoCount, pBindInfos_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkBindImageMemory2 -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBeginQuery: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t query{};
        VkQueryControlFlags flags{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkQueryPool(r, queryPool))
            return false;
        if (!r.read_pod(query))
            return false;
        if (!r.read_pod(flags))
            return false;
        LSW_TRACE("server vkCmdBeginQuery(commandBuffer={}, queryPool={}, query={}, flags={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(queryPool),
                  static_cast<long long>(query), static_cast<long long>(flags));
        s.on_vkCmdBeginQuery(commandBuffer, queryPool, query, flags);
        LSW_TRACE("server vkCmdBeginQuery -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBeginRenderPass2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkRenderPassBeginInfo pRenderPassBegin_local{};
        VkSubpassBeginInfo pSubpassBeginInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkRenderPassBeginInfo(r, &pRenderPassBegin_local, arena))
            return false;
        if (!deserialize_VkSubpassBeginInfo(r, &pSubpassBeginInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdBeginRenderPass2(commandBuffer={}, pRenderPassBegin=ptr, "
                  "pSubpassBeginInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdBeginRenderPass2(commandBuffer, &pRenderPassBegin_local,
                                   &pSubpassBeginInfo_local);
        LSW_TRACE("server vkCmdBeginRenderPass2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBeginRendering: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkRenderingInfo pRenderingInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkRenderingInfo(r, &pRenderingInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdBeginRendering(commandBuffer={}, pRenderingInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdBeginRendering(commandBuffer, &pRenderingInfo_local);
        LSW_TRACE("server vkCmdBeginRendering -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBindVertexBuffers2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t firstBinding{};
        uint32_t bindingCount{};
        std::vector<VkBuffer> pBuffers_storage;
        std::vector<VkDeviceSize> pOffsets_storage;
        std::vector<VkDeviceSize> pSizes_storage;
        std::vector<VkDeviceSize> pStrides_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(firstBinding))
            return false;
        if (!r.read_pod(bindingCount))
            return false;
        pBuffers_storage.resize(static_cast<std::size_t>(bindingCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
            if (!deserialize_VkBuffer(r, pBuffers_storage[__i]))
                return false;
        }
        pOffsets_storage.resize(static_cast<std::size_t>(bindingCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
            if (!r.read_pod(pOffsets_storage[__i]))
                return false;
        }
        pSizes_storage.resize(static_cast<std::size_t>(bindingCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
            if (!r.read_pod(pSizes_storage[__i]))
                return false;
        }
        pStrides_storage.resize(static_cast<std::size_t>(bindingCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindingCount); ++__i) {
            if (!r.read_pod(pStrides_storage[__i]))
                return false;
        }
        LSW_TRACE("server vkCmdBindVertexBuffers2(commandBuffer={}, firstBinding={}, "
                  "bindingCount={}, pBuffers=ptr, pOffsets=ptr, pSizes=ptr, pStrides=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(firstBinding),
                  static_cast<long long>(bindingCount));
        s.on_vkCmdBindVertexBuffers2(commandBuffer, firstBinding, bindingCount,
                                     pBuffers_storage.data(), pOffsets_storage.data(),
                                     pSizes_storage.data(), pStrides_storage.data());
        LSW_TRACE("server vkCmdBindVertexBuffers2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdBlitImage2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBlitImageInfo2 pBlitImageInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBlitImageInfo2(r, &pBlitImageInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdBlitImage2(commandBuffer={}, pBlitImageInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdBlitImage2(commandBuffer, &pBlitImageInfo_local);
        LSW_TRACE("server vkCmdBlitImage2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyBuffer2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCopyBufferInfo2 pCopyBufferInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkCopyBufferInfo2(r, &pCopyBufferInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdCopyBuffer2(commandBuffer={}, pCopyBufferInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdCopyBuffer2(commandBuffer, &pCopyBufferInfo_local);
        LSW_TRACE("server vkCmdCopyBuffer2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyBufferToImage2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCopyBufferToImageInfo2 pCopyBufferToImageInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkCopyBufferToImageInfo2(r, &pCopyBufferToImageInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdCopyBufferToImage2(commandBuffer={}, pCopyBufferToImageInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdCopyBufferToImage2(commandBuffer, &pCopyBufferToImageInfo_local);
        LSW_TRACE("server vkCmdCopyBufferToImage2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyImage2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCopyImageInfo2 pCopyImageInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkCopyImageInfo2(r, &pCopyImageInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdCopyImage2(commandBuffer={}, pCopyImageInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdCopyImage2(commandBuffer, &pCopyImageInfo_local);
        LSW_TRACE("server vkCmdCopyImage2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyImageToBuffer2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCopyImageToBufferInfo2 pCopyImageToBufferInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkCopyImageToBufferInfo2(r, &pCopyImageToBufferInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdCopyImageToBuffer2(commandBuffer={}, pCopyImageToBufferInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdCopyImageToBuffer2(commandBuffer, &pCopyImageToBufferInfo_local);
        LSW_TRACE("server vkCmdCopyImageToBuffer2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdCopyQueryPoolResults: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t firstQuery{};
        uint32_t queryCount{};
        VkBuffer dstBuffer = VK_NULL_HANDLE;
        VkDeviceSize dstOffset{};
        VkDeviceSize stride{};
        VkQueryResultFlags flags{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkQueryPool(r, queryPool))
            return false;
        if (!r.read_pod(firstQuery))
            return false;
        if (!r.read_pod(queryCount))
            return false;
        if (!deserialize_VkBuffer(r, dstBuffer))
            return false;
        if (!r.read_pod(dstOffset))
            return false;
        if (!r.read_pod(stride))
            return false;
        if (!r.read_pod(flags))
            return false;
        LSW_TRACE("server vkCmdCopyQueryPoolResults(commandBuffer={}, queryPool={}, firstQuery={}, "
                  "queryCount={}, dstBuffer={}, dstOffset={}, stride={}, flags={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(queryPool),
                  static_cast<long long>(firstQuery), static_cast<long long>(queryCount),
                  static_cast<const void*>(dstBuffer), static_cast<long long>(dstOffset),
                  static_cast<long long>(stride), static_cast<long long>(flags));
        s.on_vkCmdCopyQueryPoolResults(commandBuffer, queryPool, firstQuery, queryCount, dstBuffer,
                                       dstOffset, stride, flags);
        LSW_TRACE("server vkCmdCopyQueryPoolResults -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDispatchBase: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t baseGroupX{};
        uint32_t baseGroupY{};
        uint32_t baseGroupZ{};
        uint32_t groupCountX{};
        uint32_t groupCountY{};
        uint32_t groupCountZ{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(baseGroupX))
            return false;
        if (!r.read_pod(baseGroupY))
            return false;
        if (!r.read_pod(baseGroupZ))
            return false;
        if (!r.read_pod(groupCountX))
            return false;
        if (!r.read_pod(groupCountY))
            return false;
        if (!r.read_pod(groupCountZ))
            return false;
        LSW_TRACE("server vkCmdDispatchBase(commandBuffer={}, baseGroupX={}, baseGroupY={}, "
                  "baseGroupZ={}, groupCountX={}, groupCountY={}, groupCountZ={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(baseGroupX),
                  static_cast<long long>(baseGroupY), static_cast<long long>(baseGroupZ),
                  static_cast<long long>(groupCountX), static_cast<long long>(groupCountY),
                  static_cast<long long>(groupCountZ));
        s.on_vkCmdDispatchBase(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX,
                               groupCountY, groupCountZ);
        LSW_TRACE("server vkCmdDispatchBase -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDrawIndexedIndirectCount: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset{};
        VkBuffer countBuffer = VK_NULL_HANDLE;
        VkDeviceSize countBufferOffset{};
        uint32_t maxDrawCount{};
        uint32_t stride{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        if (!r.read_pod(offset))
            return false;
        if (!deserialize_VkBuffer(r, countBuffer))
            return false;
        if (!r.read_pod(countBufferOffset))
            return false;
        if (!r.read_pod(maxDrawCount))
            return false;
        if (!r.read_pod(stride))
            return false;
        LSW_TRACE("server vkCmdDrawIndexedIndirectCount(commandBuffer={}, buffer={}, offset={}, "
                  "countBuffer={}, countBufferOffset={}, maxDrawCount={}, stride={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer),
                  static_cast<long long>(offset), static_cast<const void*>(countBuffer),
                  static_cast<long long>(countBufferOffset), static_cast<long long>(maxDrawCount),
                  static_cast<long long>(stride));
        s.on_vkCmdDrawIndexedIndirectCount(commandBuffer, buffer, offset, countBuffer,
                                           countBufferOffset, maxDrawCount, stride);
        LSW_TRACE("server vkCmdDrawIndexedIndirectCount -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdDrawIndirectCount: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset{};
        VkBuffer countBuffer = VK_NULL_HANDLE;
        VkDeviceSize countBufferOffset{};
        uint32_t maxDrawCount{};
        uint32_t stride{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkBuffer(r, buffer))
            return false;
        if (!r.read_pod(offset))
            return false;
        if (!deserialize_VkBuffer(r, countBuffer))
            return false;
        if (!r.read_pod(countBufferOffset))
            return false;
        if (!r.read_pod(maxDrawCount))
            return false;
        if (!r.read_pod(stride))
            return false;
        LSW_TRACE("server vkCmdDrawIndirectCount(commandBuffer={}, buffer={}, offset={}, "
                  "countBuffer={}, countBufferOffset={}, maxDrawCount={}, stride={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(buffer),
                  static_cast<long long>(offset), static_cast<const void*>(countBuffer),
                  static_cast<long long>(countBufferOffset), static_cast<long long>(maxDrawCount),
                  static_cast<long long>(stride));
        s.on_vkCmdDrawIndirectCount(commandBuffer, buffer, offset, countBuffer, countBufferOffset,
                                    maxDrawCount, stride);
        LSW_TRACE("server vkCmdDrawIndirectCount -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdEndQuery: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t query{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkQueryPool(r, queryPool))
            return false;
        if (!r.read_pod(query))
            return false;
        LSW_TRACE("server vkCmdEndQuery(commandBuffer={}, queryPool={}, query={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(queryPool),
                  static_cast<long long>(query));
        s.on_vkCmdEndQuery(commandBuffer, queryPool, query);
        LSW_TRACE("server vkCmdEndQuery -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdEndRenderPass2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSubpassEndInfo pSubpassEndInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkSubpassEndInfo(r, &pSubpassEndInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdEndRenderPass2(commandBuffer={}, pSubpassEndInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdEndRenderPass2(commandBuffer, &pSubpassEndInfo_local);
        LSW_TRACE("server vkCmdEndRenderPass2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdEndRendering: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        LSW_TRACE("server vkCmdEndRendering(commandBuffer={})",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdEndRendering(commandBuffer);
        LSW_TRACE("server vkCmdEndRendering -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdNextSubpass2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSubpassBeginInfo pSubpassBeginInfo_local{};
        VkSubpassEndInfo pSubpassEndInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkSubpassBeginInfo(r, &pSubpassBeginInfo_local, arena))
            return false;
        if (!deserialize_VkSubpassEndInfo(r, &pSubpassEndInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdNextSubpass2(commandBuffer={}, pSubpassBeginInfo=ptr, "
                  "pSubpassEndInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdNextSubpass2(commandBuffer, &pSubpassBeginInfo_local, &pSubpassEndInfo_local);
        LSW_TRACE("server vkCmdNextSubpass2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdPipelineBarrier2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkDependencyInfo pDependencyInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkDependencyInfo(r, &pDependencyInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdPipelineBarrier2(commandBuffer={}, pDependencyInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdPipelineBarrier2(commandBuffer, &pDependencyInfo_local);
        LSW_TRACE("server vkCmdPipelineBarrier2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdResetEvent2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkEvent event = VK_NULL_HANDLE;
        VkPipelineStageFlags2 stageMask{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkEvent(r, event))
            return false;
        if (!r.read_pod(stageMask))
            return false;
        LSW_TRACE("server vkCmdResetEvent2(commandBuffer={}, event={}, stageMask={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(event),
                  static_cast<long long>(stageMask));
        s.on_vkCmdResetEvent2(commandBuffer, event, stageMask);
        LSW_TRACE("server vkCmdResetEvent2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdResetQueryPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t firstQuery{};
        uint32_t queryCount{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkQueryPool(r, queryPool))
            return false;
        if (!r.read_pod(firstQuery))
            return false;
        if (!r.read_pod(queryCount))
            return false;
        LSW_TRACE("server vkCmdResetQueryPool(commandBuffer={}, queryPool={}, firstQuery={}, "
                  "queryCount={})",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(queryPool),
                  static_cast<long long>(firstQuery), static_cast<long long>(queryCount));
        s.on_vkCmdResetQueryPool(commandBuffer, queryPool, firstQuery, queryCount);
        LSW_TRACE("server vkCmdResetQueryPool -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdResolveImage: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkImage srcImage = VK_NULL_HANDLE;
        VkImageLayout srcImageLayout{};
        VkImage dstImage = VK_NULL_HANDLE;
        VkImageLayout dstImageLayout{};
        uint32_t regionCount{};
        std::vector<VkImageResolve> pRegions_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkImage(r, srcImage))
            return false;
        if (!r.read_pod(srcImageLayout))
            return false;
        if (!deserialize_VkImage(r, dstImage))
            return false;
        if (!r.read_pod(dstImageLayout))
            return false;
        if (!r.read_pod(regionCount))
            return false;
        pRegions_storage.resize(static_cast<std::size_t>(regionCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(regionCount); ++__i) {
            if (!deserialize_VkImageResolve(r, &pRegions_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdResolveImage(commandBuffer={}, srcImage={}, srcImageLayout={}, "
                  "dstImage={}, dstImageLayout={}, regionCount={}, pRegions=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(srcImage),
                  static_cast<long long>(srcImageLayout), static_cast<const void*>(dstImage),
                  static_cast<long long>(dstImageLayout), static_cast<long long>(regionCount));
        s.on_vkCmdResolveImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                               regionCount, pRegions_storage.data());
        LSW_TRACE("server vkCmdResolveImage -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdResolveImage2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkResolveImageInfo2 pResolveImageInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkResolveImageInfo2(r, &pResolveImageInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdResolveImage2(commandBuffer={}, pResolveImageInfo=ptr)",
                  static_cast<const void*>(commandBuffer));
        s.on_vkCmdResolveImage2(commandBuffer, &pResolveImageInfo_local);
        LSW_TRACE("server vkCmdResolveImage2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetCullMode: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCullModeFlags cullMode{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(cullMode))
            return false;
        LSW_TRACE("server vkCmdSetCullMode(commandBuffer={}, cullMode={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(cullMode));
        s.on_vkCmdSetCullMode(commandBuffer, cullMode);
        LSW_TRACE("server vkCmdSetCullMode -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetDepthBiasEnable: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBool32 depthBiasEnable{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(depthBiasEnable))
            return false;
        LSW_TRACE("server vkCmdSetDepthBiasEnable(commandBuffer={}, depthBiasEnable={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(depthBiasEnable));
        s.on_vkCmdSetDepthBiasEnable(commandBuffer, depthBiasEnable);
        LSW_TRACE("server vkCmdSetDepthBiasEnable -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetDepthBoundsTestEnable: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBool32 depthBoundsTestEnable{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(depthBoundsTestEnable))
            return false;
        LSW_TRACE(
            "server vkCmdSetDepthBoundsTestEnable(commandBuffer={}, depthBoundsTestEnable={})",
            static_cast<const void*>(commandBuffer), static_cast<long long>(depthBoundsTestEnable));
        s.on_vkCmdSetDepthBoundsTestEnable(commandBuffer, depthBoundsTestEnable);
        LSW_TRACE("server vkCmdSetDepthBoundsTestEnable -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetDepthCompareOp: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCompareOp depthCompareOp{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(depthCompareOp))
            return false;
        LSW_TRACE("server vkCmdSetDepthCompareOp(commandBuffer={}, depthCompareOp={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(depthCompareOp));
        s.on_vkCmdSetDepthCompareOp(commandBuffer, depthCompareOp);
        LSW_TRACE("server vkCmdSetDepthCompareOp -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetDepthTestEnable: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBool32 depthTestEnable{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(depthTestEnable))
            return false;
        LSW_TRACE("server vkCmdSetDepthTestEnable(commandBuffer={}, depthTestEnable={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(depthTestEnable));
        s.on_vkCmdSetDepthTestEnable(commandBuffer, depthTestEnable);
        LSW_TRACE("server vkCmdSetDepthTestEnable -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetDepthWriteEnable: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBool32 depthWriteEnable{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(depthWriteEnable))
            return false;
        LSW_TRACE("server vkCmdSetDepthWriteEnable(commandBuffer={}, depthWriteEnable={})",
                  static_cast<const void*>(commandBuffer),
                  static_cast<long long>(depthWriteEnable));
        s.on_vkCmdSetDepthWriteEnable(commandBuffer, depthWriteEnable);
        LSW_TRACE("server vkCmdSetDepthWriteEnable -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetDeviceMask: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t deviceMask{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(deviceMask))
            return false;
        LSW_TRACE("server vkCmdSetDeviceMask(commandBuffer={}, deviceMask={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(deviceMask));
        s.on_vkCmdSetDeviceMask(commandBuffer, deviceMask);
        LSW_TRACE("server vkCmdSetDeviceMask -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetEvent2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkEvent event = VK_NULL_HANDLE;
        VkDependencyInfo pDependencyInfo_local{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!deserialize_VkEvent(r, event))
            return false;
        if (!deserialize_VkDependencyInfo(r, &pDependencyInfo_local, arena))
            return false;
        LSW_TRACE("server vkCmdSetEvent2(commandBuffer={}, event={}, pDependencyInfo=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<const void*>(event));
        s.on_vkCmdSetEvent2(commandBuffer, event, &pDependencyInfo_local);
        LSW_TRACE("server vkCmdSetEvent2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetFrontFace: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFrontFace frontFace{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(frontFace))
            return false;
        LSW_TRACE("server vkCmdSetFrontFace(commandBuffer={}, frontFace={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(frontFace));
        s.on_vkCmdSetFrontFace(commandBuffer, frontFace);
        LSW_TRACE("server vkCmdSetFrontFace -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetPrimitiveRestartEnable: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBool32 primitiveRestartEnable{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(primitiveRestartEnable))
            return false;
        LSW_TRACE(
            "server vkCmdSetPrimitiveRestartEnable(commandBuffer={}, primitiveRestartEnable={})",
            static_cast<const void*>(commandBuffer),
            static_cast<long long>(primitiveRestartEnable));
        s.on_vkCmdSetPrimitiveRestartEnable(commandBuffer, primitiveRestartEnable);
        LSW_TRACE("server vkCmdSetPrimitiveRestartEnable -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetPrimitiveTopology: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPrimitiveTopology primitiveTopology{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(primitiveTopology))
            return false;
        LSW_TRACE("server vkCmdSetPrimitiveTopology(commandBuffer={}, primitiveTopology={})",
                  static_cast<const void*>(commandBuffer),
                  static_cast<long long>(primitiveTopology));
        s.on_vkCmdSetPrimitiveTopology(commandBuffer, primitiveTopology);
        LSW_TRACE("server vkCmdSetPrimitiveTopology -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetRasterizerDiscardEnable: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBool32 rasterizerDiscardEnable{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(rasterizerDiscardEnable))
            return false;
        LSW_TRACE(
            "server vkCmdSetRasterizerDiscardEnable(commandBuffer={}, rasterizerDiscardEnable={})",
            static_cast<const void*>(commandBuffer),
            static_cast<long long>(rasterizerDiscardEnable));
        s.on_vkCmdSetRasterizerDiscardEnable(commandBuffer, rasterizerDiscardEnable);
        LSW_TRACE("server vkCmdSetRasterizerDiscardEnable -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetScissorWithCount: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t scissorCount{};
        std::vector<VkRect2D> pScissors_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(scissorCount))
            return false;
        pScissors_storage.resize(static_cast<std::size_t>(scissorCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(scissorCount); ++__i) {
            if (!deserialize_VkRect2D(r, &pScissors_storage[__i], arena))
                return false;
        }
        LSW_TRACE(
            "server vkCmdSetScissorWithCount(commandBuffer={}, scissorCount={}, pScissors=ptr)",
            static_cast<const void*>(commandBuffer), static_cast<long long>(scissorCount));
        s.on_vkCmdSetScissorWithCount(commandBuffer, scissorCount, pScissors_storage.data());
        LSW_TRACE("server vkCmdSetScissorWithCount -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetStencilOp: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkStencilFaceFlags faceMask{};
        VkStencilOp failOp{};
        VkStencilOp passOp{};
        VkStencilOp depthFailOp{};
        VkCompareOp compareOp{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(faceMask))
            return false;
        if (!r.read_pod(failOp))
            return false;
        if (!r.read_pod(passOp))
            return false;
        if (!r.read_pod(depthFailOp))
            return false;
        if (!r.read_pod(compareOp))
            return false;
        LSW_TRACE("server vkCmdSetStencilOp(commandBuffer={}, faceMask={}, failOp={}, passOp={}, "
                  "depthFailOp={}, compareOp={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(faceMask),
                  static_cast<long long>(failOp), static_cast<long long>(passOp),
                  static_cast<long long>(depthFailOp), static_cast<long long>(compareOp));
        s.on_vkCmdSetStencilOp(commandBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
        LSW_TRACE("server vkCmdSetStencilOp -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetStencilTestEnable: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkBool32 stencilTestEnable{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(stencilTestEnable))
            return false;
        LSW_TRACE("server vkCmdSetStencilTestEnable(commandBuffer={}, stencilTestEnable={})",
                  static_cast<const void*>(commandBuffer),
                  static_cast<long long>(stencilTestEnable));
        s.on_vkCmdSetStencilTestEnable(commandBuffer, stencilTestEnable);
        LSW_TRACE("server vkCmdSetStencilTestEnable -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdSetViewportWithCount: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t viewportCount{};
        std::vector<VkViewport> pViewports_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(viewportCount))
            return false;
        pViewports_storage.resize(static_cast<std::size_t>(viewportCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(viewportCount); ++__i) {
            if (!deserialize_VkViewport(r, &pViewports_storage[__i], arena))
                return false;
        }
        LSW_TRACE(
            "server vkCmdSetViewportWithCount(commandBuffer={}, viewportCount={}, pViewports=ptr)",
            static_cast<const void*>(commandBuffer), static_cast<long long>(viewportCount));
        s.on_vkCmdSetViewportWithCount(commandBuffer, viewportCount, pViewports_storage.data());
        LSW_TRACE("server vkCmdSetViewportWithCount -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdWaitEvents2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t eventCount{};
        std::vector<VkEvent> pEvents_storage;
        std::vector<VkDependencyInfo> pDependencyInfos_storage;
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(eventCount))
            return false;
        pEvents_storage.resize(static_cast<std::size_t>(eventCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(eventCount); ++__i) {
            if (!deserialize_VkEvent(r, pEvents_storage[__i]))
                return false;
        }
        pDependencyInfos_storage.resize(static_cast<std::size_t>(eventCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(eventCount); ++__i) {
            if (!deserialize_VkDependencyInfo(r, &pDependencyInfos_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkCmdWaitEvents2(commandBuffer={}, eventCount={}, pEvents=ptr, "
                  "pDependencyInfos=ptr)",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(eventCount));
        s.on_vkCmdWaitEvents2(commandBuffer, eventCount, pEvents_storage.data(),
                              pDependencyInfos_storage.data());
        LSW_TRACE("server vkCmdWaitEvents2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdWriteTimestamp: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipelineStageFlagBits pipelineStage{};
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t query{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(pipelineStage))
            return false;
        if (!deserialize_VkQueryPool(r, queryPool))
            return false;
        if (!r.read_pod(query))
            return false;
        LSW_TRACE("server vkCmdWriteTimestamp(commandBuffer={}, pipelineStage={}, queryPool={}, "
                  "query={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(pipelineStage),
                  static_cast<const void*>(queryPool), static_cast<long long>(query));
        s.on_vkCmdWriteTimestamp(commandBuffer, pipelineStage, queryPool, query);
        LSW_TRACE("server vkCmdWriteTimestamp -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCmdWriteTimestamp2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipelineStageFlags2 stage{};
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t query{};
        if (!deserialize_VkCommandBuffer(r, commandBuffer))
            return false;
        if (!r.read_pod(stage))
            return false;
        if (!deserialize_VkQueryPool(r, queryPool))
            return false;
        if (!r.read_pod(query))
            return false;
        LSW_TRACE("server vkCmdWriteTimestamp2(commandBuffer={}, stage={}, queryPool={}, query={})",
                  static_cast<const void*>(commandBuffer), static_cast<long long>(stage),
                  static_cast<const void*>(queryPool), static_cast<long long>(query));
        s.on_vkCmdWriteTimestamp2(commandBuffer, stage, queryPool, query);
        LSW_TRACE("server vkCmdWriteTimestamp2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateBufferView: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBufferViewCreateInfo pCreateInfo_local{};
        VkBufferView pView_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBufferViewCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE(
            "server vkCreateBufferView(device={}, pCreateInfo=ptr, pAllocator=ptr, pView=ptr)",
            static_cast<const void*>(device));
        VkResult result = s.on_vkCreateBufferView(device, &pCreateInfo_local, nullptr, &pView_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkBufferView(reply, pView_out);
        LSW_TRACE("server vkCreateBufferView -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateDescriptorUpdateTemplate: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorUpdateTemplateCreateInfo pCreateInfo_local{};
        VkDescriptorUpdateTemplate pDescriptorUpdateTemplate_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorUpdateTemplateCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateDescriptorUpdateTemplate(device={}, pCreateInfo=ptr, "
                  "pAllocator=ptr, pDescriptorUpdateTemplate=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkCreateDescriptorUpdateTemplate(device, &pCreateInfo_local, nullptr,
                                                                &pDescriptorUpdateTemplate_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkDescriptorUpdateTemplate(reply, pDescriptorUpdateTemplate_out);
        LSW_TRACE("server vkCreateDescriptorUpdateTemplate -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreatePrivateDataSlot: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPrivateDataSlotCreateInfo pCreateInfo_local{};
        VkPrivateDataSlot pPrivateDataSlot_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPrivateDataSlotCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreatePrivateDataSlot(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pPrivateDataSlot=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkCreatePrivateDataSlot(device, &pCreateInfo_local, nullptr,
                                                       &pPrivateDataSlot_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkPrivateDataSlot(reply, pPrivateDataSlot_out);
        LSW_TRACE("server vkCreatePrivateDataSlot -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateQueryPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkQueryPoolCreateInfo pCreateInfo_local{};
        VkQueryPool pQueryPool_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkQueryPoolCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE(
            "server vkCreateQueryPool(device={}, pCreateInfo=ptr, pAllocator=ptr, pQueryPool=ptr)",
            static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateQueryPool(device, &pCreateInfo_local, nullptr, &pQueryPool_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkQueryPool(reply, pQueryPool_out);
        LSW_TRACE("server vkCreateQueryPool -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateRenderPass2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkRenderPassCreateInfo2 pCreateInfo_local{};
        VkRenderPass pRenderPass_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkRenderPassCreateInfo2(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateRenderPass2(device={}, pCreateInfo=ptr, pAllocator=ptr, "
                  "pRenderPass=ptr)",
                  static_cast<const void*>(device));
        VkResult result =
            s.on_vkCreateRenderPass2(device, &pCreateInfo_local, nullptr, &pRenderPass_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkRenderPass(reply, pRenderPass_out);
        LSW_TRACE("server vkCreateRenderPass2 -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkCreateSamplerYcbcrConversion: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSamplerYcbcrConversionCreateInfo pCreateInfo_local{};
        VkSamplerYcbcrConversion pYcbcrConversion_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSamplerYcbcrConversionCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        LSW_TRACE("server vkCreateSamplerYcbcrConversion(device={}, pCreateInfo=ptr, "
                  "pAllocator=ptr, pYcbcrConversion=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkCreateSamplerYcbcrConversion(device, &pCreateInfo_local, nullptr,
                                                              &pYcbcrConversion_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkSamplerYcbcrConversion(reply, pYcbcrConversion_out);
        LSW_TRACE("server vkCreateSamplerYcbcrConversion -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyBufferView: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBufferView bufferView = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBufferView(r, bufferView))
            return false;
        LSW_TRACE("server vkDestroyBufferView(device={}, bufferView={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(bufferView));
        s.on_vkDestroyBufferView(device, bufferView, nullptr);
        LSW_TRACE("server vkDestroyBufferView -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyDescriptorUpdateTemplate: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorUpdateTemplate descriptorUpdateTemplate = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorUpdateTemplate(r, descriptorUpdateTemplate))
            return false;
        LSW_TRACE("server vkDestroyDescriptorUpdateTemplate(device={}, "
                  "descriptorUpdateTemplate={}, pAllocator=ptr)",
                  static_cast<const void*>(device),
                  static_cast<const void*>(descriptorUpdateTemplate));
        s.on_vkDestroyDescriptorUpdateTemplate(device, descriptorUpdateTemplate, nullptr);
        LSW_TRACE("server vkDestroyDescriptorUpdateTemplate -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyPrivateDataSlot: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPrivateDataSlot privateDataSlot = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPrivateDataSlot(r, privateDataSlot))
            return false;
        LSW_TRACE("server vkDestroyPrivateDataSlot(device={}, privateDataSlot={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(privateDataSlot));
        s.on_vkDestroyPrivateDataSlot(device, privateDataSlot, nullptr);
        LSW_TRACE("server vkDestroyPrivateDataSlot -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroyQueryPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkQueryPool queryPool = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkQueryPool(r, queryPool))
            return false;
        LSW_TRACE("server vkDestroyQueryPool(device={}, queryPool={}, pAllocator=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(queryPool));
        s.on_vkDestroyQueryPool(device, queryPool, nullptr);
        LSW_TRACE("server vkDestroyQueryPool -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkDestroySamplerYcbcrConversion: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSamplerYcbcrConversion ycbcrConversion = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSamplerYcbcrConversion(r, ycbcrConversion))
            return false;
        LSW_TRACE(
            "server vkDestroySamplerYcbcrConversion(device={}, ycbcrConversion={}, pAllocator=ptr)",
            static_cast<const void*>(device), static_cast<const void*>(ycbcrConversion));
        s.on_vkDestroySamplerYcbcrConversion(device, ycbcrConversion, nullptr);
        LSW_TRACE("server vkDestroySamplerYcbcrConversion -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkEnumeratePhysicalDeviceGroups: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkInstance instance = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pPhysicalDeviceGroupCount_local = 0;
        std::vector<VkPhysicalDeviceGroupProperties> pPhysicalDeviceGroupProperties_storage;
        if (!deserialize_VkInstance(r, instance))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pPhysicalDeviceGroupCount_local))
                return false;
            pPhysicalDeviceGroupProperties_storage.resize(pPhysicalDeviceGroupCount_local);
            for (auto& __e : pPhysicalDeviceGroupProperties_storage) {
                __e.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
                __e.pNext = nullptr;
            }
        }
        LSW_TRACE("server vkEnumeratePhysicalDeviceGroups(instance={}, "
                  "pPhysicalDeviceGroupCount=ptr, pPhysicalDeviceGroupProperties=ptr)",
                  static_cast<const void*>(instance));
        VkResult result = s.on_vkEnumeratePhysicalDeviceGroups(
            instance, &pPhysicalDeviceGroupCount_local,
            array_present ? pPhysicalDeviceGroupProperties_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pPhysicalDeviceGroupCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pPhysicalDeviceGroupCount_local; ++k) {
                serialize_VkPhysicalDeviceGroupProperties(
                    reply, pPhysicalDeviceGroupProperties_storage[k]);
            }
        }
        LSW_TRACE("server vkEnumeratePhysicalDeviceGroups -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkFlushMappedMemoryRanges: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t memoryRangeCount{};
        std::vector<VkMappedMemoryRange> pMemoryRanges_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(memoryRangeCount))
            return false;
        pMemoryRanges_storage.resize(static_cast<std::size_t>(memoryRangeCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(memoryRangeCount); ++__i) {
            if (!deserialize_VkMappedMemoryRange(r, &pMemoryRanges_storage[__i], arena))
                return false;
        }
        LSW_TRACE(
            "server vkFlushMappedMemoryRanges(device={}, memoryRangeCount={}, pMemoryRanges=ptr)",
            static_cast<const void*>(device), static_cast<long long>(memoryRangeCount));
        VkResult result =
            s.on_vkFlushMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkFlushMappedMemoryRanges -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetBufferDeviceAddress: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBufferDeviceAddressInfo pInfo_local{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBufferDeviceAddressInfo(r, &pInfo_local, arena))
            return false;
        LSW_TRACE("server vkGetBufferDeviceAddress(device={}, pInfo=ptr)",
                  static_cast<const void*>(device));
        VkDeviceAddress result = s.on_vkGetBufferDeviceAddress(device, &pInfo_local);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkGetBufferDeviceAddress -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetBufferMemoryRequirements2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBufferMemoryRequirementsInfo2 pInfo_local{};
        VkMemoryRequirements2 pMemoryRequirements_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBufferMemoryRequirementsInfo2(r, &pInfo_local, arena))
            return false;
        if (!r.read_pod(pMemoryRequirements_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pMemoryRequirements_out.pNext = nullptr;
        }
        LSW_TRACE(
            "server vkGetBufferMemoryRequirements2(device={}, pInfo=ptr, pMemoryRequirements=ptr)",
            static_cast<const void*>(device));
        s.on_vkGetBufferMemoryRequirements2(device, &pInfo_local, &pMemoryRequirements_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkMemoryRequirements2(reply, pMemoryRequirements_out);
        LSW_TRACE("server vkGetBufferMemoryRequirements2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetBufferOpaqueCaptureAddress: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkBufferDeviceAddressInfo pInfo_local{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkBufferDeviceAddressInfo(r, &pInfo_local, arena))
            return false;
        LSW_TRACE("server vkGetBufferOpaqueCaptureAddress(device={}, pInfo=ptr)",
                  static_cast<const void*>(device));
        uint64_t result = s.on_vkGetBufferOpaqueCaptureAddress(device, &pInfo_local);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkGetBufferOpaqueCaptureAddress -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDescriptorSetLayoutSupport: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorSetLayoutCreateInfo pCreateInfo_local{};
        VkDescriptorSetLayoutSupport pSupport_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDescriptorSetLayoutCreateInfo(r, &pCreateInfo_local, arena))
            return false;
        if (!r.read_pod(pSupport_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pSupport_out.pNext = nullptr;
        }
        LSW_TRACE(
            "server vkGetDescriptorSetLayoutSupport(device={}, pCreateInfo=ptr, pSupport=ptr)",
            static_cast<const void*>(device));
        s.on_vkGetDescriptorSetLayoutSupport(device, &pCreateInfo_local, &pSupport_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkDescriptorSetLayoutSupport(reply, pSupport_out);
        LSW_TRACE("server vkGetDescriptorSetLayoutSupport -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceBufferMemoryRequirements: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceBufferMemoryRequirements pInfo_local{};
        VkMemoryRequirements2 pMemoryRequirements_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDeviceBufferMemoryRequirements(r, &pInfo_local, arena))
            return false;
        if (!r.read_pod(pMemoryRequirements_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pMemoryRequirements_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetDeviceBufferMemoryRequirements(device={}, pInfo=ptr, "
                  "pMemoryRequirements=ptr)",
                  static_cast<const void*>(device));
        s.on_vkGetDeviceBufferMemoryRequirements(device, &pInfo_local, &pMemoryRequirements_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkMemoryRequirements2(reply, pMemoryRequirements_out);
        LSW_TRACE("server vkGetDeviceBufferMemoryRequirements -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceGroupPeerMemoryFeatures: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t heapIndex{};
        uint32_t localDeviceIndex{};
        uint32_t remoteDeviceIndex{};
        VkPeerMemoryFeatureFlags pPeerMemoryFeatures_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(heapIndex))
            return false;
        if (!r.read_pod(localDeviceIndex))
            return false;
        if (!r.read_pod(remoteDeviceIndex))
            return false;
        LSW_TRACE("server vkGetDeviceGroupPeerMemoryFeatures(device={}, heapIndex={}, "
                  "localDeviceIndex={}, remoteDeviceIndex={}, pPeerMemoryFeatures=ptr)",
                  static_cast<const void*>(device), static_cast<long long>(heapIndex),
                  static_cast<long long>(localDeviceIndex),
                  static_cast<long long>(remoteDeviceIndex));
        s.on_vkGetDeviceGroupPeerMemoryFeatures(device, heapIndex, localDeviceIndex,
                                                remoteDeviceIndex, &pPeerMemoryFeatures_out);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pPeerMemoryFeatures_out);
        LSW_TRACE("server vkGetDeviceGroupPeerMemoryFeatures -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceGroupPresentCapabilitiesKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceGroupPresentCapabilitiesKHR pDeviceGroupPresentCapabilities_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(pDeviceGroupPresentCapabilities_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pDeviceGroupPresentCapabilities_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetDeviceGroupPresentCapabilitiesKHR(device={}, "
                  "pDeviceGroupPresentCapabilities=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkGetDeviceGroupPresentCapabilitiesKHR(
            device, &pDeviceGroupPresentCapabilities_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        serialize_VkDeviceGroupPresentCapabilitiesKHR(reply, pDeviceGroupPresentCapabilities_out);
        LSW_TRACE("server vkGetDeviceGroupPresentCapabilitiesKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceGroupSurfacePresentModesKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkDeviceGroupPresentModeFlagsKHR pModes_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSurfaceKHR(r, surface))
            return false;
        LSW_TRACE(
            "server vkGetDeviceGroupSurfacePresentModesKHR(device={}, surface={}, pModes=ptr)",
            static_cast<const void*>(device), static_cast<const void*>(surface));
        VkResult result = s.on_vkGetDeviceGroupSurfacePresentModesKHR(device, surface, &pModes_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pModes_out);
        LSW_TRACE("server vkGetDeviceGroupSurfacePresentModesKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceImageMemoryRequirements: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceImageMemoryRequirements pInfo_local{};
        VkMemoryRequirements2 pMemoryRequirements_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDeviceImageMemoryRequirements(r, &pInfo_local, arena))
            return false;
        if (!r.read_pod(pMemoryRequirements_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pMemoryRequirements_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetDeviceImageMemoryRequirements(device={}, pInfo=ptr, "
                  "pMemoryRequirements=ptr)",
                  static_cast<const void*>(device));
        s.on_vkGetDeviceImageMemoryRequirements(device, &pInfo_local, &pMemoryRequirements_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkMemoryRequirements2(reply, pMemoryRequirements_out);
        LSW_TRACE("server vkGetDeviceImageMemoryRequirements -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceImageSparseMemoryRequirements: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceImageMemoryRequirements pInfo_local{};
        std::uint8_t array_present = 0;
        std::uint32_t pSparseMemoryRequirementCount_local = 0;
        std::vector<VkSparseImageMemoryRequirements2> pSparseMemoryRequirements_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDeviceImageMemoryRequirements(r, &pInfo_local, arena))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pSparseMemoryRequirementCount_local))
                return false;
            pSparseMemoryRequirements_storage.resize(pSparseMemoryRequirementCount_local);
            for (auto& __e : pSparseMemoryRequirements_storage) {
                __e.sType = VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2;
                __e.pNext = nullptr;
            }
        }
        LSW_TRACE("server vkGetDeviceImageSparseMemoryRequirements(device={}, pInfo=ptr, "
                  "pSparseMemoryRequirementCount=ptr, pSparseMemoryRequirements=ptr)",
                  static_cast<const void*>(device));
        s.on_vkGetDeviceImageSparseMemoryRequirements(
            device, &pInfo_local, &pSparseMemoryRequirementCount_local,
            array_present ? pSparseMemoryRequirements_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pSparseMemoryRequirementCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pSparseMemoryRequirementCount_local; ++k) {
                serialize_VkSparseImageMemoryRequirements2(reply,
                                                           pSparseMemoryRequirements_storage[k]);
            }
        }
        LSW_TRACE("server vkGetDeviceImageSparseMemoryRequirements -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceMemoryCommitment: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize pCommittedMemoryInBytes_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDeviceMemory(r, memory))
            return false;
        LSW_TRACE(
            "server vkGetDeviceMemoryCommitment(device={}, memory={}, pCommittedMemoryInBytes=ptr)",
            static_cast<const void*>(device), static_cast<const void*>(memory));
        s.on_vkGetDeviceMemoryCommitment(device, memory, &pCommittedMemoryInBytes_out);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pCommittedMemoryInBytes_out);
        LSW_TRACE("server vkGetDeviceMemoryCommitment -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceMemoryOpaqueCaptureAddress: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceMemoryOpaqueCaptureAddressInfo pInfo_local{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDeviceMemoryOpaqueCaptureAddressInfo(r, &pInfo_local, arena))
            return false;
        LSW_TRACE("server vkGetDeviceMemoryOpaqueCaptureAddress(device={}, pInfo=ptr)",
                  static_cast<const void*>(device));
        uint64_t result = s.on_vkGetDeviceMemoryOpaqueCaptureAddress(device, &pInfo_local);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkGetDeviceMemoryOpaqueCaptureAddress -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetDeviceQueue2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceQueueInfo2 pQueueInfo_local{};
        VkQueue pQueue_out = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDeviceQueueInfo2(r, &pQueueInfo_local, arena))
            return false;
        LSW_TRACE("server vkGetDeviceQueue2(device={}, pQueueInfo=ptr, pQueue=ptr)",
                  static_cast<const void*>(device));
        s.on_vkGetDeviceQueue2(device, &pQueueInfo_local, &pQueue_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkQueue(reply, pQueue_out);
        LSW_TRACE("server vkGetDeviceQueue2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetImageMemoryRequirements2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImageMemoryRequirementsInfo2 pInfo_local{};
        VkMemoryRequirements2 pMemoryRequirements_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImageMemoryRequirementsInfo2(r, &pInfo_local, arena))
            return false;
        if (!r.read_pod(pMemoryRequirements_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pMemoryRequirements_out.pNext = nullptr;
        }
        LSW_TRACE(
            "server vkGetImageMemoryRequirements2(device={}, pInfo=ptr, pMemoryRequirements=ptr)",
            static_cast<const void*>(device));
        s.on_vkGetImageMemoryRequirements2(device, &pInfo_local, &pMemoryRequirements_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkMemoryRequirements2(reply, pMemoryRequirements_out);
        LSW_TRACE("server vkGetImageMemoryRequirements2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetImageSparseMemoryRequirements: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pSparseMemoryRequirementCount_local = 0;
        std::vector<VkSparseImageMemoryRequirements> pSparseMemoryRequirements_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImage(r, image))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pSparseMemoryRequirementCount_local))
                return false;
            pSparseMemoryRequirements_storage.resize(pSparseMemoryRequirementCount_local);
        }
        LSW_TRACE("server vkGetImageSparseMemoryRequirements(device={}, image={}, "
                  "pSparseMemoryRequirementCount=ptr, pSparseMemoryRequirements=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(image));
        s.on_vkGetImageSparseMemoryRequirements(
            device, image, &pSparseMemoryRequirementCount_local,
            array_present ? pSparseMemoryRequirements_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pSparseMemoryRequirementCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pSparseMemoryRequirementCount_local; ++k) {
                serialize_VkSparseImageMemoryRequirements(reply,
                                                          pSparseMemoryRequirements_storage[k]);
            }
        }
        LSW_TRACE("server vkGetImageSparseMemoryRequirements -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetImageSparseMemoryRequirements2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImageSparseMemoryRequirementsInfo2 pInfo_local{};
        std::uint8_t array_present = 0;
        std::uint32_t pSparseMemoryRequirementCount_local = 0;
        std::vector<VkSparseImageMemoryRequirements2> pSparseMemoryRequirements_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImageSparseMemoryRequirementsInfo2(r, &pInfo_local, arena))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pSparseMemoryRequirementCount_local))
                return false;
            pSparseMemoryRequirements_storage.resize(pSparseMemoryRequirementCount_local);
            for (auto& __e : pSparseMemoryRequirements_storage) {
                __e.sType = VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2;
                __e.pNext = nullptr;
            }
        }
        LSW_TRACE("server vkGetImageSparseMemoryRequirements2(device={}, pInfo=ptr, "
                  "pSparseMemoryRequirementCount=ptr, pSparseMemoryRequirements=ptr)",
                  static_cast<const void*>(device));
        s.on_vkGetImageSparseMemoryRequirements2(
            device, &pInfo_local, &pSparseMemoryRequirementCount_local,
            array_present ? pSparseMemoryRequirements_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pSparseMemoryRequirementCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pSparseMemoryRequirementCount_local; ++k) {
                serialize_VkSparseImageMemoryRequirements2(reply,
                                                           pSparseMemoryRequirements_storage[k]);
            }
        }
        LSW_TRACE("server vkGetImageSparseMemoryRequirements2 -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetImageSubresourceLayout: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageSubresource pSubresource_local{};
        VkSubresourceLayout pLayout_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkImage(r, image))
            return false;
        if (!deserialize_VkImageSubresource(r, &pSubresource_local, arena))
            return false;
        LSW_TRACE("server vkGetImageSubresourceLayout(device={}, image={}, pSubresource=ptr, "
                  "pLayout=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(image));
        s.on_vkGetImageSubresourceLayout(device, image, &pSubresource_local, &pLayout_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkSubresourceLayout(reply, pLayout_out);
        LSW_TRACE("server vkGetImageSubresourceLayout -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceExternalBufferProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceExternalBufferInfo pExternalBufferInfo_local{};
        VkExternalBufferProperties pExternalBufferProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkPhysicalDeviceExternalBufferInfo(r, &pExternalBufferInfo_local, arena))
            return false;
        if (!r.read_pod(pExternalBufferProperties_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pExternalBufferProperties_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetPhysicalDeviceExternalBufferProperties(physicalDevice={}, "
                  "pExternalBufferInfo=ptr, pExternalBufferProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceExternalBufferProperties(physicalDevice, &pExternalBufferInfo_local,
                                                         &pExternalBufferProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkExternalBufferProperties(reply, pExternalBufferProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceExternalBufferProperties -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceExternalFenceProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceExternalFenceInfo pExternalFenceInfo_local{};
        VkExternalFenceProperties pExternalFenceProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkPhysicalDeviceExternalFenceInfo(r, &pExternalFenceInfo_local, arena))
            return false;
        if (!r.read_pod(pExternalFenceProperties_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pExternalFenceProperties_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetPhysicalDeviceExternalFenceProperties(physicalDevice={}, "
                  "pExternalFenceInfo=ptr, pExternalFenceProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceExternalFenceProperties(physicalDevice, &pExternalFenceInfo_local,
                                                        &pExternalFenceProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkExternalFenceProperties(reply, pExternalFenceProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceExternalFenceProperties -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceExternalSemaphoreProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceExternalSemaphoreInfo pExternalSemaphoreInfo_local{};
        VkExternalSemaphoreProperties pExternalSemaphoreProperties_out{};
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkPhysicalDeviceExternalSemaphoreInfo(r, &pExternalSemaphoreInfo_local,
                                                               arena))
            return false;
        if (!r.read_pod(pExternalSemaphoreProperties_out.sType))
            return false;
        {
            std::uint64_t pnext_count = 0;
            if (!r.read_varint(pnext_count))
                return false;
            if (pnext_count != 0)
                return false;
            pExternalSemaphoreProperties_out.pNext = nullptr;
        }
        LSW_TRACE("server vkGetPhysicalDeviceExternalSemaphoreProperties(physicalDevice={}, "
                  "pExternalSemaphoreInfo=ptr, pExternalSemaphoreProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        s.on_vkGetPhysicalDeviceExternalSemaphoreProperties(
            physicalDevice, &pExternalSemaphoreInfo_local, &pExternalSemaphoreProperties_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkExternalSemaphoreProperties(reply, pExternalSemaphoreProperties_out);
        LSW_TRACE("server vkGetPhysicalDeviceExternalSemaphoreProperties -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDevicePresentRectanglesKHR: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pRectCount_local = 0;
        std::vector<VkRect2D> pRects_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!deserialize_VkSurfaceKHR(r, surface))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pRectCount_local))
                return false;
            pRects_storage.resize(pRectCount_local);
        }
        LSW_TRACE("server vkGetPhysicalDevicePresentRectanglesKHR(physicalDevice={}, surface={}, "
                  "pRectCount=ptr, pRects=ptr)",
                  static_cast<const void*>(physicalDevice), static_cast<const void*>(surface));
        VkResult result = s.on_vkGetPhysicalDevicePresentRectanglesKHR(
            physicalDevice, surface, &pRectCount_local,
            array_present ? pRects_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pRectCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pRectCount_local; ++k) {
                serialize_VkRect2D(reply, pRects_storage[k]);
            }
        }
        LSW_TRACE("server vkGetPhysicalDevicePresentRectanglesKHR -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPhysicalDeviceToolProperties: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint8_t array_present = 0;
        std::uint32_t pToolCount_local = 0;
        std::vector<VkPhysicalDeviceToolProperties> pToolProperties_storage;
        if (!deserialize_VkPhysicalDevice(r, physicalDevice))
            return false;
        if (!r.read_pod(array_present))
            return false;
        if (array_present) {
            if (!r.read_pod(pToolCount_local))
                return false;
            pToolProperties_storage.resize(pToolCount_local);
            for (auto& __e : pToolProperties_storage) {
                __e.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES;
                __e.pNext = nullptr;
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceToolProperties(physicalDevice={}, pToolCount=ptr, "
                  "pToolProperties=ptr)",
                  static_cast<const void*>(physicalDevice));
        VkResult result = s.on_vkGetPhysicalDeviceToolProperties(
            physicalDevice, &pToolCount_local,
            array_present ? pToolProperties_storage.data() : nullptr);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pToolCount_local);
        if (array_present) {
            for (std::uint32_t k = 0; k < pToolCount_local; ++k) {
                serialize_VkPhysicalDeviceToolProperties(reply, pToolProperties_storage[k]);
            }
        }
        LSW_TRACE("server vkGetPhysicalDeviceToolProperties -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetPrivateData: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkObjectType objectType{};
        uint64_t objectHandle{};
        VkPrivateDataSlot privateDataSlot = VK_NULL_HANDLE;
        uint64_t pData_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(objectType))
            return false;
        if (!r.read_pod(objectHandle))
            return false;
        if (!deserialize_VkPrivateDataSlot(r, privateDataSlot))
            return false;
        LSW_TRACE("server vkGetPrivateData(device={}, objectType={}, objectHandle={}, "
                  "privateDataSlot={}, pData=ptr)",
                  static_cast<const void*>(device), static_cast<long long>(objectType),
                  static_cast<long long>(objectHandle), static_cast<const void*>(privateDataSlot));
        s.on_vkGetPrivateData(device, objectType, objectHandle, privateDataSlot, &pData_out);
        reply.write_pod<std::int32_t>(0);
        reply.write_pod(pData_out);
        LSW_TRACE("server vkGetPrivateData -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetRenderAreaGranularity: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkExtent2D pGranularity_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkRenderPass(r, renderPass))
            return false;
        LSW_TRACE("server vkGetRenderAreaGranularity(device={}, renderPass={}, pGranularity=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(renderPass));
        s.on_vkGetRenderAreaGranularity(device, renderPass, &pGranularity_out);
        reply.write_pod<std::int32_t>(0);
        serialize_VkExtent2D(reply, pGranularity_out);
        LSW_TRACE("server vkGetRenderAreaGranularity -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkGetSemaphoreCounterValue: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        uint64_t pValue_out{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSemaphore(r, semaphore))
            return false;
        LSW_TRACE("server vkGetSemaphoreCounterValue(device={}, semaphore={}, pValue=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(semaphore));
        VkResult result = s.on_vkGetSemaphoreCounterValue(device, semaphore, &pValue_out);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        reply.write_pod(pValue_out);
        LSW_TRACE("server vkGetSemaphoreCounterValue -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkInvalidateMappedMemoryRanges: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t memoryRangeCount{};
        std::vector<VkMappedMemoryRange> pMemoryRanges_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(memoryRangeCount))
            return false;
        pMemoryRanges_storage.resize(static_cast<std::size_t>(memoryRangeCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(memoryRangeCount); ++__i) {
            if (!deserialize_VkMappedMemoryRange(r, &pMemoryRanges_storage[__i], arena))
                return false;
        }
        LSW_TRACE("server vkInvalidateMappedMemoryRanges(device={}, memoryRangeCount={}, "
                  "pMemoryRanges=ptr)",
                  static_cast<const void*>(device), static_cast<long long>(memoryRangeCount));
        VkResult result = s.on_vkInvalidateMappedMemoryRanges(device, memoryRangeCount,
                                                              pMemoryRanges_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkInvalidateMappedMemoryRanges -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkMergePipelineCaches: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkPipelineCache dstCache = VK_NULL_HANDLE;
        uint32_t srcCacheCount{};
        std::vector<VkPipelineCache> pSrcCaches_storage;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkPipelineCache(r, dstCache))
            return false;
        if (!r.read_pod(srcCacheCount))
            return false;
        pSrcCaches_storage.resize(static_cast<std::size_t>(srcCacheCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(srcCacheCount); ++__i) {
            if (!deserialize_VkPipelineCache(r, pSrcCaches_storage[__i]))
                return false;
        }
        LSW_TRACE("server vkMergePipelineCaches(device={}, dstCache={}, srcCacheCount={}, "
                  "pSrcCaches=ptr)",
                  static_cast<const void*>(device), static_cast<const void*>(dstCache),
                  static_cast<long long>(srcCacheCount));
        VkResult result =
            s.on_vkMergePipelineCaches(device, dstCache, srcCacheCount, pSrcCaches_storage.data());
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkMergePipelineCaches -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkQueueBindSparse: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t bindInfoCount{};
        std::vector<VkBindSparseInfo> pBindInfo_storage;
        VkFence fence = VK_NULL_HANDLE;
        if (!deserialize_VkQueue(r, queue))
            return false;
        if (!r.read_pod(bindInfoCount))
            return false;
        pBindInfo_storage.resize(static_cast<std::size_t>(bindInfoCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(bindInfoCount); ++__i) {
            if (!deserialize_VkBindSparseInfo(r, &pBindInfo_storage[__i], arena))
                return false;
        }
        if (!deserialize_VkFence(r, fence))
            return false;
        LSW_TRACE("server vkQueueBindSparse(queue={}, bindInfoCount={}, pBindInfo=ptr, fence={})",
                  static_cast<const void*>(queue), static_cast<long long>(bindInfoCount),
                  static_cast<const void*>(fence));
        VkResult result =
            s.on_vkQueueBindSparse(queue, bindInfoCount, pBindInfo_storage.data(), fence);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkQueueBindSparse -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkQueueSubmit2: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t submitCount{};
        std::vector<VkSubmitInfo2> pSubmits_storage;
        VkFence fence = VK_NULL_HANDLE;
        if (!deserialize_VkQueue(r, queue))
            return false;
        if (!r.read_pod(submitCount))
            return false;
        pSubmits_storage.resize(static_cast<std::size_t>(submitCount));
        for (std::uint32_t __i = 0; __i < static_cast<std::uint32_t>(submitCount); ++__i) {
            if (!deserialize_VkSubmitInfo2(r, &pSubmits_storage[__i], arena))
                return false;
        }
        if (!deserialize_VkFence(r, fence))
            return false;
        LSW_TRACE("server vkQueueSubmit2(queue={}, submitCount={}, pSubmits=ptr, fence={})",
                  static_cast<const void*>(queue), static_cast<long long>(submitCount),
                  static_cast<const void*>(fence));
        VkResult result = s.on_vkQueueSubmit2(queue, submitCount, pSubmits_storage.data(), fence);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkQueueSubmit2 -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkResetQueryPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t firstQuery{};
        uint32_t queryCount{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkQueryPool(r, queryPool))
            return false;
        if (!r.read_pod(firstQuery))
            return false;
        if (!r.read_pod(queryCount))
            return false;
        LSW_TRACE("server vkResetQueryPool(device={}, queryPool={}, firstQuery={}, queryCount={})",
                  static_cast<const void*>(device), static_cast<const void*>(queryPool),
                  static_cast<long long>(firstQuery), static_cast<long long>(queryCount));
        s.on_vkResetQueryPool(device, queryPool, firstQuery, queryCount);
        LSW_TRACE("server vkResetQueryPool -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkSetPrivateData: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkObjectType objectType{};
        uint64_t objectHandle{};
        VkPrivateDataSlot privateDataSlot = VK_NULL_HANDLE;
        uint64_t data{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!r.read_pod(objectType))
            return false;
        if (!r.read_pod(objectHandle))
            return false;
        if (!deserialize_VkPrivateDataSlot(r, privateDataSlot))
            return false;
        if (!r.read_pod(data))
            return false;
        LSW_TRACE("server vkSetPrivateData(device={}, objectType={}, objectHandle={}, "
                  "privateDataSlot={}, data={})",
                  static_cast<const void*>(device), static_cast<long long>(objectType),
                  static_cast<long long>(objectHandle), static_cast<const void*>(privateDataSlot),
                  static_cast<long long>(data));
        VkResult result =
            s.on_vkSetPrivateData(device, objectType, objectHandle, privateDataSlot, data);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkSetPrivateData -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkSignalSemaphore: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSemaphoreSignalInfo pSignalInfo_local{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSemaphoreSignalInfo(r, &pSignalInfo_local, arena))
            return false;
        LSW_TRACE("server vkSignalSemaphore(device={}, pSignalInfo=ptr)",
                  static_cast<const void*>(device));
        VkResult result = s.on_vkSignalSemaphore(device, &pSignalInfo_local);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkSignalSemaphore -> {}", static_cast<int>(result));
        return true;
    }
    case ::lsw::vkproto::kCmd_vkTrimCommandPool: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandPoolTrimFlags flags{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkCommandPool(r, commandPool))
            return false;
        if (!r.read_pod(flags))
            return false;
        LSW_TRACE("server vkTrimCommandPool(device={}, commandPool={}, flags={})",
                  static_cast<const void*>(device), static_cast<const void*>(commandPool),
                  static_cast<long long>(flags));
        s.on_vkTrimCommandPool(device, commandPool, flags);
        LSW_TRACE("server vkTrimCommandPool -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkUnmapMemory: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkDeviceMemory(r, memory))
            return false;
        LSW_TRACE("server vkUnmapMemory(device={}, memory={})", static_cast<const void*>(device),
                  static_cast<const void*>(memory));
        s.on_vkUnmapMemory(device, memory);
        LSW_TRACE("server vkUnmapMemory -> (void)");
        return true;
    }
    case ::lsw::vkproto::kCmd_vkWaitSemaphores: {
        ::lsw::protocol::Arena arena;
        (void)arena;
        VkDevice device = VK_NULL_HANDLE;
        VkSemaphoreWaitInfo pWaitInfo_local{};
        uint64_t timeout{};
        if (!deserialize_VkDevice(r, device))
            return false;
        if (!deserialize_VkSemaphoreWaitInfo(r, &pWaitInfo_local, arena))
            return false;
        if (!r.read_pod(timeout))
            return false;
        LSW_TRACE("server vkWaitSemaphores(device={}, pWaitInfo=ptr, timeout={})",
                  static_cast<const void*>(device), static_cast<long long>(timeout));
        VkResult result = s.on_vkWaitSemaphores(device, &pWaitInfo_local, timeout);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(result));
        LSW_TRACE("server vkWaitSemaphores -> {}", static_cast<int>(result));
        return true;
    }
    default:
        return false;
    }
}

}  // namespace lsw::vkproto
