#pragma once

#include "lsw/base/log.hpp"
#include "lsw/render/surface_manager.hpp"
#include "vk_real_dispatch.h"  // generated

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace lsw::render::dispatch {

// RealVulkanDispatch + surface-handle translation.
//
// The codegen-emitted RealVulkanDispatch forwards every command directly
// to the host Vulkan loader. That works for the vast majority of
// commands because non-dispatchable handles are pass-through (server
// minted them in the first place). Surfaces are the exception: they're
// minted by the SurfaceManager, the client holds a synthetic id, and
// every surface query / consumer must translate the synthetic id back
// to the real ``VkSurfaceKHR`` before calling the real driver.
//
// Override is via subclass-and-override-a-non-final-method; the codegen
// emits virtuals without ``final``, so this drops in.
class TranslatingDispatch : public ::lsw::vkproto::RealVulkanDispatch {
public:
    explicit TranslatingDispatch(SurfaceManager& mgr) noexcept : mgr_(mgr) {}

    // ---- Surface queries ----
    VkResult on_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice phys, uint32_t qfi,
                                                     VkSurfaceKHR surface,
                                                     VkBool32* pSupported) override {
        return ::lsw::vkproto::RealVulkanDispatch::on_vkGetPhysicalDeviceSurfaceSupportKHR(
            phys, qfi, resolve(surface), pSupported);
    }

    VkResult
    on_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice phys, VkSurfaceKHR surface,
                                                 VkSurfaceCapabilitiesKHR* pCaps) override {
        return ::lsw::vkproto::RealVulkanDispatch::on_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            phys, resolve(surface), pCaps);
    }

    VkResult on_vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice phys, VkSurfaceKHR surface,
                                                     uint32_t* pCount,
                                                     VkSurfaceFormatKHR* pFormats) override {
        return ::lsw::vkproto::RealVulkanDispatch::on_vkGetPhysicalDeviceSurfaceFormatsKHR(
            phys, resolve(surface), pCount, pFormats);
    }

    VkResult on_vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice phys,
                                                          VkSurfaceKHR surface, uint32_t* pCount,
                                                          VkPresentModeKHR* pModes) override {
        return ::lsw::vkproto::RealVulkanDispatch::on_vkGetPhysicalDeviceSurfacePresentModesKHR(
            phys, resolve(surface), pCount, pModes);
    }

    // ---- Swapchain ----
    // VkSwapchainCreateInfoKHR carries the surface in its ``surface``
    // member. We translate it before forwarding to the driver.
    VkResult on_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkSwapchainKHR* pSwapchain) override {
        VkSwapchainCreateInfoKHR translated = *pCreateInfo;
        translated.surface = resolve(pCreateInfo->surface);
        return ::lsw::vkproto::RealVulkanDispatch::on_vkCreateSwapchainKHR(device, &translated,
                                                                           pAllocator, pSwapchain);
    }

    // vkDestroySurfaceKHR is excluded from codegen — the client handles
    // it via the kCmdDestroySurface frame, which routes to
    // SurfaceManager::destroy. No on_vkDestroySurfaceKHR virtual exists.

    // ---- Instance version ----
    // The application's requested apiVersion is replayed here, and for
    // an app written against Vulkan 1.0 that is 1.0 — which makes
    // ``vkGetPhysicalDeviceProperties2`` unavailable, and with it any
    // way to ask what an imported host pointer must be aligned to. The
    // query silently leaves the chained struct untouched rather than
    // failing, so the symptom is "this driver has no
    // VK_EXT_external_memory_host" on a driver that plainly does.
    //
    // Raising it is safe in the direction that matters: a higher
    // instance version only permits more, and every 1.0 entry point
    // keeps working. Capped at what the loader actually offers.
    VkResult on_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkInstance* pInstance) override {
        std::uint32_t loader_version = VK_API_VERSION_1_0;
        if (::vkEnumerateInstanceVersion(&loader_version) != VK_SUCCESS) {
            loader_version = VK_API_VERSION_1_0;
        }

        VkInstanceCreateInfo augmented = *pCreateInfo;
        VkApplicationInfo app{};
        if (pCreateInfo->pApplicationInfo != nullptr) {
            app = *pCreateInfo->pApplicationInfo;
        } else {
            app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        }
        const std::uint32_t wanted = std::min(loader_version, VK_API_VERSION_1_1);
        if (app.apiVersion < wanted) {
            app.apiVersion = wanted;
            augmented.pApplicationInfo = &app;
        }

        // The client's extension list is the CLIENT'S PLATFORM's.
        //
        // A guest application asks for VK_KHR_win32_surface, because
        // that is what a Windows application asks for. Replaying that
        // name to a Linux driver fails the whole instance with
        // VK_ERROR_EXTENSION_NOT_PRESENT — which is how a guest vkcube
        // came out as "cannot find a compatible ICD", a message about
        // something entirely different.
        //
        // Dropping it is not a loss: the ICD never sends a Win32 surface
        // over the wire. It intercepts vkCreateWin32SurfaceKHR locally
        // and turns it into kCmdCreateHostSurface, because the window
        // the pixels land in belongs to the HOST. So what the host
        // instance actually needs is the host's own surface extensions,
        // which is why they are added rather than merely kept.
        std::vector<const char*> extensions;
        const auto host_supported = host_instance_extensions();
        for (std::uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            const char* name = pCreateInfo->ppEnabledExtensionNames[i];
            if (host_supported.count(name) != 0) {
                extensions.push_back(name);
            }
            // Anything unsupported here is a client-platform WSI name
            // the ICD already handles itself. Silently dropping a
            // non-WSI extension would be worse than failing, so say so.
            else if (std::strstr(name, "surface") == nullptr) {
                LSW_WARN("dropping instance extension the host does not have: {}", name);
            }
        }
        for (const char* needed : {VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_xcb_surface"}) {
            if (host_supported.count(needed) != 0
                && std::none_of(extensions.begin(), extensions.end(), [&](const char* e) {
                       return std::strcmp(e, needed) == 0;
                   })) {
                extensions.push_back(needed);
            }
        }
        augmented.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        augmented.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

        return ::lsw::vkproto::RealVulkanDispatch::on_vkCreateInstance(&augmented, pAllocator,
                                                                       pInstance);
    }

    // ---- Device parentage ----
    // Vulkan gives no way to ask a VkDevice which VkPhysicalDevice it
    // came from, and the memory-import path needs exactly that: the
    // alignment an imported host pointer must satisfy is a physical
    // device property, and it has to be read from the real device
    // rather than assumed. Recording it here is the only moment the
    // pairing is visible.
    VkResult on_vkCreateDevice(VkPhysicalDevice physicalDevice,
                               const VkDeviceCreateInfo* pCreateInfo,
                               const VkAllocationCallbacks* pAllocator,
                               VkDevice* pDevice) override {
        // The device extension list is the CLIENT's, replayed. It cannot
        // contain VK_EXT_external_memory_host, because the application
        // has no idea it is being proxied and would never ask for it —
        // and without it enabled here, importing the client's shared
        // memory is not merely unsupported, it is invalid usage.
        //
        // So the server adds it when the physical device offers it. This
        // is a proxy doing the one thing a proxy is for: asking for a
        // capability the client cannot know it needs. Nothing observable
        // by the application changes.
        VkDeviceCreateInfo augmented = *pCreateInfo;
        std::vector<const char*> extensions(
            pCreateInfo->ppEnabledExtensionNames,
            pCreateInfo->ppEnabledExtensionNames + pCreateInfo->enabledExtensionCount);
        if (supports_external_memory_host(physicalDevice)
            && std::none_of(extensions.begin(), extensions.end(), [](const char* e) {
                   return std::strcmp(e, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME) == 0;
               })) {
            extensions.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
            augmented.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
            augmented.ppEnabledExtensionNames = extensions.data();
        }

        const VkResult rc = ::lsw::vkproto::RealVulkanDispatch::on_vkCreateDevice(
            physicalDevice, &augmented, pAllocator, pDevice);
        if (rc == VK_SUCCESS && pDevice != nullptr && *pDevice != VK_NULL_HANDLE) {
            const std::lock_guard lk{parents_mu_};
            device_parents_[*pDevice] = physicalDevice;
        }
        return rc;
    }

    // VK_NULL_HANDLE for a device this server did not create.
    [[nodiscard]] VkPhysicalDevice physical_device_for(VkDevice device) const {
        const std::lock_guard lk{parents_mu_};
        const auto it = device_parents_.find(device);
        return it == device_parents_.end() ? VK_NULL_HANDLE : it->second;
    }

private:
    // What the host's Vulkan instance can actually offer, asked once.
    [[nodiscard]] static const std::set<std::string>& host_instance_extensions() {
        static const std::set<std::string> cached = [] {
            std::set<std::string> names;
            std::uint32_t count = 0;
            if (::vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
                return names;
            }
            std::vector<VkExtensionProperties> props(count);
            if (::vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data())
                != VK_SUCCESS) {
                return names;
            }
            for (const auto& e : props) {
                names.insert(e.extensionName);
            }
            return names;
        }();
        return cached;
    }

    // Asked of the driver, never assumed: whether an import is possible
    // at all is a property of whatever GPU this server happens to be
    // running on.
    [[nodiscard]] static bool supports_external_memory_host(VkPhysicalDevice phys) {
        std::uint32_t count = 0;
        if (::vkEnumerateDeviceExtensionProperties(phys, nullptr, &count, nullptr) != VK_SUCCESS) {
            return false;
        }
        std::vector<VkExtensionProperties> props(count);
        if (::vkEnumerateDeviceExtensionProperties(phys, nullptr, &count, props.data())
            != VK_SUCCESS) {
            return false;
        }
        return std::any_of(props.begin(), props.end(), [](const VkExtensionProperties& e) {
            return std::strcmp(e.extensionName, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME) == 0;
        });
    }

    [[nodiscard]] VkSurfaceKHR resolve(VkSurfaceKHR synthetic) const noexcept {
        std::uint64_t raw = 0;
        std::memcpy(&raw, &synthetic, sizeof(raw));
        return mgr_.resolve(raw);
    }

    SurfaceManager& mgr_;

    // Process-wide, like this dispatch itself: VkDevice handles are
    // unique within the driver, so one map serves every connection.
    mutable std::mutex parents_mu_;
    std::unordered_map<VkDevice, VkPhysicalDevice> device_parents_;
};

}  // namespace lsw::render::dispatch
