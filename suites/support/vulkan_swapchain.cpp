#include "support/vulkan_swapchain.hpp"

#include "support/vulkan_image.hpp"

namespace lsw::suites {

VkResult VulkanSwapchain::open(VulkanRig& rig, HWND window) {
    rig_ = &rig;
    if (rig.fn.vkCreateWin32SurfaceKHR == nullptr || rig.fn.vkCreateSwapchainKHR == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    vkw::SurfaceCreateInfo info{};
    info.sType = vkw::kWin32SurfaceCreateInfo;
    info.hinstance = ::GetModuleHandleW(nullptr);
    info.hwnd = window;
    VkResult result = rig.fn.vkCreateWin32SurfaceKHR(rig.instance, &info, nullptr, &surface);
    if (result != VK_SUCCESS) {
        surface = VK_NULL_HANDLE;
        return result;
    }
    for (VulkanQueue& queue : rig.queues) {
        VkBool32 supported = VK_FALSE;
        if (rig.fn.vkGetPhysicalDeviceSurfaceSupportKHR(rig.physical, queue.family, surface, &supported) == VK_SUCCESS && supported != VK_FALSE &&
            (queue.properties.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0) {
            queue_ = &queue;
            break;
        }
    }
    result = rig.make_fence(fence_);
    return result;
}

VkSurfaceCapabilitiesKHR VulkanSwapchain::capabilities() const {
    VkSurfaceCapabilitiesKHR caps{};
    rig_->fn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rig_->physical, surface, &caps);
    return caps;
}

std::vector<VkSurfaceFormatKHR> VulkanSwapchain::formats() const {
    std::uint32_t count = 0;
    rig_->fn.vkGetPhysicalDeviceSurfaceFormatsKHR(rig_->physical, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> out(count);
    rig_->fn.vkGetPhysicalDeviceSurfaceFormatsKHR(rig_->physical, surface, &count, out.data());
    out.resize(count);
    return out;
}

std::vector<VkPresentModeKHR> VulkanSwapchain::modes() const {
    std::uint32_t count = 0;
    rig_->fn.vkGetPhysicalDeviceSurfacePresentModesKHR(rig_->physical, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> out(count);
    rig_->fn.vkGetPhysicalDeviceSurfacePresentModesKHR(rig_->physical, surface, &count, out.data());
    out.resize(count);
    return out;
}

VkResult VulkanSwapchain::create(VkFormat chosen, VkColorSpaceKHR color_space, VkPresentModeKHR mode, std::uint32_t count, VkExtent2D size) {
    const VkSurfaceCapabilitiesKHR caps = capabilities();
    min_images = caps.minImageCount;
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = count;
    info.imageFormat = chosen;
    info.imageColorSpace = color_space;
    info.imageExtent = size;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0 ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                                                                    : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    rig_->wait_idle();
    const VkResult result = rig_->fn.vkCreateSwapchainKHR(rig_->device, &info, nullptr, &created);
    if (info.oldSwapchain != VK_NULL_HANDLE) {
        rig_->wait_idle();
        rig_->fn.vkDestroySwapchainKHR(rig_->device, info.oldSwapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        images.clear();
    }
    if (result != VK_SUCCESS) {
        return result;
    }
    swapchain = created;
    format = chosen;
    extent = size;
    requested = count;
    std::uint32_t got = 0;
    rig_->fn.vkGetSwapchainImagesKHR(rig_->device, swapchain, &got, nullptr);
    images.resize(got);
    rig_->fn.vkGetSwapchainImagesKHR(rig_->device, swapchain, &got, images.data());
    images.resize(got);
    return VK_SUCCESS;
}

VkResult VulkanSwapchain::acquire(std::uint64_t timeout, std::uint32_t& index) {
    VkFence handle = fence_.get();
    (void)rig_->fn.vkResetFences(rig_->device, 1, &handle);
    rig_->mark_pending();
    const VkResult result = rig_->fn.vkAcquireNextImageKHR(rig_->device, swapchain, timeout, VK_NULL_HANDLE, handle, &index);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        return rig_->wait_fence(handle, 20ull * 1000000000ull) == VK_SUCCESS ? result : VK_TIMEOUT;
    }
    return result;
}

VkResult VulkanSwapchain::paint_and_present(std::uint32_t index, const float rgba[4]) {
    if (queue_ == nullptr || index >= images.size()) {
        return VK_ERROR_UNKNOWN;
    }
    const VkImage image = images[index];
    VkClearColorValue value{};
    for (int i = 0; i < 4; ++i) {
        value.float32[i] = rgba[i];
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkResult result = rig_->run(
        [&](VkCommandBuffer commands) {
            VkImageMemoryBarrier to_clear = VulkanImage::barrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                                                 VK_IMAGE_ASPECT_COLOR_BIT);
            rig_->fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_clear);
            rig_->fn.vkCmdClearColorImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
            VkImageMemoryBarrier to_present = VulkanImage::barrier(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                                                                   VK_IMAGE_ASPECT_COLOR_BIT);
            rig_->fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_present);
        },
        queue_);
    if (result != VK_SUCCESS) {
        return result;
    }
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &index;
    rig_->mark_pending();
    return rig_->fn.vkQueuePresentKHR(queue_->handle, &present);
}

void VulkanSwapchain::destroy_swapchain() {
    if (rig_ != nullptr && swapchain != VK_NULL_HANDLE) {
        rig_->wait_idle();
        rig_->fn.vkDestroySwapchainKHR(rig_->device, swapchain, nullptr);
    }
    swapchain = VK_NULL_HANDLE;
    images.clear();
}

void VulkanSwapchain::destroy_surface() {
    if (rig_ != nullptr && surface != VK_NULL_HANDLE) {
        rig_->wait_idle();
        rig_->fn.vkDestroySurfaceKHR(rig_->instance, surface, nullptr);
    }
    surface = VK_NULL_HANDLE;
}

void VulkanSwapchain::destroy() {
    if (rig_ == nullptr) {
        return;
    }
    fence_.reset();
    destroy_swapchain();
    destroy_surface();
}

}  // namespace lsw::suites
