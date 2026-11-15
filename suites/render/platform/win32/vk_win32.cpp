#include "render/platform/vk_platform.hpp"
#include "render/vk/kit.hpp"
#include "render/vk/presenter.hpp"
#include "support/target_adapter.hpp"
#include "support/test_window.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace lsw::render::platform {

namespace {

struct Win32SurfaceInfo {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    HINSTANCE hinstance;
    HWND hwnd;
};

using CreateWin32Surface = VkResult(VKAPI_PTR*)(VkInstance, const Win32SurfaceInfo*, const VkAllocationCallbacks*, VkSurfaceKHR*);

constexpr VkStructureType kWin32SurfaceType = static_cast<VkStructureType>(1000009000);

class WindowPresenter : public vk::Presenter {
public:
    bool open(vk::Kit& kit, std::uint32_t width, std::uint32_t height, std::string& error) override {
        lsw::suites::TestWindowConfig config;
        lsw::suites::TestWindow::fit_config(config, static_cast<int>(width), static_cast<int>(height));
        if (!window_.create(config, error)) {
            return false;
        }
        auto create_surface = reinterpret_cast<CreateWin32Surface>(kit.api.vkGetInstanceProcAddr(kit.instance, "vkCreateWin32SurfaceKHR"));
        if (create_surface == nullptr || kit.api.vkCreateSwapchainKHR == nullptr) {
            error = "the Vulkan device does not expose the Win32 surface or swapchain entry points";
            return false;
        }
        Win32SurfaceInfo info{kWin32SurfaceType, nullptr, 0, ::GetModuleHandleW(nullptr), window_.hwnd()};
        if (!kit.check(create_surface(kit.instance, &info, nullptr, &surface_), "vkCreateWin32SurfaceKHR", error)) {
            return false;
        }
        VkBool32 supported = VK_FALSE;
        if (!kit.check(kit.api.vkGetPhysicalDeviceSurfaceSupportKHR(kit.physical, kit.family, surface_, &supported), "surface support", error)) {
            return false;
        }
        if (supported == VK_FALSE) {
            error = "the graphics queue family cannot present to the window surface";
            return false;
        }
        VkSurfaceCapabilitiesKHR caps{};
        if (!kit.check(kit.api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(kit.physical, surface_, &caps), "surface capabilities", error)) {
            return false;
        }
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if ((caps.supportedUsageFlags & usage) != usage) {
            error = "the swapchain cannot be used as a transfer source";
            return false;
        }
        std::uint32_t count = 0;
        kit.api.vkGetPhysicalDeviceSurfaceFormatsKHR(kit.physical, surface_, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        kit.api.vkGetPhysicalDeviceSurfaceFormatsKHR(kit.physical, surface_, &count, formats.data());
        VkSurfaceFormatKHR chosen{};
        bool found = false;
        for (const VkSurfaceFormatKHR& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
                chosen = f;
                found = true;
            }
        }
        if (!found) {
            error = "the surface offers no B8G8R8A8_UNORM format";
            return false;
        }
        format_ = chosen.format;
        VkExtent2D extent = caps.currentExtent;
        if (extent.width == 0xffffffffu) {
            extent = {width, height};
        }
        if (extent.width != width || extent.height != height) {
            error = "the window surface is " + std::to_string(extent.width) + "x" + std::to_string(extent.height) + ", not the requested size";
            return false;
        }
        VkSwapchainCreateInfoKHR sc{};
        sc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sc.surface = surface_;
        sc.minImageCount = std::max(2u, caps.minImageCount);
        sc.imageFormat = chosen.format;
        sc.imageColorSpace = chosen.colorSpace;
        sc.imageExtent = extent;
        sc.imageArrayLayers = 1;
        sc.imageUsage = usage;
        sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sc.preTransform = caps.currentTransform;
        sc.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0 ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                                                                       : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sc.clipped = VK_TRUE;
        if (!kit.check(kit.api.vkCreateSwapchainKHR(kit.device, &sc, nullptr, &swapchain_), "vkCreateSwapchainKHR", error)) {
            return false;
        }
        count = 0;
        kit.api.vkGetSwapchainImagesKHR(kit.device, swapchain_, &count, nullptr);
        images_.resize(count);
        kit.api.vkGetSwapchainImagesKHR(kit.device, swapchain_, &count, images_.data());
        return true;
    }

