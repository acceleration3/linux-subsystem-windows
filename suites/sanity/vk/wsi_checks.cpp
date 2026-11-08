#include "sanity/vk/wsi_checks.hpp"

#include "sanity/vk/kit.hpp"
#include "support/swap_rig.hpp"
#include "support/vulkan_swapchain.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace lsw::suites::sanity::vk {

namespace {

VulkanRigOptions surface_options() {
    VulkanRigOptions options;
    options.surface = true;
    return options;
}

struct Stage {
    TestWindow window;
    VulkanSwapchain chain;
    int client_width = 128;
    int client_height = 96;

    [[nodiscard]] std::string open(VulkanRig& rig) {
        if (!rig.instance_has(vkw::kSurfaceExtension) || !rig.enabled_has("VK_KHR_swapchain")) {
            return "VK_KHR_win32_surface or VK_KHR_swapchain is not advertised";
        }
        TestWindowConfig config;
        config.x = 80;
        config.y = 80;
        TestWindow::outer_size(config.x, config.y, client_width, client_height, config.width, config.height);
        std::string error;
        if (!window.create(config, error)) {
            return "window: " + error;
        }
        ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        const VkResult result = chain.open(rig, window.hwnd());
        if (result != VK_SUCCESS) {
            return "surface: " + VulkanTools::result_name(result);
        }
        return {};
    }

    [[nodiscard]] VkExtent2D extent() const {
        const VkSurfaceCapabilitiesKHR caps = chain.capabilities();
        if (caps.currentExtent.width != 0xFFFFFFFFu) {
            return caps.currentExtent;
        }
        return {static_cast<std::uint32_t>(window.width()), static_cast<std::uint32_t>(window.height())};
    }

