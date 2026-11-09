#include "render/vk/quadkit.hpp"

#include <vector>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};

std::string timestamp_caps(Kit& kit) {
    if (kit.properties.limits.timestampComputeAndGraphics == VK_FALSE) {
        return "timestampComputeAndGraphics";
    }
    std::uint32_t count = 0;
    kit.api.vkGetPhysicalDeviceQueueFamilyProperties(kit.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    kit.api.vkGetPhysicalDeviceQueueFamilyProperties(kit.physical, &count, families.data());
    return families[kit.family].timestampValidBits == 0 ? std::string("timestampValidBits") : std::string();
}

void register_timestamps() {
    CaseSpec spec("query", "timestamps_monotonic_and_image_untouched");
    spec.tolerance(Tolerance::exact());
    Cases::add(
        std::move(spec),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            QuadKit quads(rig);
            PipelineDesc desc = quads.desc(target);
            desc.blend = true;
            const VkPipeline pipeline = rig.pipe(desc);
            constexpr std::uint32_t kStamps = 6;
            VkQueryPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = kStamps;
            VkQueryPool pool = VK_NULL_HANDLE;
            rig.vk_ok(kit.api.vkCreateQueryPool(kit.device, &info, nullptr, &pool), "vkCreateQueryPool");
            kit.defer([&kit, pool] { kit.api.vkDestroyQueryPool(kit.device, pool, nullptr); });
            const auto layers = [&](VkCommandBuffer cb, unsigned first, unsigned count) {
                for (unsigned i = 0; i < count; ++i) {
                    const float a = 0.25f + 0.02f * static_cast<float>(first + i);
                    const float color[4] = {palette(first + i)[0], palette(first + i)[1], palette(first + i)[2], a};
                    quads.quad(cb, -0.9f + 0.05f * static_cast<float>(first + i), -0.9f + 0.04f * static_cast<float>(first + i), 1.6f, 1.6f, color);
                }
            };
            const Image plain = rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                layers(cb, 0, 8);
                layers(cb, 8, 8);
                layers(cb, 16, 8);
            }, quads.layout);
            rig.run([&](VkCommandBuffer cb) {
                kit.api.vkCmdResetQueryPool(cb, pool, 0, kStamps);
                kit.api.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
                target.begin(kit, cb, true);
                rig.bind_graphics(cb, pipeline, quads.layout);
                kit.api.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, pool, 1);
                layers(cb, 0, 8);
                kit.api.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, pool, 2);
                layers(cb, 8, 8);
                kit.api.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, pool, 3);
                layers(cb, 16, 8);
                kit.api.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, pool, 4);
                target.end(kit, cb);
                kit.api.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, 5);
            });
            std::uint64_t times[kStamps] = {};
            rig.vk_ok(kit.api.vkGetQueryPoolResults(kit.device, pool, 0, kStamps, sizeof times, times, sizeof times[0],
                                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                      "vkGetQueryPoolResults");
            std::uint32_t count = 0;
            kit.api.vkGetPhysicalDeviceQueueFamilyProperties(kit.physical, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            kit.api.vkGetPhysicalDeviceQueueFamilyProperties(kit.physical, &count, families.data());
            const std::uint32_t bits = families[kit.family].timestampValidBits;
            const std::uint64_t mask = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
            rig.must(kit.properties.limits.timestampPeriod > 0.0f, "timestampPeriod is not positive");
            for (std::uint32_t i = 0; i < kStamps; ++i) {
                rig.must((times[i] & ~mask) == 0, "a timestamp has bits above timestampValidBits");
                if (i != 0) {
                    rig.must(times[i] >= times[i - 1], "timestamp " + std::to_string(i) + " is earlier than timestamp " + std::to_string(i - 1));
                }
            }
            rig.must(times[kStamps - 1] > times[0], "the timestamps around the whole pass did not advance");
            const Image stamped = rig.color(target);
            rig.must(stamped.bytes() == plain.bytes(), "writing timestamps changed the image");
            out.add("", stamped);
        },
        timestamp_caps);
}

const int registered = (register_timestamps(), 0);

}  // namespace

}  // namespace lsw::render::vk