    VkFormat format() const override { return format_; }
    std::uint32_t image_count() const override { return static_cast<std::uint32_t>(images_.size()); }
    VkImage image(std::uint32_t index) const override { return images_[index]; }
    VkImageLayout present_layout() const override { return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; }

    bool acquire(vk::Kit& kit, std::uint32_t& index, VkSemaphore signal, std::string& error) override {
        const VkResult result = kit.api.vkAcquireNextImageKHR(kit.device, swapchain_, 10000000000ull, signal, VK_NULL_HANDLE, &index);
        if (result == VK_SUBOPTIMAL_KHR) {
            return true;
        }
        return kit.check(result, "vkAcquireNextImageKHR", error);
    }

    bool present(vk::Kit& kit, std::uint32_t index, VkSemaphore wait, std::string& error) override {
        VkPresentInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &wait;
        info.swapchainCount = 1;
        info.pSwapchains = &swapchain_;
        info.pImageIndices = &index;
        const VkResult result = kit.api.vkQueuePresentKHR(kit.queue, &info);
        if (result == VK_SUBOPTIMAL_KHR) {
            return true;
        }
        return kit.check(result, "vkQueuePresentKHR", error);
    }

    void close(vk::Kit& kit) override {
        if (swapchain_ != VK_NULL_HANDLE) {
            kit.api.vkDestroySwapchainKHR(kit.device, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        if (surface_ != VK_NULL_HANDLE) {
            kit.api.vkDestroySurfaceKHR(kit.instance, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        window_.destroy();
    }

private:
    lsw::suites::TestWindow window_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> images_;
};

class Win32VkPlatform : public VkPlatform {
public:
    ~Win32VkPlatform() override {
        if (library_ != nullptr) {
            ::FreeLibrary(library_);
        }
    }

    bool select(std::string& error) {
        return lsw::suites::TargetAdapter::pick(adapter_, error);
    }

    PFN_vkGetInstanceProcAddr loader(std::string& error) override {
        library_ = ::LoadLibraryA("vulkan-1.dll");
        if (library_ == nullptr) {
            error = "vulkan-1.dll cannot be loaded";
            return nullptr;
        }
        auto symbol = reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void*>(::GetProcAddress(library_, "vkGetInstanceProcAddr")));
        if (symbol == nullptr) {
            error = "vulkan-1.dll exports no vkGetInstanceProcAddr";
        }
        return symbol;
    }

    std::vector<std::string> instance_extensions(bool present) override {
        if (!present) {
            return {};
        }
        return {"VK_KHR_surface", "VK_KHR_win32_surface"};
    }

    std::vector<std::string> device_extensions(bool present) override {
        if (!present) {
            return {};
        }
        return {"VK_KHR_swapchain"};
    }

    bool accepts(const VkDeviceIdentity& identity) override {
        if (identity.luid_valid) {
            return std::memcmp(identity.luid, &adapter_.luid, sizeof(LUID)) == 0;
        }
        return identity.index == 0;
    }

    std::unique_ptr<vk::Presenter> make_presenter(std::string&) override { return std::make_unique<WindowPresenter>(); }

private:
    lsw::suites::AvAdapter adapter_;
    HMODULE library_ = nullptr;
};

}  // namespace

std::unique_ptr<VkPlatform> make_vk_platform(const VkPlatformConfig&, std::string& error) {
    auto platform = std::make_unique<Win32VkPlatform>();
    if (!platform->select(error)) {
        return nullptr;
    }
    return platform;
}

}  // namespace lsw::render::platform