    [[nodiscard]] bool pick(VkSurfaceFormatKHR& out) const {
        for (const VkSurfaceFormatKHR& item : chain.formats()) {
            if (item.format == VK_FORMAT_B8G8R8A8_UNORM) {
                out = item;
                return true;
            }
        }
        return false;
    }
};

float shade(int k) {
    return static_cast<float>(20 + k * 3 % 200) / 255.0f;
}

bool wait_for_shade(Stage& stage, int k, int timeout_ms) {
    POINT point{stage.client_width / 2, stage.client_height / 2};
    ::ClientToScreen(stage.window.hwnd(), &point);
    const int want = static_cast<int>(std::lround(shade(k) * 255.0f));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        int rgb[3] = {};
        if (SwapRig::screen_rgb(point.x, point.y, rgb) && std::abs(rgb[0] - want) <= 2 && std::abs(rgb[1] - want) <= 2) {
            return true;
        }
        ::Sleep(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool ok_present(VkResult result) {
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

}  // namespace

Outcome WsiChecks::surface_caps_consistent(Context& context) {
    return Kit::each_device(context, surface_options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        Stage stage;
        const std::string skipped = stage.open(rig);
        if (!skipped.empty()) {
            return skipped;
        }
        if (!stage.chain.presentable()) {
            problems.add("no queue family supports presentation to a Win32 surface");
            return {};
        }
        if (rig.fn.vkGetPhysicalDeviceWin32PresentationSupportKHR != nullptr) {
            for (std::uint32_t family = 0; family < rig.families.size(); ++family) {
                VkBool32 surface_support = VK_FALSE;
                rig.fn.vkGetPhysicalDeviceSurfaceSupportKHR(rig.physical, family, stage.chain.surface, &surface_support);
                if (surface_support != VK_FALSE && rig.fn.vkGetPhysicalDeviceWin32PresentationSupportKHR(rig.physical, family) == VK_FALSE) {
                    problems.add("family " + std::to_string(family) + " supports the surface but vkGetPhysicalDeviceWin32PresentationSupportKHR says no");
                }
            }
        }
        const VkSurfaceCapabilitiesKHR caps = stage.chain.capabilities();
        if (caps.minImageCount == 0 || (caps.maxImageCount != 0 && caps.maxImageCount < caps.minImageCount)) {
            problems.add("image count range " + std::to_string(caps.minImageCount) + ".." + std::to_string(caps.maxImageCount) + " is inconsistent");
        }
        if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
            problems.add("supportedUsageFlags lacks COLOR_ATTACHMENT, which the specification requires");
        }
        if (caps.supportedTransforms == 0 || (caps.supportedTransforms & static_cast<VkSurfaceTransformFlagsKHR>(caps.currentTransform)) == 0 || caps.supportedCompositeAlpha == 0 || caps.maxImageArrayLayers == 0) {
            problems.add("surface transform, composite alpha or layer capabilities are empty");
        }
        if (caps.currentExtent.width != 0xFFFFFFFFu) {
            if (caps.currentExtent.width < caps.minImageExtent.width || caps.currentExtent.width > caps.maxImageExtent.width || caps.currentExtent.height < caps.minImageExtent.height ||
                caps.currentExtent.height > caps.maxImageExtent.height) {
                problems.add("currentExtent lies outside the min/max image extent");
            }
            if (static_cast<int>(caps.currentExtent.width) != stage.window.width() || static_cast<int>(caps.currentExtent.height) != stage.window.height()) {
                problems.add("currentExtent " + std::to_string(caps.currentExtent.width) + "x" + std::to_string(caps.currentExtent.height) + " differs from the window client area " +
                             std::to_string(stage.window.width()) + "x" + std::to_string(stage.window.height()));
            }
        }
        const std::vector<VkSurfaceFormatKHR> formats = stage.chain.formats();
        const std::vector<VkPresentModeKHR> modes = stage.chain.modes();
        if (formats.empty()) {
            problems.add("the surface reports no formats");
        }
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_FIFO_KHR) == modes.end()) {
            problems.add("VK_PRESENT_MODE_FIFO_KHR is not reported, which the specification requires");
        }
        std::uint32_t count = static_cast<std::uint32_t>(formats.size());
        if (count > 1) {
            std::uint32_t small = 1;
            VkSurfaceFormatKHR one{};
            Kit::expect(problems, "surface formats with room for one", rig.fn.vkGetPhysicalDeviceSurfaceFormatsKHR(rig.physical, stage.chain.surface, &small, &one), {VK_INCOMPLETE});
        }
        count = static_cast<std::uint32_t>(modes.size());
        if (count > 1) {
            std::uint32_t small = 1;
            VkPresentModeKHR one{};
            Kit::expect(problems, "present modes with room for one", rig.fn.vkGetPhysicalDeviceSurfacePresentModesKHR(rig.physical, stage.chain.surface, &small, &one), {VK_INCOMPLETE});
        }
        const VkExtent2D extent = stage.extent();
        std::size_t created = 0;
        for (const VkSurfaceFormatKHR& format : formats) {
            for (const VkPresentModeKHR mode : modes) {
                for (const std::uint32_t wanted : {caps.minImageCount, std::min(std::max(caps.minImageCount + 1, 3u), caps.maxImageCount == 0 ? 8u : caps.maxImageCount)}) {
                    const std::string what = "swapchain format " + VulkanTools::format_name(format.format) + ", mode " + std::to_string(mode) + ", " + std::to_string(wanted) + " images";
                    if (Kit::succeeds(problems, what, stage.chain.create(format.format, format.colorSpace, mode, wanted, extent))) {
                        ++created;
                        if (stage.chain.images.size() < wanted) {
                            problems.add(what + " grants " + std::to_string(stage.chain.images.size()) + " images, fewer than requested");
                        }
                        if (caps.maxImageCount != 0 && stage.chain.images.size() > caps.maxImageCount) {
                            problems.add(what + " grants " + std::to_string(stage.chain.images.size()) + " images, above maxImageCount " + std::to_string(caps.maxImageCount));
                        }
                        if (stage.chain.images.size() > 1) {
                            std::uint32_t got = static_cast<std::uint32_t>(stage.chain.images.size());
                            std::uint32_t small = 1;
                            VkImage one = VK_NULL_HANDLE;
                            Kit::expect(problems, what + " image enumeration with room for one", rig.fn.vkGetSwapchainImagesKHR(rig.device, stage.chain.swapchain, &small, &one),
                                        {VK_INCOMPLETE});
                            (void)got;
                        }
                    }
                }
            }
        }
        context.measure("vk.swapchains_created", created);
        stage.chain.destroy();
        return {};
    });
}

