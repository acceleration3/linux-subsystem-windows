#pragma once

#include <windows.h>

#include <cstdint>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

namespace lsw::suites::vkw {
constexpr VkStructureType kWin32SurfaceCreateInfo = static_cast<VkStructureType>(1000009000);
constexpr VkStructureType kImportMemoryWin32 = static_cast<VkStructureType>(1000073000);
constexpr VkStructureType kMemoryWin32HandleProperties = static_cast<VkStructureType>(1000073002);
constexpr VkStructureType kMemoryGetWin32Handle = static_cast<VkStructureType>(1000073003);
constexpr VkStructureType kImportSemaphoreWin32 = static_cast<VkStructureType>(1000078000);
constexpr VkStructureType kSemaphoreGetWin32Handle = static_cast<VkStructureType>(1000078003);
constexpr VkStructureType kImportFenceWin32 = static_cast<VkStructureType>(1000114000);
constexpr VkStructureType kFenceGetWin32Handle = static_cast<VkStructureType>(1000114002);

constexpr const char* kSurfaceExtension = "VK_KHR_win32_surface";
constexpr const char* kExternalMemoryWin32 = "VK_KHR_external_memory_win32";
constexpr const char* kExternalSemaphoreWin32 = "VK_KHR_external_semaphore_win32";
constexpr const char* kExternalFenceWin32 = "VK_KHR_external_fence_win32";

struct SurfaceCreateInfo {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    HINSTANCE hinstance;
    HWND hwnd;
};

struct ImportMemoryInfo {
    VkStructureType sType;
    const void* pNext;
    VkExternalMemoryHandleTypeFlagBits handleType;
    HANDLE handle;
    LPCWSTR name;
};

struct MemoryGetHandleInfo {
    VkStructureType sType;
    const void* pNext;
    VkDeviceMemory memory;
    VkExternalMemoryHandleTypeFlagBits handleType;
};

struct SemaphoreGetHandleInfo {
    VkStructureType sType;
    const void* pNext;
    VkSemaphore semaphore;
    VkExternalSemaphoreHandleTypeFlagBits handleType;
};

struct ImportSemaphoreInfo {
    VkStructureType sType;
    const void* pNext;
    VkSemaphore semaphore;
    VkSemaphoreImportFlags flags;
    VkExternalSemaphoreHandleTypeFlagBits handleType;
    HANDLE handle;
    LPCWSTR name;
};

struct FenceGetHandleInfo {
    VkStructureType sType;
    const void* pNext;
    VkFence fence;
    VkExternalFenceHandleTypeFlagBits handleType;
};

struct ImportFenceInfo {
    VkStructureType sType;
    const void* pNext;
    VkFence fence;
    VkFenceImportFlags flags;
    VkExternalFenceHandleTypeFlagBits handleType;
    HANDLE handle;
    LPCWSTR name;
};

using CreateWin32Surface = VkResult(VKAPI_PTR*)(VkInstance, const SurfaceCreateInfo*, const VkAllocationCallbacks*, VkSurfaceKHR*);
using GetPresentationSupport = VkBool32(VKAPI_PTR*)(VkPhysicalDevice, std::uint32_t);
using GetMemoryHandle = VkResult(VKAPI_PTR*)(VkDevice, const MemoryGetHandleInfo*, HANDLE*);
using GetSemaphoreHandle = VkResult(VKAPI_PTR*)(VkDevice, const SemaphoreGetHandleInfo*, HANDLE*);
using ImportSemaphoreHandle = VkResult(VKAPI_PTR*)(VkDevice, const ImportSemaphoreInfo*);
using GetFenceHandle = VkResult(VKAPI_PTR*)(VkDevice, const FenceGetHandleInfo*, HANDLE*);
using ImportFenceHandle = VkResult(VKAPI_PTR*)(VkDevice, const ImportFenceInfo*);
}  // namespace lsw::suites::vkw
