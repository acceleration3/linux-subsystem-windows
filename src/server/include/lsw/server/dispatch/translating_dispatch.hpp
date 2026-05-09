#pragma once

#include "lsw/server/surface_manager.hpp"
#include "vk_real_dispatch.h"  // generated

#include <cstdint>
#include <cstring>

namespace lsw::server::dispatch {

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

private:
    [[nodiscard]] VkSurfaceKHR resolve(VkSurfaceKHR synthetic) const noexcept {
        std::uint64_t raw = 0;
        std::memcpy(&raw, &synthetic, sizeof(raw));
        return mgr_.resolve(raw);
    }

    SurfaceManager& mgr_;
};

}  // namespace lsw::server::dispatch