Outcome WsiChecks::fifo_every_image_in_order(Context& context) {
    return Kit::each_device(context, surface_options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        Stage stage;
        const std::string skipped = stage.open(rig);
        if (!skipped.empty()) {
            return skipped;
        }
        VkSurfaceFormatKHR format{};
        if (!stage.pick(format) || (stage.chain.capabilities().supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            return "B8G8R8A8_UNORM or TRANSFER_DST swapchain images are not offered";
        }
        const VkSurfaceCapabilitiesKHR caps = stage.chain.capabilities();
        if (!Kit::succeeds(problems, "FIFO swapchain", stage.chain.create(format.format, format.colorSpace, VK_PRESENT_MODE_FIFO_KHR, std::max(caps.minImageCount, 3u), stage.extent()))) {
            return {};
        }
        int shown = 0;
        std::vector<int> seen_images;
        for (int k = 0; k < 24; ++k) {
            std::uint32_t index = 0;
            const VkResult acquired = stage.chain.acquire(5ull * 1000000000ull, index);
            if (!ok_present(acquired)) {
                Kit::expect(problems, "acquire of frame " + std::to_string(k), acquired, {VK_SUCCESS, VK_SUBOPTIMAL_KHR});
                break;
            }
            const float color[4] = {shade(k), shade(k), shade(k), 1.0f};
            const VkResult presented = stage.chain.paint_and_present(index, color);
            if (!ok_present(presented)) {
                Kit::expect(problems, "present of frame " + std::to_string(k), presented, {VK_SUCCESS, VK_SUBOPTIMAL_KHR});
                break;
            }
            seen_images.push_back(static_cast<int>(index));
            if (wait_for_shade(stage, k, 2000)) {
                ++shown;
            } else {
                problems.add("frame " + std::to_string(k) + " never appeared on screen in FIFO mode");
            }
        }
        context.measure("vk.fifo_frames_shown", shown);
        for (std::size_t i = 0; i < seen_images.size() && i < stage.chain.images.size(); ++i) {
            for (std::size_t j = i + 1; j < seen_images.size() && j < stage.chain.images.size(); ++j) {
                if (seen_images[i] == seen_images[j]) {
                    problems.add("acquire returned image " + std::to_string(seen_images[i]) + " twice within the first " + std::to_string(stage.chain.images.size()) +
                                 " frames although it was presented in between and the chain is FIFO");
                    i = seen_images.size();
                    break;
                }
            }
        }
        return {};
    });
}

