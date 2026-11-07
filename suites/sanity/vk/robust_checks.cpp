#include "sanity/vk/robust_checks.hpp"

#include "sanity/vk/kit.hpp"
#include "support/vulkan_image_io.hpp"
#include "support/vulkan_swapchain.hpp"

#include <cstring>
#include <memory>

namespace lsw::suites::sanity::vk {

namespace {

constexpr const char* kValidation = "VK_LAYER_KHRONOS_validation";

void roundtrip(Context& context, VulkanRig& rig, Problems& problems, const std::string& label, std::uint64_t seed) {
    VulkanBuffer source;
    VulkanBuffer middle;
    VulkanBuffer sink;
    const VkDeviceSize size = 256 * 1024;
    if (!Kit::succeeds(problems, label + " source", Kit::host_buffer(rig, source, size)) || !Kit::succeeds(problems, label + " middle", Kit::device_buffer(rig, middle, size)) ||
        !Kit::succeeds(problems, label + " sink", Kit::host_buffer(rig, sink, size))) {
        return;
    }
    source.fill_pattern(seed);
    source.publish();
    sink.fill_value(0xCD);
    sink.publish();
    if (Kit::succeeds(problems, label + " upload", Kit::copy(rig, source.buffer, middle.buffer, size)) && Kit::succeeds(problems, label + " download", Kit::copy(rig, middle.buffer, sink.buffer, size))) {
        sink.acquire();
        Kit::same_bytes(context, problems, label + " data", source.bytes(), sink.bytes(), size);
    }
}

}  // namespace

Outcome RobustChecks::independent_devices(Context& context) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Outcome::fail("no target adapter: " + error);
    }
    Problems problems;
    for (const AvAdapter& adapter : adapters) {
        auto first = std::make_unique<VulkanRig>();
        auto second = std::make_unique<VulkanRig>();
        if (!first->open(adapter, VulkanRigOptions{}, error) || !second->open(adapter, VulkanRigOptions{}, error)) {
            problems.add(adapter.name + ": " + error);
            continue;
        }
        const std::string label = first->describe();
        roundtrip(context, *first, problems, label + " first device", 0x1001);
        roundtrip(context, *second, problems, label + " second device", 0x1002);
        Kit::succeeds(problems, label + " vkDeviceWaitIdle on the first device", first->wait_idle());
        first.reset();
        roundtrip(context, *second, problems, label + " second device after the first was destroyed", 0x1003);
        auto third = std::make_unique<VulkanRig>();
        if (!third->open(adapter, VulkanRigOptions{}, error)) {
            problems.add(label + ": a new device after destroying the first fails: " + error);
        } else {
            roundtrip(context, *third, problems, label + " new device", 0x1004);
            Kit::succeeds(problems, label + " vkDeviceWaitIdle on the new device", third->wait_idle());
        }
        second->wait_idle();
    }
    context.note("device loss is not provoked: every documented way of forcing it submits invalid usage or an unbounded shader");
    return problems.outcome();
}

