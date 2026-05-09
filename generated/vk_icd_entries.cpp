
#include "vk_icd_entries.h"

#include "lsw/icd/connection.hpp"
#include "lsw/icd/handle_wrap.hpp"
#include "vk_dispatch_client.h"

#include <array>
#include <vector>

namespace lsw::icd::generated {

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyInstance(VkInstance instance,
                                                  const VkAllocationCallbacks* pAllocator) {
    if (instance == VK_NULL_HANDLE) {
        return;
    }
    auto* conn = ::lsw::icd::connection_of(instance);
    ::lsw::vkproto::lsw_vkDestroyInstance(conn->transport(), instance, pAllocator);
    ::lsw::icd::destroy_wrapper(instance);
    ::lsw::icd::release_connection(conn);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyDevice(VkDevice device,
                                                const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyDevice(conn->transport(), device, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex,
                                                 uint32_t queueIndex, VkQueue* pQueue) {
    auto* conn = ::lsw::icd::connection_of(device);
    VkQueue server_pQueue = VK_NULL_HANDLE;
    ::lsw::vkproto::lsw_vkGetDeviceQueue(conn->transport(), device, queueFamilyIndex, queueIndex,
                                         &server_pQueue);
    *pQueue = ::lsw::icd::wrap_server_handle(server_pQueue, conn);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEnumeratePhysicalDevices(VkInstance instance,
                                                               uint32_t* pPhysicalDeviceCount,
                                                               VkPhysicalDevice* pPhysicalDevices) {
    auto* conn = ::lsw::icd::connection_of(instance);
    if (pPhysicalDevices == nullptr) {
        return ::lsw::vkproto::lsw_vkEnumeratePhysicalDevices(conn->transport(), instance,
                                                              pPhysicalDeviceCount, nullptr);
    }
    std::vector<VkPhysicalDevice> server_pPhysicalDevices(*pPhysicalDeviceCount, VK_NULL_HANDLE);
    const VkResult __rc = ::lsw::vkproto::lsw_vkEnumeratePhysicalDevices(
        conn->transport(), instance, pPhysicalDeviceCount, server_pPhysicalDevices.data());
    if (__rc != VK_SUCCESS && __rc != VK_INCOMPLETE) {
        return __rc;
    }
    for (std::uint32_t k = 0; k < *pPhysicalDeviceCount; ++k) {
        pPhysicalDevices[k] = ::lsw::icd::wrap_server_handle(server_pPhysicalDevices[k], conn);
    }
    return __rc;
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceProperties2(conn->transport(), physicalDevice,
                                                       pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkInstance* pInstance) {
    auto* conn = ::lsw::icd::acquire_default_connection();
    if (conn == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkInstance server_instance = VK_NULL_HANDLE;
    const VkResult __rc = ::lsw::vkproto::lsw_vkCreateInstance(conn->transport(), pCreateInfo,
                                                               pAllocator, &server_instance);
    if (__rc != VK_SUCCESS) {
        ::lsw::icd::release_connection(conn);
        return __rc;
    }
    *pInstance = ::lsw::icd::wrap_server_handle(server_instance, conn);
    return __rc;
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDevice(VkPhysicalDevice physicalDevice,
                                                   const VkDeviceCreateInfo* pCreateInfo,
                                                   const VkAllocationCallbacks* pAllocator,
                                                   VkDevice* pDevice) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    VkDevice server_pDevice = VK_NULL_HANDLE;
    const VkResult __rc = ::lsw::vkproto::lsw_vkCreateDevice(
        conn->transport(), physicalDevice, pCreateInfo, pAllocator, &server_pDevice);
    if (__rc != VK_SUCCESS && __rc != VK_INCOMPLETE) {
        return __rc;
    }
    *pDevice = ::lsw::icd::wrap_server_handle(server_pDevice, conn);
    return __rc;
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceProperties(conn->transport(), physicalDevice,
                                                      pProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                                            VkPhysicalDeviceFeatures* pFeatures) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceFeatures(conn->transport(), physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceFormatProperties(conn->transport(), physicalDevice,
                                                            format, pFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling,
    VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkGetPhysicalDeviceImageFormatProperties(
        conn->transport(), physicalDevice, format, type, tiling, usage, flags,
        pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceMemoryProperties(conn->transport(), physicalDevice,
                                                            pMemoryProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceQueueFamilyProperties(
        conn->transport(), physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling,
    uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceSparseImageFormatProperties(
        conn->transport(), physicalDevice, format, type, samples, usage, tiling, pPropertyCount,
        pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkEnumerateDeviceExtensionProperties(
        conn->transport(), physicalDevice, pLayerName, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkEnumerateDeviceLayerProperties(conn->transport(), physicalDevice,
                                                                pPropertyCount, pProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                             VkPhysicalDeviceFeatures2* pFeatures) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceFeatures2(conn->transport(), physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceFormatProperties2(conn->transport(), physicalDevice,
                                                             format, pFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkGetPhysicalDeviceImageFormatProperties2(
        conn->transport(), physicalDevice, pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceMemoryProperties2(conn->transport(), physicalDevice,
                                                             pMemoryProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceQueueFamilyProperties2(
        conn->transport(), physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceSparseImageFormatProperties2(
        conn->transport(), physicalDevice, pFormatInfo, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkDeviceWaitIdle(VkDevice device) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkDeviceWaitIdle(conn->transport(), device);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueueWaitIdle(VkQueue queue) {
    auto* conn = ::lsw::icd::connection_of(queue);
    return ::lsw::vkproto::lsw_vkQueueWaitIdle(conn->transport(), queue);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkAllocateMemory(VkDevice device,
                                                     const VkMemoryAllocateInfo* pAllocateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkDeviceMemory* pMemory) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkAllocateMemory(conn->transport(), device, pAllocateInfo,
                                                pAllocator, pMemory);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                                             const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkFreeMemory(conn->transport(), device, memory, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBindBufferMemory(VkDevice device, VkBuffer buffer,
                                                       VkDeviceMemory memory,
                                                       VkDeviceSize memoryOffset) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkBindBufferMemory(conn->transport(), device, buffer, memory,
                                                  memoryOffset);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBindImageMemory(VkDevice device, VkImage image,
                                                      VkDeviceMemory memory,
                                                      VkDeviceSize memoryOffset) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkBindImageMemory(conn->transport(), device, image, memory,
                                                 memoryOffset);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetBufferMemoryRequirements(
    VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetBufferMemoryRequirements(conn->transport(), device, buffer,
                                                      pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetImageMemoryRequirements(
    VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetImageMemoryRequirements(conn->transport(), device, image,
                                                     pMemoryRequirements);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateBuffer(VkDevice device,
                                                   const VkBufferCreateInfo* pCreateInfo,
                                                   const VkAllocationCallbacks* pAllocator,
                                                   VkBuffer* pBuffer) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateBuffer(conn->transport(), device, pCreateInfo, pAllocator,
                                              pBuffer);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                                                const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyBuffer(conn->transport(), device, buffer, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateImage(VkDevice device,
                                                  const VkImageCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkImage* pImage) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateImage(conn->transport(), device, pCreateInfo, pAllocator,
                                             pImage);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyImage(VkDevice device, VkImage image,
                                               const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyImage(conn->transport(), device, image, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateImageView(VkDevice device,
                                                      const VkImageViewCreateInfo* pCreateInfo,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkImageView* pView) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateImageView(conn->transport(), device, pCreateInfo, pAllocator,
                                                 pView);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyImageView(VkDevice device, VkImageView imageView,
                                                   const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyImageView(conn->transport(), device, imageView, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateSampler(VkDevice device,
                                                    const VkSamplerCreateInfo* pCreateInfo,
                                                    const VkAllocationCallbacks* pAllocator,
                                                    VkSampler* pSampler) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateSampler(conn->transport(), device, pCreateInfo, pAllocator,
                                               pSampler);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroySampler(VkDevice device, VkSampler sampler,
                                                 const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroySampler(conn->transport(), device, sampler, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateSemaphore(VkDevice device,
                                                      const VkSemaphoreCreateInfo* pCreateInfo,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkSemaphore* pSemaphore) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateSemaphore(conn->transport(), device, pCreateInfo, pAllocator,
                                                 pSemaphore);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                                   const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroySemaphore(conn->transport(), device, semaphore, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateFence(VkDevice device,
                                                  const VkFenceCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkFence* pFence) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateFence(conn->transport(), device, pCreateInfo, pAllocator,
                                             pFence);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyFence(VkDevice device, VkFence fence,
                                               const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyFence(conn->transport(), device, fence, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetFences(VkDevice device, uint32_t fenceCount,
                                                  const VkFence* pFences) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkResetFences(conn->transport(), device, fenceCount, pFences);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkWaitForFences(VkDevice device, uint32_t fenceCount,
                                                    const VkFence* pFences, VkBool32 waitAll,
                                                    uint64_t timeout) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkWaitForFences(conn->transport(), device, fenceCount, pFences,
                                               waitAll, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetFenceStatus(VkDevice device, VkFence fence) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetFenceStatus(conn->transport(), device, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateCommandPool(VkDevice device,
                                                        const VkCommandPoolCreateInfo* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkCommandPool* pCommandPool) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateCommandPool(conn->transport(), device, pCreateInfo,
                                                   pAllocator, pCommandPool);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                                     const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyCommandPool(conn->transport(), device, commandPool, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                                                       VkCommandPoolResetFlags flags) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkResetCommandPool(conn->transport(), device, commandPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    return ::lsw::vkproto::lsw_vkBeginCommandBuffer(conn->transport(), commandBuffer, pBeginInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    return ::lsw::vkproto::lsw_vkEndCommandBuffer(conn->transport(), commandBuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetCommandBuffer(VkCommandBuffer commandBuffer,
                                                         VkCommandBufferResetFlags flags) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    return ::lsw::vkproto::lsw_vkResetCommandBuffer(conn->transport(), commandBuffer, flags);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                                                      const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyShaderModule(conn->transport(), device, shaderModule, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo,
                          const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateShaderModule(conn->transport(), device, pCreateInfo,
                                                    pAllocator, pShaderModule);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreatePipelineLayout(
    VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreatePipelineLayout(conn->transport(), device, pCreateInfo,
                                                      pAllocator, pPipelineLayout);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyPipelineLayout(VkDevice device,
                                                        VkPipelineLayout pipelineLayout,
                                                        const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyPipelineLayout(conn->transport(), device, pipelineLayout,
                                                pAllocator);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                                                  const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyPipeline(conn->transport(), device, pipeline, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreatePipelineCache(
    VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreatePipelineCache(conn->transport(), device, pCreateInfo,
                                                     pAllocator, pPipelineCache);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyPipelineCache(VkDevice device,
                                                       VkPipelineCache pipelineCache,
                                                       const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyPipelineCache(conn->transport(), device, pipelineCache,
                                               pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateRenderPass(VkDevice device,
                                                       const VkRenderPassCreateInfo* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkRenderPass* pRenderPass) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateRenderPass(conn->transport(), device, pCreateInfo,
                                                  pAllocator, pRenderPass);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                                                    const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyRenderPass(conn->transport(), device, renderPass, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateFramebuffer(VkDevice device,
                                                        const VkFramebufferCreateInfo* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkFramebuffer* pFramebuffer) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateFramebuffer(conn->transport(), device, pCreateInfo,
                                                   pAllocator, pFramebuffer);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                                                     const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyFramebuffer(conn->transport(), device, framebuffer, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDescriptorSetLayout(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateDescriptorSetLayout(conn->transport(), device, pCreateInfo,
                                                           pAllocator, pSetLayout);
}

VKAPI_ATTR void VKAPI_CALL
Wrap_vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                                  const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyDescriptorSetLayout(conn->transport(), device, descriptorSetLayout,
                                                     pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDescriptorPool(
    VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateDescriptorPool(conn->transport(), device, pCreateInfo,
                                                      pAllocator, pDescriptorPool);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyDescriptorPool(VkDevice device,
                                                        VkDescriptorPool descriptorPool,
                                                        const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyDescriptorPool(conn->transport(), device, descriptorPool,
                                                pAllocator);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                                     uint32_t commandBufferCount,
                                                     const VkCommandBuffer* pCommandBuffers) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkFreeCommandBuffers(conn->transport(), device, commandPool,
                                             commandBufferCount, pCommandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueueSubmit(VkQueue queue, uint32_t submitCount,
                                                  const VkSubmitInfo* pSubmits, VkFence fence) {
    auto* conn = ::lsw::icd::connection_of(queue);
    return ::lsw::vkproto::lsw_vkQueueSubmit(conn->transport(), queue, submitCount, pSubmits,
                                             fence);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                                                  VkPipelineBindPoint pipelineBindPoint,
                                                  VkPipeline pipeline) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBindPipeline(conn->transport(), commandBuffer, pipelineBindPoint,
                                          pipeline);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetViewport(VkCommandBuffer commandBuffer,
                                                 uint32_t firstViewport, uint32_t viewportCount,
                                                 const VkViewport* pViewports) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetViewport(conn->transport(), commandBuffer, firstViewport,
                                         viewportCount, pViewports);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetScissor(VkCommandBuffer commandBuffer,
                                                uint32_t firstScissor, uint32_t scissorCount,
                                                const VkRect2D* pScissors) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetScissor(conn->transport(), commandBuffer, firstScissor,
                                        scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                                                       uint32_t firstBinding, uint32_t bindingCount,
                                                       const VkBuffer* pBuffers,
                                                       const VkDeviceSize* pOffsets) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBindVertexBuffers(conn->transport(), commandBuffer, firstBinding,
                                               bindingCount, pBuffers, pOffsets);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                     VkDeviceSize offset, VkIndexType indexType) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBindIndexBuffer(conn->transport(), commandBuffer, buffer, offset,
                                             indexType);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
    uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets,
    uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBindDescriptorSets(
        conn->transport(), commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount,
        pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                                          uint32_t instanceCount, uint32_t firstVertex,
                                          uint32_t firstInstance) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDraw(conn->transport(), commandBuffer, vertexCount, instanceCount,
                                  firstVertex, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                                                 uint32_t instanceCount, uint32_t firstIndex,
                                                 int32_t vertexOffset, uint32_t firstInstance) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDrawIndexed(conn->transport(), commandBuffer, indexCount,
                                         instanceCount, firstIndex, vertexOffset, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdPipelineBarrier(
        conn->transport(), commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
        memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
        imageMemoryBarrierCount, pImageMemoryBarriers);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                                                VkBuffer dstBuffer, uint32_t regionCount,
                                                const VkBufferCopy* pRegions) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyBuffer(conn->transport(), commandBuffer, srcBuffer, dstBuffer,
                                        regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                                     const VkRenderPassBeginInfo* pRenderPassBegin,
                                                     VkSubpassContents contents) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBeginRenderPass(conn->transport(), commandBuffer, pRenderPassBegin,
                                             contents);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdEndRenderPass(conn->transport(), commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdNextSubpass(VkCommandBuffer commandBuffer,
                                                 VkSubpassContents contents) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdNextSubpass(conn->transport(), commandBuffer, contents);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdPushConstants(VkCommandBuffer commandBuffer,
                                                   VkPipelineLayout layout,
                                                   VkShaderStageFlags stageFlags, uint32_t offset,
                                                   uint32_t size, const void* pValues) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdPushConstants(conn->transport(), commandBuffer, layout, stageFlags,
                                           offset, size, pValues);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface,
    VkBool32* pSupported) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkGetPhysicalDeviceSurfaceSupportKHR(
        conn->transport(), physicalDevice, queueFamilyIndex, surface, pSupported);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        conn->transport(), physicalDevice, surface, pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount,
    VkSurfaceFormatKHR* pSurfaceFormats) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkGetPhysicalDeviceSurfaceFormatsKHR(
        conn->transport(), physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount,
    VkPresentModeKHR* pPresentModes) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkGetPhysicalDeviceSurfacePresentModesKHR(
        conn->transport(), physicalDevice, surface, pPresentModeCount, pPresentModes);
}

VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                          const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateSwapchainKHR(conn->transport(), device, pCreateInfo,
                                                    pAllocator, pSwapchain);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                      const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroySwapchainKHR(conn->transport(), device, swapchain, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetSwapchainImagesKHR(VkDevice device,
                                                            VkSwapchainKHR swapchain,
                                                            uint32_t* pSwapchainImageCount,
                                                            VkImage* pSwapchainImages) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetSwapchainImagesKHR(conn->transport(), device, swapchain,
                                                       pSwapchainImageCount, pSwapchainImages);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                          uint64_t timeout, VkSemaphore semaphore,
                                                          VkFence fence, uint32_t* pImageIndex) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkAcquireNextImageKHR(conn->transport(), device, swapchain, timeout,
                                                     semaphore, fence, pImageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueuePresentKHR(VkQueue queue,
                                                      const VkPresentInfoKHR* pPresentInfo) {
    auto* conn = ::lsw::icd::connection_of(queue);
    return ::lsw::vkproto::lsw_vkQueuePresentKHR(conn->transport(), queue, pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateGraphicsPipelines(conn->transport(), device, pipelineCache,
                                                         createInfoCount, pCreateInfos, pAllocator,
                                                         pPipelines);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkUpdateDescriptorSets(
    VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites,
    uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkUpdateDescriptorSets(conn->transport(), device, descriptorWriteCount,
                                               pDescriptorWrites, descriptorCopyCount,
                                               pDescriptorCopies);
}

VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo,
                              VkCommandBuffer* pCommandBuffers) {
    auto* conn = ::lsw::icd::connection_of(device);
    std::vector<VkCommandBuffer> server_pCommandBuffers(pAllocateInfo->commandBufferCount,
                                                        VK_NULL_HANDLE);
    const VkResult __rc = ::lsw::vkproto::lsw_vkAllocateCommandBuffers(
        conn->transport(), device, pAllocateInfo, server_pCommandBuffers.data());
    if (__rc != VK_SUCCESS && __rc != VK_INCOMPLETE) {
        return __rc;
    }
    for (std::uint32_t k = 0; k < static_cast<std::uint32_t>(pAllocateInfo->commandBufferCount);
         ++k) {
        pCommandBuffers[k] = ::lsw::icd::wrap_server_handle(server_pCommandBuffers[k], conn);
    }
    return __rc;
}

VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo,
                              VkDescriptorSet* pDescriptorSets) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkAllocateDescriptorSets(conn->transport(), device, pAllocateInfo,
                                                        pDescriptorSets);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkFreeDescriptorSets(VkDevice device,
                                                         VkDescriptorPool descriptorPool,
                                                         uint32_t descriptorSetCount,
                                                         const VkDescriptorSet* pDescriptorSets) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkFreeDescriptorSets(conn->transport(), device, descriptorPool,
                                                    descriptorSetCount, pDescriptorSets);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateComputePipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateComputePipelines(conn->transport(), device, pipelineCache,
                                                        createInfoCount, pCreateInfos, pAllocator,
                                                        pPipelines);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetDescriptorPool(VkDevice device,
                                                          VkDescriptorPool descriptorPool,
                                                          VkDescriptorPoolResetFlags flags) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkResetDescriptorPool(conn->transport(), device, descriptorPool,
                                                     flags);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateEvent(VkDevice device,
                                                  const VkEventCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkEvent* pEvent) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateEvent(conn->transport(), device, pCreateInfo, pAllocator,
                                             pEvent);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyEvent(VkDevice device, VkEvent event,
                                               const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyEvent(conn->transport(), device, event, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetEventStatus(VkDevice device, VkEvent event) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetEventStatus(conn->transport(), device, event);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkSetEvent(VkDevice device, VkEvent event) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkSetEvent(conn->transport(), device, event);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkResetEvent(VkDevice device, VkEvent event) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkResetEvent(conn->transport(), device, event);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                                              VkPipelineStageFlags stageMask) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetEvent(conn->transport(), commandBuffer, event, stageMask);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                                                VkPipelineStageFlags stageMask) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdResetEvent(conn->transport(), commandBuffer, event, stageMask);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdWaitEvents(
    VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
    VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
    uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdWaitEvents(
        conn->transport(), commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask,
        memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
        imageMemoryBarrierCount, pImageMemoryBarriers);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                               VkImageLayout srcImageLayout, VkImage dstImage,
                                               VkImageLayout dstImageLayout, uint32_t regionCount,
                                               const VkImageCopy* pRegions) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyImage(conn->transport(), commandBuffer, srcImage, srcImageLayout,
                                       dstImage, dstImageLayout, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                                                       VkBuffer srcBuffer, VkImage dstImage,
                                                       VkImageLayout dstImageLayout,
                                                       uint32_t regionCount,
                                                       const VkBufferImageCopy* pRegions) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyBufferToImage(conn->transport(), commandBuffer, srcBuffer,
                                               dstImage, dstImageLayout, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                                                       VkImage srcImage,
                                                       VkImageLayout srcImageLayout,
                                                       VkBuffer dstBuffer, uint32_t regionCount,
                                                       const VkBufferImageCopy* pRegions) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyImageToBuffer(conn->transport(), commandBuffer, srcImage,
                                               srcImageLayout, dstBuffer, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                               VkImageLayout srcImageLayout, VkImage dstImage,
                                               VkImageLayout dstImageLayout, uint32_t regionCount,
                                               const VkImageBlit* pRegions, VkFilter filter) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBlitImage(conn->transport(), commandBuffer, srcImage, srcImageLayout,
                                       dstImage, dstImageLayout, regionCount, pRegions, filter);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                                                     VkImageLayout imageLayout,
                                                     const VkClearColorValue* pColor,
                                                     uint32_t rangeCount,
                                                     const VkImageSubresourceRange* pRanges) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdClearColorImage(conn->transport(), commandBuffer, image, imageLayout,
                                             pColor, rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdClearDepthStencilImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
    const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount,
    const VkImageSubresourceRange* pRanges) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdClearDepthStencilImage(
        conn->transport(), commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdClearAttachments(VkCommandBuffer commandBuffer,
                                                      uint32_t attachmentCount,
                                                      const VkClearAttachment* pAttachments,
                                                      uint32_t rectCount,
                                                      const VkClearRect* pRects) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdClearAttachments(conn->transport(), commandBuffer, attachmentCount,
                                              pAttachments, rectCount, pRects);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                                VkDeviceSize dstOffset, VkDeviceSize size,
                                                uint32_t data) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdFillBuffer(conn->transport(), commandBuffer, dstBuffer, dstOffset,
                                        size, data);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                                  VkDeviceSize dstOffset, VkDeviceSize dataSize,
                                                  const void* pData) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdUpdateBuffer(conn->transport(), commandBuffer, dstBuffer, dstOffset,
                                          dataSize, pData);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetLineWidth(conn->transport(), commandBuffer, lineWidth);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthBias(VkCommandBuffer commandBuffer,
                                                  float depthBiasConstantFactor,
                                                  float depthBiasClamp,
                                                  float depthBiasSlopeFactor) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetDepthBias(conn->transport(), commandBuffer, depthBiasConstantFactor,
                                          depthBiasClamp, depthBiasSlopeFactor);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetBlendConstants(VkCommandBuffer commandBuffer,
                                                       const float* blendConstants) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetBlendConstants(conn->transport(), commandBuffer, blendConstants);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthBounds(VkCommandBuffer commandBuffer,
                                                    float minDepthBounds, float maxDepthBounds) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetDepthBounds(conn->transport(), commandBuffer, minDepthBounds,
                                            maxDepthBounds);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                                                           VkStencilFaceFlags faceMask,
                                                           uint32_t compareMask) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetStencilCompareMask(conn->transport(), commandBuffer, faceMask,
                                                   compareMask);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                                                         VkStencilFaceFlags faceMask,
                                                         uint32_t writeMask) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetStencilWriteMask(conn->transport(), commandBuffer, faceMask,
                                                 writeMask);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilReference(VkCommandBuffer commandBuffer,
                                                         VkStencilFaceFlags faceMask,
                                                         uint32_t reference) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetStencilReference(conn->transport(), commandBuffer, faceMask,
                                                 reference);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX,
                                              uint32_t groupCountY, uint32_t groupCountZ) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDispatch(conn->transport(), commandBuffer, groupCountX, groupCountY,
                                      groupCountZ);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDispatchIndirect(VkCommandBuffer commandBuffer,
                                                      VkBuffer buffer, VkDeviceSize offset) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDispatchIndirect(conn->transport(), commandBuffer, buffer, offset);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                  VkDeviceSize offset, uint32_t drawCount,
                                                  uint32_t stride) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDrawIndirect(conn->transport(), commandBuffer, buffer, offset,
                                          drawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                                                         VkBuffer buffer, VkDeviceSize offset,
                                                         uint32_t drawCount, uint32_t stride) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDrawIndexedIndirect(conn->transport(), commandBuffer, buffer, offset,
                                                 drawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdExecuteCommands(VkCommandBuffer commandBuffer,
                                                     uint32_t commandBufferCount,
                                                     const VkCommandBuffer* pCommandBuffers) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdExecuteCommands(conn->transport(), commandBuffer, commandBufferCount,
                                             pCommandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkAcquireNextImage2KHR(
    VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkAcquireNextImage2KHR(conn->transport(), device, pAcquireInfo,
                                                      pImageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                                                        const VkBindBufferMemoryInfo* pBindInfos) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkBindBufferMemory2(conn->transport(), device, bindInfoCount,
                                                   pBindInfos);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                                                       const VkBindImageMemoryInfo* pBindInfos) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkBindImageMemory2(conn->transport(), device, bindInfoCount,
                                                  pBindInfos);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBeginQuery(VkCommandBuffer commandBuffer,
                                                VkQueryPool queryPool, uint32_t query,
                                                VkQueryControlFlags flags) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBeginQuery(conn->transport(), commandBuffer, queryPool, query, flags);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                                                      const VkRenderPassBeginInfo* pRenderPassBegin,
                                                      const VkSubpassBeginInfo* pSubpassBeginInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBeginRenderPass2(conn->transport(), commandBuffer, pRenderPassBegin,
                                              pSubpassBeginInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBeginRendering(VkCommandBuffer commandBuffer,
                                                    const VkRenderingInfo* pRenderingInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBeginRendering(conn->transport(), commandBuffer, pRenderingInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBindVertexBuffers2(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes,
    const VkDeviceSize* pStrides) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBindVertexBuffers2(conn->transport(), commandBuffer, firstBinding,
                                                bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdBlitImage2(VkCommandBuffer commandBuffer,
                                                const VkBlitImageInfo2* pBlitImageInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdBlitImage2(conn->transport(), commandBuffer, pBlitImageInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyBuffer2(VkCommandBuffer commandBuffer,
                                                 const VkCopyBufferInfo2* pCopyBufferInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyBuffer2(conn->transport(), commandBuffer, pCopyBufferInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyBufferToImage2(
    VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyBufferToImage2(conn->transport(), commandBuffer,
                                                pCopyBufferToImageInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyImage2(VkCommandBuffer commandBuffer,
                                                const VkCopyImageInfo2* pCopyImageInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyImage2(conn->transport(), commandBuffer, pCopyImageInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyImageToBuffer2(
    VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyImageToBuffer2(conn->transport(), commandBuffer,
                                                pCopyImageToBufferInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdCopyQueryPoolResults(
    VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount,
    VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdCopyQueryPoolResults(conn->transport(), commandBuffer, queryPool,
                                                  firstQuery, queryCount, dstBuffer, dstOffset,
                                                  stride, flags);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDispatchBase(VkCommandBuffer commandBuffer,
                                                  uint32_t baseGroupX, uint32_t baseGroupY,
                                                  uint32_t baseGroupZ, uint32_t groupCountX,
                                                  uint32_t groupCountY, uint32_t groupCountZ) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDispatchBase(conn->transport(), commandBuffer, baseGroupX, baseGroupY,
                                          baseGroupZ, groupCountX, groupCountY, groupCountZ);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndexedIndirectCount(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer,
    VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDrawIndexedIndirectCount(conn->transport(), commandBuffer, buffer,
                                                      offset, countBuffer, countBufferOffset,
                                                      maxDrawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer,
                                                       VkBuffer buffer, VkDeviceSize offset,
                                                       VkBuffer countBuffer,
                                                       VkDeviceSize countBufferOffset,
                                                       uint32_t maxDrawCount, uint32_t stride) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdDrawIndirectCount(conn->transport(), commandBuffer, buffer, offset,
                                               countBuffer, countBufferOffset, maxDrawCount,
                                               stride);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                              uint32_t query) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdEndQuery(conn->transport(), commandBuffer, queryPool, query);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdEndRenderPass2(VkCommandBuffer commandBuffer,
                                                    const VkSubpassEndInfo* pSubpassEndInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdEndRenderPass2(conn->transport(), commandBuffer, pSubpassEndInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdEndRendering(VkCommandBuffer commandBuffer) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdEndRendering(conn->transport(), commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdNextSubpass2(VkCommandBuffer commandBuffer,
                                                  const VkSubpassBeginInfo* pSubpassBeginInfo,
                                                  const VkSubpassEndInfo* pSubpassEndInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdNextSubpass2(conn->transport(), commandBuffer, pSubpassBeginInfo,
                                          pSubpassEndInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                                                      const VkDependencyInfo* pDependencyInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdPipelineBarrier2(conn->transport(), commandBuffer, pDependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                                                 VkPipelineStageFlags2 stageMask) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdResetEvent2(conn->transport(), commandBuffer, event, stageMask);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResetQueryPool(VkCommandBuffer commandBuffer,
                                                    VkQueryPool queryPool, uint32_t firstQuery,
                                                    uint32_t queryCount) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdResetQueryPool(conn->transport(), commandBuffer, queryPool, firstQuery,
                                            queryCount);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                                  VkImageLayout srcImageLayout, VkImage dstImage,
                                                  VkImageLayout dstImageLayout,
                                                  uint32_t regionCount,
                                                  const VkImageResolve* pRegions) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdResolveImage(conn->transport(), commandBuffer, srcImage,
                                          srcImageLayout, dstImage, dstImageLayout, regionCount,
                                          pRegions);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdResolveImage2(VkCommandBuffer commandBuffer,
                                                   const VkResolveImageInfo2* pResolveImageInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdResolveImage2(conn->transport(), commandBuffer, pResolveImageInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetCullMode(VkCommandBuffer commandBuffer,
                                                 VkCullModeFlags cullMode) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetCullMode(conn->transport(), commandBuffer, cullMode);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer,
                                                        VkBool32 depthBiasEnable) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetDepthBiasEnable(conn->transport(), commandBuffer, depthBiasEnable);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer,
                                                              VkBool32 depthBoundsTestEnable) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetDepthBoundsTestEnable(conn->transport(), commandBuffer,
                                                      depthBoundsTestEnable);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer,
                                                       VkCompareOp depthCompareOp) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetDepthCompareOp(conn->transport(), commandBuffer, depthCompareOp);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer,
                                                        VkBool32 depthTestEnable) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetDepthTestEnable(conn->transport(), commandBuffer, depthTestEnable);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer,
                                                         VkBool32 depthWriteEnable) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetDepthWriteEnable(conn->transport(), commandBuffer,
                                                 depthWriteEnable);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetDeviceMask(VkCommandBuffer commandBuffer,
                                                   uint32_t deviceMask) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetDeviceMask(conn->transport(), commandBuffer, deviceMask);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                                               const VkDependencyInfo* pDependencyInfo) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetEvent2(conn->transport(), commandBuffer, event, pDependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetFrontFace(VkCommandBuffer commandBuffer,
                                                  VkFrontFace frontFace) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetFrontFace(conn->transport(), commandBuffer, frontFace);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer,
                                                               VkBool32 primitiveRestartEnable) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetPrimitiveRestartEnable(conn->transport(), commandBuffer,
                                                       primitiveRestartEnable);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer,
                                                          VkPrimitiveTopology primitiveTopology) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetPrimitiveTopology(conn->transport(), commandBuffer,
                                                  primitiveTopology);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer,
                                                                VkBool32 rasterizerDiscardEnable) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetRasterizerDiscardEnable(conn->transport(), commandBuffer,
                                                        rasterizerDiscardEnable);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer,
                                                         uint32_t scissorCount,
                                                         const VkRect2D* pScissors) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetScissorWithCount(conn->transport(), commandBuffer, scissorCount,
                                                 pScissors);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilOp(VkCommandBuffer commandBuffer,
                                                  VkStencilFaceFlags faceMask, VkStencilOp failOp,
                                                  VkStencilOp passOp, VkStencilOp depthFailOp,
                                                  VkCompareOp compareOp) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetStencilOp(conn->transport(), commandBuffer, faceMask, failOp,
                                          passOp, depthFailOp, compareOp);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer,
                                                          VkBool32 stencilTestEnable) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetStencilTestEnable(conn->transport(), commandBuffer,
                                                  stencilTestEnable);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer,
                                                          uint32_t viewportCount,
                                                          const VkViewport* pViewports) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdSetViewportWithCount(conn->transport(), commandBuffer, viewportCount,
                                                  pViewports);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount,
                                                 const VkEvent* pEvents,
                                                 const VkDependencyInfo* pDependencyInfos) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdWaitEvents2(conn->transport(), commandBuffer, eventCount, pEvents,
                                         pDependencyInfos);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdWriteTimestamp(VkCommandBuffer commandBuffer,
                                                    VkPipelineStageFlagBits pipelineStage,
                                                    VkQueryPool queryPool, uint32_t query) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdWriteTimestamp(conn->transport(), commandBuffer, pipelineStage,
                                            queryPool, query);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                                                     VkPipelineStageFlags2 stage,
                                                     VkQueryPool queryPool, uint32_t query) {
    auto* conn = ::lsw::icd::connection_of(commandBuffer);
    ::lsw::vkproto::lsw_vkCmdWriteTimestamp2(conn->transport(), commandBuffer, stage, queryPool,
                                             query);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateBufferView(VkDevice device,
                                                       const VkBufferViewCreateInfo* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkBufferView* pView) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateBufferView(conn->transport(), device, pCreateInfo,
                                                  pAllocator, pView);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDescriptorUpdateTemplate(
    VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateDescriptorUpdateTemplate(
        conn->transport(), device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreatePrivateDataSlot(
    VkDevice device, const VkPrivateDataSlotCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkPrivateDataSlot* pPrivateDataSlot) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreatePrivateDataSlot(conn->transport(), device, pCreateInfo,
                                                       pAllocator, pPrivateDataSlot);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateQueryPool(VkDevice device,
                                                      const VkQueryPoolCreateInfo* pCreateInfo,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkQueryPool* pQueryPool) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateQueryPool(conn->transport(), device, pCreateInfo, pAllocator,
                                                 pQueryPool);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateRenderPass2(VkDevice device,
                                                        const VkRenderPassCreateInfo2* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkRenderPass* pRenderPass) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateRenderPass2(conn->transport(), device, pCreateInfo,
                                                   pAllocator, pRenderPass);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateSamplerYcbcrConversion(
    VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkCreateSamplerYcbcrConversion(
        conn->transport(), device, pCreateInfo, pAllocator, pYcbcrConversion);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyBufferView(VkDevice device, VkBufferView bufferView,
                                                    const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyBufferView(conn->transport(), device, bufferView, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyDescriptorUpdateTemplate(
    VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyDescriptorUpdateTemplate(conn->transport(), device,
                                                          descriptorUpdateTemplate, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyPrivateDataSlot(VkDevice device,
                                                         VkPrivateDataSlot privateDataSlot,
                                                         const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyPrivateDataSlot(conn->transport(), device, privateDataSlot,
                                                 pAllocator);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                                                   const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroyQueryPool(conn->transport(), device, queryPool, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
Wrap_vkDestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion,
                                     const VkAllocationCallbacks* pAllocator) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkDestroySamplerYcbcrConversion(conn->transport(), device, ycbcrConversion,
                                                        pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkEnumeratePhysicalDeviceGroups(
    VkInstance instance, uint32_t* pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) {
    auto* conn = ::lsw::icd::connection_of(instance);
    return ::lsw::vkproto::lsw_vkEnumeratePhysicalDeviceGroups(
        conn->transport(), instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkFlushMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkFlushMappedMemoryRanges(conn->transport(), device,
                                                         memoryRangeCount, pMemoryRanges);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
Wrap_vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetBufferDeviceAddress(conn->transport(), device, pInfo);
}

VKAPI_ATTR void VKAPI_CALL
Wrap_vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo,
                                    VkMemoryRequirements2* pMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetBufferMemoryRequirements2(conn->transport(), device, pInfo,
                                                       pMemoryRequirements);
}

VKAPI_ATTR uint64_t VKAPI_CALL
Wrap_vkGetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetBufferOpaqueCaptureAddress(conn->transport(), device, pInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDescriptorSetLayoutSupport(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    VkDescriptorSetLayoutSupport* pSupport) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetDescriptorSetLayoutSupport(conn->transport(), device, pCreateInfo,
                                                        pSupport);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceBufferMemoryRequirements(
    VkDevice device, const VkDeviceBufferMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetDeviceBufferMemoryRequirements(conn->transport(), device, pInfo,
                                                            pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceGroupPeerMemoryFeatures(
    VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex,
    VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetDeviceGroupPeerMemoryFeatures(conn->transport(), device, heapIndex,
                                                           localDeviceIndex, remoteDeviceIndex,
                                                           pPeerMemoryFeatures);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetDeviceGroupPresentCapabilitiesKHR(
    VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetDeviceGroupPresentCapabilitiesKHR(
        conn->transport(), device, pDeviceGroupPresentCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetDeviceGroupSurfacePresentModesKHR(
    VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetDeviceGroupSurfacePresentModesKHR(conn->transport(), device,
                                                                      surface, pModes);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceImageMemoryRequirements(
    VkDevice device, const VkDeviceImageMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetDeviceImageMemoryRequirements(conn->transport(), device, pInfo,
                                                           pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceImageSparseMemoryRequirements(
    VkDevice device, const VkDeviceImageMemoryRequirements* pInfo,
    uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetDeviceImageSparseMemoryRequirements(
        conn->transport(), device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                                                            VkDeviceSize* pCommittedMemoryInBytes) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetDeviceMemoryCommitment(conn->transport(), device, memory,
                                                    pCommittedMemoryInBytes);
}

VKAPI_ATTR uint64_t VKAPI_CALL Wrap_vkGetDeviceMemoryOpaqueCaptureAddress(
    VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetDeviceMemoryOpaqueCaptureAddress(conn->transport(), device,
                                                                     pInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetDeviceQueue2(VkDevice device,
                                                  const VkDeviceQueueInfo2* pQueueInfo,
                                                  VkQueue* pQueue) {
    auto* conn = ::lsw::icd::connection_of(device);
    VkQueue server_pQueue = VK_NULL_HANDLE;
    ::lsw::vkproto::lsw_vkGetDeviceQueue2(conn->transport(), device, pQueueInfo, &server_pQueue);
    *pQueue = ::lsw::icd::wrap_server_handle(server_pQueue, conn);
}

VKAPI_ATTR void VKAPI_CALL
Wrap_vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo,
                                   VkMemoryRequirements2* pMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetImageMemoryRequirements2(conn->transport(), device, pInfo,
                                                      pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetImageSparseMemoryRequirements(
    VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements* pSparseMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetImageSparseMemoryRequirements(
        conn->transport(), device, image, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetImageSparseMemoryRequirements2(
    VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo,
    uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetImageSparseMemoryRequirements2(
        conn->transport(), device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetImageSubresourceLayout(VkDevice device, VkImage image,
                                                            const VkImageSubresource* pSubresource,
                                                            VkSubresourceLayout* pLayout) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetImageSubresourceLayout(conn->transport(), device, image, pSubresource,
                                                    pLayout);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo,
    VkExternalBufferProperties* pExternalBufferProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceExternalBufferProperties(
        conn->transport(), physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo,
    VkExternalFenceProperties* pExternalFenceProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceExternalFenceProperties(
        conn->transport(), physicalDevice, pExternalFenceInfo, pExternalFenceProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties* pExternalSemaphoreProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    ::lsw::vkproto::lsw_vkGetPhysicalDeviceExternalSemaphoreProperties(
        conn->transport(), physicalDevice, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkGetPhysicalDevicePresentRectanglesKHR(
        conn->transport(), physicalDevice, surface, pRectCount, pRects);
}

VKAPI_ATTR VkResult VKAPI_CALL
Wrap_vkGetPhysicalDeviceToolProperties(VkPhysicalDevice physicalDevice, uint32_t* pToolCount,
                                       VkPhysicalDeviceToolProperties* pToolProperties) {
    auto* conn = ::lsw::icd::connection_of(physicalDevice);
    return ::lsw::vkproto::lsw_vkGetPhysicalDeviceToolProperties(conn->transport(), physicalDevice,
                                                                 pToolCount, pToolProperties);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetPrivateData(VkDevice device, VkObjectType objectType,
                                                 uint64_t objectHandle,
                                                 VkPrivateDataSlot privateDataSlot,
                                                 uint64_t* pData) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetPrivateData(conn->transport(), device, objectType, objectHandle,
                                         privateDataSlot, pData);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass,
                                                           VkExtent2D* pGranularity) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkGetRenderAreaGranularity(conn->transport(), device, renderPass,
                                                   pGranularity);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetSemaphoreCounterValue(VkDevice device,
                                                               VkSemaphore semaphore,
                                                               uint64_t* pValue) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkGetSemaphoreCounterValue(conn->transport(), device, semaphore,
                                                          pValue);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkInvalidateMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkInvalidateMappedMemoryRanges(conn->transport(), device,
                                                              memoryRangeCount, pMemoryRanges);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
                                                          uint32_t srcCacheCount,
                                                          const VkPipelineCache* pSrcCaches) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkMergePipelineCaches(conn->transport(), device, dstCache,
                                                     srcCacheCount, pSrcCaches);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount,
                                                      const VkBindSparseInfo* pBindInfo,
                                                      VkFence fence) {
    auto* conn = ::lsw::icd::connection_of(queue);
    return ::lsw::vkproto::lsw_vkQueueBindSparse(conn->transport(), queue, bindInfoCount, pBindInfo,
                                                 fence);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueueSubmit2(VkQueue queue, uint32_t submitCount,
                                                   const VkSubmitInfo2* pSubmits, VkFence fence) {
    auto* conn = ::lsw::icd::connection_of(queue);
    return ::lsw::vkproto::lsw_vkQueueSubmit2(conn->transport(), queue, submitCount, pSubmits,
                                              fence);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkResetQueryPool(VkDevice device, VkQueryPool queryPool,
                                                 uint32_t firstQuery, uint32_t queryCount) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkResetQueryPool(conn->transport(), device, queryPool, firstQuery,
                                         queryCount);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkSetPrivateData(VkDevice device, VkObjectType objectType,
                                                     uint64_t objectHandle,
                                                     VkPrivateDataSlot privateDataSlot,
                                                     uint64_t data) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkSetPrivateData(conn->transport(), device, objectType, objectHandle,
                                                privateDataSlot, data);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkSignalSemaphore(VkDevice device,
                                                      const VkSemaphoreSignalInfo* pSignalInfo) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkSignalSemaphore(conn->transport(), device, pSignalInfo);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkTrimCommandPool(VkDevice device, VkCommandPool commandPool,
                                                  VkCommandPoolTrimFlags flags) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkTrimCommandPool(conn->transport(), device, commandPool, flags);
}

VKAPI_ATTR void VKAPI_CALL Wrap_vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    auto* conn = ::lsw::icd::connection_of(device);
    ::lsw::vkproto::lsw_vkUnmapMemory(conn->transport(), device, memory);
}

VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkWaitSemaphores(VkDevice device,
                                                     const VkSemaphoreWaitInfo* pWaitInfo,
                                                     uint64_t timeout) {
    auto* conn = ::lsw::icd::connection_of(device);
    return ::lsw::vkproto::lsw_vkWaitSemaphores(conn->transport(), device, pWaitInfo, timeout);
}

namespace {
const std::array<Entry, 219> kWrapperTable = {{
    Entry{"vkDestroyInstance", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyInstance)},
    Entry{"vkDestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyDevice)},
    Entry{"vkGetDeviceQueue", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceQueue)},
    Entry{"vkEnumeratePhysicalDevices",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkEnumeratePhysicalDevices)},
    Entry{"vkGetPhysicalDeviceProperties2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceProperties2)},
    Entry{"vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateInstance)},
    Entry{"vkCreateDevice", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateDevice)},
    Entry{"vkGetPhysicalDeviceProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceProperties)},
    Entry{"vkGetPhysicalDeviceFeatures",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceFeatures)},
    Entry{"vkGetPhysicalDeviceFormatProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceFormatProperties)},
    Entry{"vkGetPhysicalDeviceImageFormatProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceImageFormatProperties)},
    Entry{"vkGetPhysicalDeviceMemoryProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceMemoryProperties)},
    Entry{"vkGetPhysicalDeviceQueueFamilyProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceQueueFamilyProperties)},
    Entry{
        "vkGetPhysicalDeviceSparseImageFormatProperties",
        reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceSparseImageFormatProperties)},
    Entry{"vkEnumerateDeviceExtensionProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkEnumerateDeviceExtensionProperties)},
    Entry{"vkEnumerateDeviceLayerProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkEnumerateDeviceLayerProperties)},
    Entry{"vkGetPhysicalDeviceFeatures2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceFeatures2)},
    Entry{"vkGetPhysicalDeviceFormatProperties2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceFormatProperties2)},
    Entry{"vkGetPhysicalDeviceImageFormatProperties2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceImageFormatProperties2)},
    Entry{"vkGetPhysicalDeviceMemoryProperties2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceMemoryProperties2)},
    Entry{"vkGetPhysicalDeviceQueueFamilyProperties2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceQueueFamilyProperties2)},
    Entry{"vkGetPhysicalDeviceSparseImageFormatProperties2",
          reinterpret_cast<PFN_vkVoidFunction>(
              &Wrap_vkGetPhysicalDeviceSparseImageFormatProperties2)},
    Entry{"vkDeviceWaitIdle", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDeviceWaitIdle)},
    Entry{"vkQueueWaitIdle", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkQueueWaitIdle)},
    Entry{"vkAllocateMemory", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkAllocateMemory)},
    Entry{"vkFreeMemory", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkFreeMemory)},
    Entry{"vkBindBufferMemory", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkBindBufferMemory)},
    Entry{"vkBindImageMemory", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkBindImageMemory)},
    Entry{"vkGetBufferMemoryRequirements",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetBufferMemoryRequirements)},
    Entry{"vkGetImageMemoryRequirements",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetImageMemoryRequirements)},
    Entry{"vkCreateBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateBuffer)},
    Entry{"vkDestroyBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyBuffer)},
    Entry{"vkCreateImage", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateImage)},
    Entry{"vkDestroyImage", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyImage)},
    Entry{"vkCreateImageView", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateImageView)},
    Entry{"vkDestroyImageView", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyImageView)},
    Entry{"vkCreateSampler", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateSampler)},
    Entry{"vkDestroySampler", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroySampler)},
    Entry{"vkCreateSemaphore", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateSemaphore)},
    Entry{"vkDestroySemaphore", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroySemaphore)},
    Entry{"vkCreateFence", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateFence)},
    Entry{"vkDestroyFence", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyFence)},
    Entry{"vkResetFences", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkResetFences)},
    Entry{"vkWaitForFences", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkWaitForFences)},
    Entry{"vkGetFenceStatus", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetFenceStatus)},
    Entry{"vkCreateCommandPool", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateCommandPool)},
    Entry{"vkDestroyCommandPool", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyCommandPool)},
    Entry{"vkResetCommandPool", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkResetCommandPool)},
    Entry{"vkBeginCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkBeginCommandBuffer)},
    Entry{"vkEndCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkEndCommandBuffer)},
    Entry{"vkResetCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkResetCommandBuffer)},
    Entry{"vkDestroyShaderModule",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyShaderModule)},
    Entry{"vkCreateShaderModule", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateShaderModule)},
    Entry{"vkCreatePipelineLayout",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreatePipelineLayout)},
    Entry{"vkDestroyPipelineLayout",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyPipelineLayout)},
    Entry{"vkDestroyPipeline", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyPipeline)},
    Entry{"vkCreatePipelineCache",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreatePipelineCache)},
    Entry{"vkDestroyPipelineCache",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyPipelineCache)},
    Entry{"vkCreateRenderPass", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateRenderPass)},
    Entry{"vkDestroyRenderPass", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyRenderPass)},
    Entry{"vkCreateFramebuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateFramebuffer)},
    Entry{"vkDestroyFramebuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyFramebuffer)},
    Entry{"vkCreateDescriptorSetLayout",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateDescriptorSetLayout)},
    Entry{"vkDestroyDescriptorSetLayout",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyDescriptorSetLayout)},
    Entry{"vkCreateDescriptorPool",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateDescriptorPool)},
    Entry{"vkDestroyDescriptorPool",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyDescriptorPool)},
    Entry{"vkFreeCommandBuffers", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkFreeCommandBuffers)},
    Entry{"vkQueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkQueueSubmit)},
    Entry{"vkCmdBindPipeline", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBindPipeline)},
    Entry{"vkCmdSetViewport", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetViewport)},
    Entry{"vkCmdSetScissor", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetScissor)},
    Entry{"vkCmdBindVertexBuffers",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBindVertexBuffers)},
    Entry{"vkCmdBindIndexBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBindIndexBuffer)},
    Entry{"vkCmdBindDescriptorSets",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBindDescriptorSets)},
    Entry{"vkCmdDraw", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDraw)},
    Entry{"vkCmdDrawIndexed", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDrawIndexed)},
    Entry{"vkCmdPipelineBarrier", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdPipelineBarrier)},
    Entry{"vkCmdCopyBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyBuffer)},
    Entry{"vkCmdBeginRenderPass", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBeginRenderPass)},
    Entry{"vkCmdEndRenderPass", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdEndRenderPass)},
    Entry{"vkCmdNextSubpass", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdNextSubpass)},
    Entry{"vkCmdPushConstants", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdPushConstants)},
    Entry{"vkGetPhysicalDeviceSurfaceSupportKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceSurfaceSupportKHR)},
    Entry{"vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)},
    Entry{"vkGetPhysicalDeviceSurfaceFormatsKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceSurfaceFormatsKHR)},
    Entry{"vkGetPhysicalDeviceSurfacePresentModesKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceSurfacePresentModesKHR)},
    Entry{"vkCreateSwapchainKHR", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateSwapchainKHR)},
    Entry{"vkDestroySwapchainKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroySwapchainKHR)},
    Entry{"vkGetSwapchainImagesKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetSwapchainImagesKHR)},
    Entry{"vkAcquireNextImageKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkAcquireNextImageKHR)},
    Entry{"vkQueuePresentKHR", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkQueuePresentKHR)},
    Entry{"vkCreateGraphicsPipelines",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateGraphicsPipelines)},
    Entry{"vkUpdateDescriptorSets",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkUpdateDescriptorSets)},
    Entry{"vkAllocateCommandBuffers",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkAllocateCommandBuffers)},
    Entry{"vkAllocateDescriptorSets",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkAllocateDescriptorSets)},
    Entry{"vkFreeDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkFreeDescriptorSets)},
    Entry{"vkCreateComputePipelines",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateComputePipelines)},
    Entry{"vkResetDescriptorPool",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkResetDescriptorPool)},
    Entry{"vkCreateEvent", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateEvent)},
    Entry{"vkDestroyEvent", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyEvent)},
    Entry{"vkGetEventStatus", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetEventStatus)},
    Entry{"vkSetEvent", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkSetEvent)},
    Entry{"vkResetEvent", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkResetEvent)},
    Entry{"vkCmdSetEvent", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetEvent)},
    Entry{"vkCmdResetEvent", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdResetEvent)},
    Entry{"vkCmdWaitEvents", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdWaitEvents)},
    Entry{"vkCmdCopyImage", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyImage)},
    Entry{"vkCmdCopyBufferToImage",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyBufferToImage)},
    Entry{"vkCmdCopyImageToBuffer",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyImageToBuffer)},
    Entry{"vkCmdBlitImage", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBlitImage)},
    Entry{"vkCmdClearColorImage", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdClearColorImage)},
    Entry{"vkCmdClearDepthStencilImage",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdClearDepthStencilImage)},
    Entry{"vkCmdClearAttachments",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdClearAttachments)},
    Entry{"vkCmdFillBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdFillBuffer)},
    Entry{"vkCmdUpdateBuffer", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdUpdateBuffer)},
    Entry{"vkCmdSetLineWidth", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetLineWidth)},
    Entry{"vkCmdSetDepthBias", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetDepthBias)},
    Entry{"vkCmdSetBlendConstants",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetBlendConstants)},
    Entry{"vkCmdSetDepthBounds", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetDepthBounds)},
    Entry{"vkCmdSetStencilCompareMask",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetStencilCompareMask)},
    Entry{"vkCmdSetStencilWriteMask",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetStencilWriteMask)},
    Entry{"vkCmdSetStencilReference",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetStencilReference)},
    Entry{"vkCmdDispatch", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDispatch)},
    Entry{"vkCmdDispatchIndirect",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDispatchIndirect)},
    Entry{"vkCmdDrawIndirect", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDrawIndirect)},
    Entry{"vkCmdDrawIndexedIndirect",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDrawIndexedIndirect)},
    Entry{"vkCmdExecuteCommands", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdExecuteCommands)},
    Entry{"vkAcquireNextImage2KHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkAcquireNextImage2KHR)},
    Entry{"vkBindBufferMemory2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkBindBufferMemory2)},
    Entry{"vkBindImageMemory2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkBindImageMemory2)},
    Entry{"vkCmdBeginQuery", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBeginQuery)},
    Entry{"vkCmdBeginRenderPass2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBeginRenderPass2)},
    Entry{"vkCmdBeginRendering", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBeginRendering)},
    Entry{"vkCmdBindVertexBuffers2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBindVertexBuffers2)},
    Entry{"vkCmdBlitImage2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdBlitImage2)},
    Entry{"vkCmdCopyBuffer2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyBuffer2)},
    Entry{"vkCmdCopyBufferToImage2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyBufferToImage2)},
    Entry{"vkCmdCopyImage2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyImage2)},
    Entry{"vkCmdCopyImageToBuffer2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyImageToBuffer2)},
    Entry{"vkCmdCopyQueryPoolResults",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdCopyQueryPoolResults)},
    Entry{"vkCmdDispatchBase", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDispatchBase)},
    Entry{"vkCmdDrawIndexedIndirectCount",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDrawIndexedIndirectCount)},
    Entry{"vkCmdDrawIndirectCount",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdDrawIndirectCount)},
    Entry{"vkCmdEndQuery", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdEndQuery)},
    Entry{"vkCmdEndRenderPass2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdEndRenderPass2)},
    Entry{"vkCmdEndRendering", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdEndRendering)},
    Entry{"vkCmdNextSubpass2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdNextSubpass2)},
    Entry{"vkCmdPipelineBarrier2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdPipelineBarrier2)},
    Entry{"vkCmdResetEvent2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdResetEvent2)},
    Entry{"vkCmdResetQueryPool", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdResetQueryPool)},
    Entry{"vkCmdResolveImage", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdResolveImage)},
    Entry{"vkCmdResolveImage2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdResolveImage2)},
    Entry{"vkCmdSetCullMode", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetCullMode)},
    Entry{"vkCmdSetDepthBiasEnable",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetDepthBiasEnable)},
    Entry{"vkCmdSetDepthBoundsTestEnable",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetDepthBoundsTestEnable)},
    Entry{"vkCmdSetDepthCompareOp",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetDepthCompareOp)},
    Entry{"vkCmdSetDepthTestEnable",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetDepthTestEnable)},
    Entry{"vkCmdSetDepthWriteEnable",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetDepthWriteEnable)},
    Entry{"vkCmdSetDeviceMask", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetDeviceMask)},
    Entry{"vkCmdSetEvent2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetEvent2)},
    Entry{"vkCmdSetFrontFace", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetFrontFace)},
    Entry{"vkCmdSetPrimitiveRestartEnable",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetPrimitiveRestartEnable)},
    Entry{"vkCmdSetPrimitiveTopology",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetPrimitiveTopology)},
    Entry{"vkCmdSetRasterizerDiscardEnable",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetRasterizerDiscardEnable)},
    Entry{"vkCmdSetScissorWithCount",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetScissorWithCount)},
    Entry{"vkCmdSetStencilOp", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetStencilOp)},
    Entry{"vkCmdSetStencilTestEnable",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetStencilTestEnable)},
    Entry{"vkCmdSetViewportWithCount",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdSetViewportWithCount)},
    Entry{"vkCmdWaitEvents2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdWaitEvents2)},
    Entry{"vkCmdWriteTimestamp", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdWriteTimestamp)},
    Entry{"vkCmdWriteTimestamp2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCmdWriteTimestamp2)},
    Entry{"vkCreateBufferView", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateBufferView)},
    Entry{"vkCreateDescriptorUpdateTemplate",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateDescriptorUpdateTemplate)},
    Entry{"vkCreatePrivateDataSlot",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreatePrivateDataSlot)},
    Entry{"vkCreateQueryPool", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateQueryPool)},
    Entry{"vkCreateRenderPass2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateRenderPass2)},
    Entry{"vkCreateSamplerYcbcrConversion",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkCreateSamplerYcbcrConversion)},
    Entry{"vkDestroyBufferView", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyBufferView)},
    Entry{"vkDestroyDescriptorUpdateTemplate",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyDescriptorUpdateTemplate)},
    Entry{"vkDestroyPrivateDataSlot",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyPrivateDataSlot)},
    Entry{"vkDestroyQueryPool", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroyQueryPool)},
    Entry{"vkDestroySamplerYcbcrConversion",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkDestroySamplerYcbcrConversion)},
    Entry{"vkEnumeratePhysicalDeviceGroups",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkEnumeratePhysicalDeviceGroups)},
    Entry{"vkFlushMappedMemoryRanges",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkFlushMappedMemoryRanges)},
    Entry{"vkGetBufferDeviceAddress",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetBufferDeviceAddress)},
    Entry{"vkGetBufferMemoryRequirements2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetBufferMemoryRequirements2)},
    Entry{"vkGetBufferOpaqueCaptureAddress",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetBufferOpaqueCaptureAddress)},
    Entry{"vkGetDescriptorSetLayoutSupport",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDescriptorSetLayoutSupport)},
    Entry{"vkGetDeviceBufferMemoryRequirements",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceBufferMemoryRequirements)},
    Entry{"vkGetDeviceGroupPeerMemoryFeatures",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceGroupPeerMemoryFeatures)},
    Entry{"vkGetDeviceGroupPresentCapabilitiesKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceGroupPresentCapabilitiesKHR)},
    Entry{"vkGetDeviceGroupSurfacePresentModesKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceGroupSurfacePresentModesKHR)},
    Entry{"vkGetDeviceImageMemoryRequirements",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceImageMemoryRequirements)},
    Entry{"vkGetDeviceImageSparseMemoryRequirements",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceImageSparseMemoryRequirements)},
    Entry{"vkGetDeviceMemoryCommitment",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceMemoryCommitment)},
    Entry{"vkGetDeviceMemoryOpaqueCaptureAddress",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceMemoryOpaqueCaptureAddress)},
    Entry{"vkGetDeviceQueue2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetDeviceQueue2)},
    Entry{"vkGetImageMemoryRequirements2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetImageMemoryRequirements2)},
    Entry{"vkGetImageSparseMemoryRequirements",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetImageSparseMemoryRequirements)},
    Entry{"vkGetImageSparseMemoryRequirements2",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetImageSparseMemoryRequirements2)},
    Entry{"vkGetImageSubresourceLayout",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetImageSubresourceLayout)},
    Entry{"vkGetPhysicalDeviceExternalBufferProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceExternalBufferProperties)},
    Entry{"vkGetPhysicalDeviceExternalFenceProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceExternalFenceProperties)},
    Entry{
        "vkGetPhysicalDeviceExternalSemaphoreProperties",
        reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceExternalSemaphoreProperties)},
    Entry{"vkGetPhysicalDevicePresentRectanglesKHR",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDevicePresentRectanglesKHR)},
    Entry{"vkGetPhysicalDeviceToolProperties",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPhysicalDeviceToolProperties)},
    Entry{"vkGetPrivateData", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetPrivateData)},
    Entry{"vkGetRenderAreaGranularity",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetRenderAreaGranularity)},
    Entry{"vkGetSemaphoreCounterValue",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkGetSemaphoreCounterValue)},
    Entry{"vkInvalidateMappedMemoryRanges",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkInvalidateMappedMemoryRanges)},
    Entry{"vkMergePipelineCaches",
          reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkMergePipelineCaches)},
    Entry{"vkQueueBindSparse", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkQueueBindSparse)},
    Entry{"vkQueueSubmit2", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkQueueSubmit2)},
    Entry{"vkResetQueryPool", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkResetQueryPool)},
    Entry{"vkSetPrivateData", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkSetPrivateData)},
    Entry{"vkSignalSemaphore", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkSignalSemaphore)},
    Entry{"vkTrimCommandPool", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkTrimCommandPool)},
    Entry{"vkUnmapMemory", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkUnmapMemory)},
    Entry{"vkWaitSemaphores", reinterpret_cast<PFN_vkVoidFunction>(&Wrap_vkWaitSemaphores)},
}};
}  // namespace

std::span<const Entry> wrapper_entries() noexcept {
    return {kWrapperTable.data(), kWrapperTable.size()};
}

}  // namespace lsw::icd::generated