Outcome WsiChecks::mailbox_immediate_caps(Context& context) {
    return Kit::each_device(context, surface_options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        Stage stage;
        const std::string skipped = stage.open(rig);
        if (!skipped.empty()) {
            return skipped;
        }
        VkSurfaceFormatKHR format{};
        if (!stage.pick(format) || (stage.chain.capabilities().supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            return "B8G8R8A8_UNORM or TRANSFER_DST swapchain images are not offered";
        }
        const std::vector<VkPresentModeKHR> modes = stage.chain.modes();
        const VkSurfaceCapabilitiesKHR caps = stage.chain.capabilities();
        double fifo_ms = 0;
        for (const VkPresentModeKHR mode : {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            const bool listed = std::find(modes.begin(), modes.end(), mode) != modes.end();
            const std::string name = mode == VK_PRESENT_MODE_FIFO_KHR ? "FIFO" : mode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "IMMEDIATE";
            if (!listed) {
                context.note(name + " is not reported and is not used");
                continue;
            }
            if (!Kit::succeeds(problems, name + " swapchain", stage.chain.create(format.format, format.colorSpace, mode, std::max(caps.minImageCount, 3u), stage.extent()))) {
                continue;
            }
            std::atomic<bool> stop{false};
            std::vector<int> observed;
            std::thread sampler;
            POINT point{stage.client_width / 2, stage.client_height / 2};
            ::ClientToScreen(stage.window.hwnd(), &point);
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                sampler = std::thread([&] {
                    while (!stop.load()) {
                        int rgb[3] = {};
                        if (SwapRig::screen_rgb(point.x, point.y, rgb)) {
                            observed.push_back(rgb[0]);
                        }
                    }
                });
            }
            const int frames = 90;
            const auto begin = std::chrono::steady_clock::now();
            bool failed = false;
            for (int k = 0; k < frames && !failed; ++k) {
                std::uint32_t index = 0;
                VkResult result = stage.chain.acquire(5ull * 1000000000ull, index);
                if (ok_present(result)) {
                    const float level = static_cast<float>(10 + k * 2) / 255.0f;
                    const float color[4] = {level, level, level, 1.0f};
                    result = stage.chain.paint_and_present(index, color);
                }
                if (!ok_present(result)) {
                    Kit::expect(problems, name + " frame " + std::to_string(k), result, {VK_SUCCESS, VK_SUBOPTIMAL_KHR});
                    failed = true;
                }
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / frames;
            stop = true;
            if (sampler.joinable()) {
                sampler.join();
            }
            context.measure("vk.present_ms." + name, ms);
            if (mode == VK_PRESENT_MODE_FIFO_KHR) {
                fifo_ms = ms;
            } else if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR && fifo_ms > 0 && ms > fifo_ms * 1.1) {
                problems.add("IMMEDIATE presents at " + std::to_string(ms) + " ms per frame, slower than FIFO at " + std::to_string(fifo_ms) + " ms");
            }
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                int highest = -1;
                for (const int value : observed) {
                    const bool frame_value = value >= 10 && value <= 10 + frames * 2 && (value - 10) % 2 == 0;
                    if (!frame_value) {
                        continue;
                    }
                    if (value < highest) {
                        problems.add("MAILBOX showed frame value " + std::to_string(value) + " after the newer value " + std::to_string(highest));
                        break;
                    }
                    highest = value;
                }
            }
        }
        return {};
    });
}

Outcome WsiChecks::out_of_date_suboptimal(Context& context) {
    return Kit::each_device(context, surface_options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        Stage stage;
        const std::string skipped = stage.open(rig);
        if (!skipped.empty()) {
            return skipped;
        }
        VkSurfaceFormatKHR format{};
        if (!stage.pick(format) || (stage.chain.capabilities().supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            return "B8G8R8A8_UNORM or TRANSFER_DST swapchain images are not offered";
        }
        const VkSurfaceCapabilitiesKHR caps = stage.chain.capabilities();
        if (!Kit::succeeds(problems, "initial swapchain", stage.chain.create(format.format, format.colorSpace, VK_PRESENT_MODE_FIFO_KHR, std::max(caps.minImageCount, 2u), stage.extent()))) {
            return {};
        }
        std::uint32_t index = 0;
        if (Kit::succeeds(problems, "first acquire", stage.chain.acquire(5ull * 1000000000ull, index))) {
            const float color[4] = {shade(1), shade(1), shade(1), 1.0f};
            Kit::succeeds(problems, "first present", stage.chain.paint_and_present(index, color));
        }
        const int new_width = 160;
        const int new_height = 120;
        int outer_width = 0;
        int outer_height = 0;
        TestWindow::outer_size(80, 80, new_width, new_height, outer_width, outer_height);
        stage.window.resize(outer_width, outer_height);
        stage.client_width = new_width;
        stage.client_height = new_height;
        ::Sleep(200);
        const VkExtent2D grown = stage.extent();
        if (static_cast<int>(grown.width) != new_width || static_cast<int>(grown.height) != new_height) {
            problems.add("after the resize the surface reports " + std::to_string(grown.width) + "x" + std::to_string(grown.height) + ", the client area is " + std::to_string(new_width) + "x" +
                         std::to_string(new_height));
        }
        VkResult acquired = stage.chain.acquire(5ull * 1000000000ull, index);
        Kit::expect(problems, "acquire from the old-size swapchain after the resize", acquired, {VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR});
        if (ok_present(acquired)) {
            const float color[4] = {shade(2), shade(2), shade(2), 1.0f};
            const VkResult presented = stage.chain.paint_and_present(index, color);
            Kit::expect(problems, "present to the old-size swapchain after the resize", presented, {VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR});
        }
        if (!Kit::succeeds(problems, "recreated swapchain at the new extent", stage.chain.create(format.format, format.colorSpace, VK_PRESENT_MODE_FIFO_KHR, std::max(caps.minImageCount, 2u), grown))) {
            return {};
        }
        if (stage.chain.extent.width != grown.width) {
            problems.add("the recreated swapchain keeps the wrong extent");
        }
        acquired = stage.chain.acquire(5ull * 1000000000ull, index);
        if (Kit::expect(problems, "acquire from the recreated swapchain", acquired, {VK_SUCCESS})) {
            const float color[4] = {shade(3), shade(3), shade(3), 1.0f};
            if (Kit::expect(problems, "present to the recreated swapchain", stage.chain.paint_and_present(index, color), {VK_SUCCESS}) && !wait_for_shade(stage, 3, 2000)) {
                problems.add("the frame presented after recreating the swapchain never appeared in the resized window");
            }
        }
        return {};
    });
}