Outcome RobustChecks::reference_workload(Context& context) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Outcome::fail("no target adapter: " + error);
    }
    Problems problems;
    std::vector<std::string> skips;
    for (const AvAdapter& adapter : adapters) {
        VulkanRigOptions options;
        options.layers = {kValidation};
        options.debug_messages = true;
        options.surface = true;
        VulkanRig rig;
        if (!rig.open(adapter, options, error)) {
            if (error.find("is not installed") != std::string::npos) {
                skips.push_back(std::string(kValidation) + " is not installed");
            } else {
                problems.add(adapter.name + ": " + error);
            }
            continue;
        }
        const std::string label = rig.describe();
        Problems inner;
        roundtrip(context, rig, inner, "buffers", 0x2001);
        {
            VulkanCompute program;
            VulkanBuffer input;
            VulkanBuffer output;
            VulkanBuffer params;
            if (Kit::succeeds(inner, "compute pipeline",
                              program.create(rig, VulkanShaders::words(VulkanShader::Ops),
                                             {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, 8)) &&
                Kit::succeeds(inner, "input", Kit::host_buffer(rig, input, 1024)) && Kit::succeeds(inner, "output", Kit::host_buffer(rig, output, 1024)) &&
                Kit::succeeds(inner, "params", Kit::host_buffer(rig, params, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))) {
                input.fill_pattern(0x2002);
                input.publish();
                output.fill_value(0);
                output.publish();
                params.words()[0] = 2;
                params.words()[1] = 1;
                params.words()[2] = 256;
                params.words()[3] = 0;
                params.publish();
                program.write_buffer(0, input.buffer);
                program.write_buffer(1, output.buffer);
                program.write_buffer(2, params.buffer);
                const std::uint32_t push[2] = {0, 0};
                Kit::succeeds(inner, "compute dispatch", program.run(push, sizeof(push), 4));
            }
        }
        {
            SceneOptions scene_options;
            scene_options.depth = true;
            scene_options.blend = true;
            VulkanScene scene;
            SceneRect a;
            a.x0 = 4;
            a.y0 = 4;
            a.x1 = 20;
            a.y1 = 20;
            a.color[3] = 0.5f;
            SceneRect b = a;
            b.x0 = 10;
            b.y0 = 10;
            b.x1 = 28;
            b.y1 = 28;
            b.depth = 0.25f;
            const float clear[4] = {0, 0, 0, 1};
            SceneResult out;
            if (Kit::succeeds(inner, "scene", scene.create(rig, scene_options))) {
                Kit::succeeds(inner, "scene render", scene.render({a, b}, clear, 1.0f, out));
            }
        }
        {
            VulkanImage image;
            if (Kit::succeeds(inner, "image", image.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, 32, 32, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) &&
                Kit::succeeds(inner, "image layout", VulkanImageIo::to_general(rig, image))) {
                ImageRegion region;
                region.extent = {32, 32, 1};
                std::vector<std::uint8_t> data(32 * 32 * 4);
                Pattern::fill(0x2003, 0, data.data(), data.size());
                std::vector<std::uint8_t> back;
                if (Kit::succeeds(inner, "image write", VulkanImageIo::write(rig, image, region, data))) {
                    Kit::succeeds(inner, "image read", VulkanImageIo::read(rig, image, region, back, 4));
                }
            }
        }
        {
            Kit::succeeds(inner, "queue idle", rig.fn.vkQueueWaitIdle(rig.main().handle));
        }
        if (rig.instance_has(vkw::kSurfaceExtension) && rig.enabled_has("VK_KHR_swapchain")) {
            TestWindow window;
            TestWindowConfig config;
            config.x = 90;
            config.y = 90;
            TestWindow::outer_size(config.x, config.y, 96, 64, config.width, config.height);
            std::string window_error;
            if (window.create(config, window_error)) {
                VulkanSwapchain chain;
                if (chain.open(rig, window.hwnd()) == VK_SUCCESS && chain.presentable()) {
                    VkSurfaceFormatKHR format{};
                    bool found = false;
                    for (const VkSurfaceFormatKHR& item : chain.formats()) {
                        if (item.format == VK_FORMAT_B8G8R8A8_UNORM) {
                            format = item;
                            found = true;
                        }
                    }
                    const VkSurfaceCapabilitiesKHR caps = chain.capabilities();
                    if (found && (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0 &&
                        chain.create(format.format, format.colorSpace, VK_PRESENT_MODE_FIFO_KHR, caps.minImageCount,
                                     {static_cast<std::uint32_t>(window.width()), static_cast<std::uint32_t>(window.height())}) == VK_SUCCESS) {
                        for (int frame = 0; frame < 3; ++frame) {
                            std::uint32_t index = 0;
                            const float color[4] = {0.2f, 0.4f, 0.6f, 1.0f};
                            if (chain.acquire(5ull * 1000000000ull, index) == VK_SUCCESS) {
                                (void)chain.paint_and_present(index, color);
                            }
                        }
                    }
                }
                rig.wait_idle();
                chain.destroy();
            }
        }
        rig.wait_idle();
        std::string first;
        int errors = 0;
        int warnings = 0;
        rig.drain_debug(first, errors, warnings);
        context.measure("vk.validation_errors", errors);
        context.measure("vk.validation_warnings", warnings);
        if (!inner.empty()) {
            problems.add(label + ": " + inner.text());
        }
        if (errors != 0 || warnings != 0) {
            problems.add(label + ": the validation layer reported " + std::to_string(errors) + " errors and " + std::to_string(warnings) + " warnings: " + first);
        }
    }
    if (problems.empty() && !skips.empty() && skips.size() == adapters.size()) {
        return Context::skip(skips.front());
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::vk
