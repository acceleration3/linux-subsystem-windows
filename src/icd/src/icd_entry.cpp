// LSW Vulkan ICD: loader contract.
//
// This translation unit owns only the entry points the Vulkan loader
// needs to discover us:
//
//   * vk_icdGetInstanceProcAddr                  (extern "C")
//   * vk_icdNegotiateLoaderICDInterfaceVersion   (extern "C")
//   * vkEnumerateInstance{Version,ExtensionProperties,LayerProperties}
//   * vkGetInstanceProcAddr / vkGetDeviceProcAddr
//
// Everything else lives in a topical TU:
//   * surface.cpp    — vkCreateXcbSurfaceKHR & friends
//   * memory.cpp     — vkMapMemory + memory shadow
//   * dispatch.cpp   — name -> entry-point lookup that aggregates them

#include "lsw/icd/dispatch.hpp"
#include "lsw/icd/entry_table.hpp"

#include <array>
#include <cstdint>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan_core.h>

// ELF hides everything by default (CXX_VISIBILITY_PRESET hidden) and
// this opts the two loader entry points back in. A PE DLL has no
// visibility attribute at all — an export is either named in the export
// table or absent — so the same two functions need dllexport there.
//
// Without this, mingw's fallback is to export every symbol in the
// library, which for this DLL is well over a thousand: every generated
// wrapper, every inline that survived, the whole of spdlog. The loader
// needs exactly two.
#if defined(_WIN32)
#define LSW_ICD_EXPORT __declspec(dllexport)
#else
#define LSW_ICD_EXPORT __attribute__((visibility("default")))
#endif

namespace lsw::icd::loader {

namespace {

// Instance extensions advertised to the loader.
//
// Both platform-surface extensions are advertised on both platforms,
// for the same reason surface.cpp registers both entry points: a Linux
// application never asks for the Win32 one and a guest application
// never asks for the xcb one, and either way both end at
// ``create_host_surface`` — the client's own window is ignored, because
// the pixels land in a window the HOST owns.
//
// Advertising only xcb was a real gap rather than a tidy default: the
// ICD implemented ``vkCreateWin32SurfaceKHR`` and the loader still told
// guest applications the extension did not exist, so nothing in the
// guest could create a surface at all.
//
// wayland/xlib land when the server grows the corresponding backends.
const VkExtensionProperties kInstanceExtensions[] = {
    {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_SPEC_VERSION},
    {"VK_KHR_xcb_surface", 6},
    {"VK_KHR_win32_surface", 6},
};

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceVersion(uint32_t* pApiVersion) {
    if (pApiVersion != nullptr) {
        *pApiVersion = VK_API_VERSION_1_3;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(
    const char* /*pLayerName*/, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    constexpr uint32_t kCount = sizeof(kInstanceExtensions) / sizeof(kInstanceExtensions[0]);
    if (pProperties == nullptr) {
        if (pPropertyCount != nullptr) {
            *pPropertyCount = kCount;
        }
        return VK_SUCCESS;
    }
    const uint32_t to_write = *pPropertyCount < kCount ? *pPropertyCount : kCount;
    for (uint32_t i = 0; i < to_write; ++i) {
        pProperties[i] = kInstanceExtensions[i];
    }
    *pPropertyCount = to_write;
    return to_write < kCount ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* /*pProperties*/) {
    if (pPropertyCount != nullptr) {
        *pPropertyCount = 0;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance, const char* pName) {
    return ::lsw::icd::lookup_entry(pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice, const char* pName) {
    return ::lsw::icd::lookup_entry(pName);
}

const std::array<Entry, 5> kEntries = {{
    {"vkEnumerateInstanceVersion", reinterpret_cast<PFN_vkVoidFunction>(&EnumerateInstanceVersion)},
    {"vkEnumerateInstanceExtensionProperties",
     reinterpret_cast<PFN_vkVoidFunction>(&EnumerateInstanceExtensionProperties)},
    {"vkEnumerateInstanceLayerProperties",
     reinterpret_cast<PFN_vkVoidFunction>(&EnumerateInstanceLayerProperties)},
    {"vkGetInstanceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr)},
    {"vkGetDeviceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr)},
}};

}  // namespace

std::span<const Entry> entries() noexcept {
    return {kEntries.data(), kEntries.size()};
}

}  // namespace lsw::icd::loader

extern "C" {

LSW_ICD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance /*instance*/, const char* pName) {
    return ::lsw::icd::lookup_entry(pName);
}

LSW_ICD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    constexpr uint32_t kIcdInterfaceVersion = 5;
    if (pSupportedVersion == nullptr) {
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    if (*pSupportedVersion > kIcdInterfaceVersion) {
        *pSupportedVersion = kIcdInterfaceVersion;
    }
    return VK_SUCCESS;
}

}  // extern "C"