Outcome WsiChecks::acquire_timeouts(Context& context) {
    return Kit::each_device(context, surface_options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        Stage stage;
        const std::string skipped = stage.open(rig);
        if (!skipped.empty()) {
            return skipped;
        }
        VkSurfaceFormatKHR format{};
        if (!stage.pick(format) || (stage.chain.capabilities().supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            return "B8G8R8A8_UNORM or TRANSFER_DST swapchain images are not offered";
        }
        const VkSurfaceCapabilitiesKHR caps = stage.chain.capabilities();
        if (!Kit::succeeds(problems, "swapchain", stage.chain.create(format.format, format.colorSpace, VK_PRESENT_MODE_FIFO_KHR, caps.minImageCount, stage.extent()))) {
            return {};
        }
        const std::uint32_t granted = static_cast<std::uint32_t>(stage.chain.images.size());
        const std::uint32_t acquirable = granted - caps.minImageCount + 1;
        std::vector<std::uint32_t> held;
        for (std::uint32_t i = 0; i < acquirable; ++i) {
            std::uint32_t index = 0;
            const VkResult result = stage.chain.acquire(5ull * 1000000000ull, index);
            if (!Kit::expect(problems, "acquire " + std::to_string(i + 1) + " of " + std::to_string(acquirable) + " acquirable images", result, {VK_SUCCESS, VK_SUBOPTIMAL_KHR})) {
                return {};
            }
            held.push_back(index);
        }
        std::uint32_t extra = 0;
        for (std::uint32_t i = acquirable; i < granted; ++i) {
            const VkResult result = stage.chain.acquire(20000000, extra);
            if (!Kit::expect(problems, "acquire " + std::to_string(i + 1) + " of " + std::to_string(granted) + " images, beyond the guaranteed ones", result,
                             {VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_NOT_READY, VK_TIMEOUT})) {
                break;
            }
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                break;
            }
            held.push_back(extra);
        }
        context.measure("vk.acquirable_images_held", static_cast<std::uint64_t>(held.size()));
        if (held.size() == granted) {
            Kit::expect(problems, "acquire with timeout 0 when no image is free", stage.chain.acquire(0, extra), {VK_NOT_READY});
            Kit::expect(problems, "acquire with a 20 ms timeout when no image is free", stage.chain.acquire(20000000, extra), {VK_TIMEOUT});
        }
        const float color[4] = {shade(4), shade(4), shade(4), 1.0f};
        for (const std::uint32_t index : held) {
            Kit::expect(problems, "present of a held image", stage.chain.paint_and_present(index, color), {VK_SUCCESS, VK_SUBOPTIMAL_KHR});
        }
        const VkResult again = stage.chain.acquire(5ull * 1000000000ull, extra);
        Kit::expect(problems, "acquire after the held images were presented", again, {VK_SUCCESS, VK_SUBOPTIMAL_KHR});
        return {};
    });
}

