#pragma once

#include "support/test_window.hpp"
#include "support/vulkan_rig.hpp"

namespace lsw::suites {
class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    ~VulkanSwapchain() { destroy(); }

    [[nodiscard]] VkResult open(VulkanRig& rig, HWND window);

    [[nodiscard]] VkResult create(VkFormat format, VkColorSpaceKHR color_space, VkPresentModeKHR mode, std::uint32_t images, VkExtent2D extent);

    [[nodiscard]] VkResult acquire(std::uint64_t timeout, std::uint32_t& index);

    [[nodiscard]] VkResult paint_and_present(std::uint32_t index, const float rgba[4]);
    [[nodiscard]] VkResult pixels_and_present(std::uint32_t index, const std::vector<std::uint32_t>& xrgb);

    void destroy_swapchain();

    void destroy_surface();

    void destroy();

    [[nodiscard]] VkSurfaceCapabilitiesKHR capabilities() const;

    [[nodiscard]] std::vector<VkSurfaceFormatKHR> formats() const;

    [[nodiscard]] std::vector<VkPresentModeKHR> modes() const;

    [[nodiscard]] bool presentable() const { return queue_ != nullptr; }

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::uint32_t requested = 0;
    std::uint32_t min_images = 0;

private:
    VulkanRig* rig_ = nullptr;
    VulkanQueue* queue_ = nullptr;
    VulkanOwned<VkFence> fence_;
};
}  // namespace lsw::suites