Outcome WsiChecks::minimized_and_destroyed(Context& context) {
    return Kit::each_device(context, surface_options(), [&](VulkanRig& rig, Problems& problems) -> std::string {
        VkSurfaceFormatKHR format{};
        const std::string probe = [&] {
            Stage stage;
            const std::string skipped = stage.open(rig);
            if (!skipped.empty()) {
                return skipped;
            }
            if (!stage.pick(format) || (stage.chain.capabilities().supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
                return std::string("B8G8R8A8_UNORM or TRANSFER_DST swapchain images are not offered");
            }
            return std::string();
        }();
        if (!probe.empty()) {
            return probe;
        }
        for (int order = 0; order < 3; ++order) {
            Stage stage;
            if (!stage.open(rig).empty()) {
                continue;
            }
            const std::string name = order == 0 ? "swapchain, surface, window" : order == 1 ? "window, swapchain, surface" : "surface with the swapchain, window last";
            const VkSurfaceCapabilitiesKHR caps = stage.chain.capabilities();
            if (!Kit::succeeds(problems, name + ": swapchain", stage.chain.create(format.format, format.colorSpace, VK_PRESENT_MODE_FIFO_KHR, std::max(caps.minImageCount, 2u), stage.extent()))) {
                continue;
            }
            std::uint32_t index = 0;
            const float color[4] = {shade(5), shade(5), shade(5), 1.0f};
            if (Kit::expect(problems, name + ": acquire", stage.chain.acquire(5ull * 1000000000ull, index), {VK_SUCCESS, VK_SUBOPTIMAL_KHR})) {
                Kit::expect(problems, name + ": present", stage.chain.paint_and_present(index, color), {VK_SUCCESS, VK_SUBOPTIMAL_KHR});
            }
            if (order == 0) {
                ::ShowWindow(stage.window.hwnd(), SW_MINIMIZE);
                ::Sleep(150);
                std::uint32_t minimized_index = 0;
                const VkResult acquired = stage.chain.acquire(1ull * 1000000000ull, minimized_index);
                Kit::expect(problems, "acquire from a minimized window", acquired, {VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR, VK_TIMEOUT, VK_NOT_READY});
                if (ok_present(acquired)) {
                    Kit::expect(problems, "present to a minimized window", stage.chain.paint_and_present(minimized_index, color),
                                {VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR});
                }
                ::ShowWindow(stage.window.hwnd(), SW_RESTORE);
                stage.chain.destroy_swapchain();
                stage.chain.destroy_surface();
                stage.window.destroy();
            } else if (order == 1) {
                rig.wait_idle();
                stage.window.destroy();
                stage.chain.destroy_swapchain();
                stage.chain.destroy_surface();
            } else {
                stage.chain.destroy_swapchain();
                stage.chain.destroy_surface();
                stage.window.destroy();
            }
            VulkanBuffer source;
            VulkanBuffer sink;
            if (Kit::succeeds(problems, name + ": host buffers", Kit::host_buffer(rig, source, 4096)) && Kit::succeeds(problems, name + ": sink", Kit::host_buffer(rig, sink, 4096))) {
                source.fill_pattern(0x3300 + static_cast<std::uint64_t>(order));
                source.publish();
                sink.fill_value(0xCD);
                sink.publish();
                if (Kit::succeeds(problems, name + ": device usable afterwards", Kit::copy(rig, source.buffer, sink.buffer, 4096))) {
                    sink.acquire();
                    Kit::same_bytes(context, problems, name + ": copy after the teardown", source.bytes(), sink.bytes(), 4096);
                }
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
