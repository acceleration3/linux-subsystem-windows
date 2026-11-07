#include "sanity/vk/graphics_checks.hpp"

#include "sanity/vk/kit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::vk {

namespace {

SceneRect rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a, float depth) {
    SceneRect out;
    out.x0 = x0;
    out.y0 = y0;
    out.x1 = x1;
    out.y1 = y1;
    out.color[0] = r;
    out.color[1] = g;
    out.color[2] = b;
    out.color[3] = a;
    out.depth = depth;
    return out;
}

bool compare_bytes(Context& context, Problems& problems, const std::string& what, const std::vector<std::uint8_t>& expected,
                   const std::vector<std::uint8_t>& actual, std::uint32_t width, int tolerance) {
    if (expected.size() != actual.size()) {
        problems.add(what + ": " + std::to_string(actual.size()) + " bytes read, expected " + std::to_string(expected.size()));
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (std::abs(static_cast<int>(expected[i]) - static_cast<int>(actual[i])) > tolerance) {
            const std::size_t pixel = i / 4;
            problems.add(what + ": pixel (" + std::to_string(pixel % width) + "," + std::to_string(pixel / width) + ") channel " + std::to_string(i % 4) + " is " +
                         std::to_string(actual[i]) + ", expected " + std::to_string(expected[i]) + (tolerance != 0 ? " within " + std::to_string(tolerance) : ""));
            ArtifactWriter writer(context);
            const std::string base = Kit::sanitize(what);
            writer.image(base + ".expected", expected.data(), width, static_cast<std::uint32_t>(expected.size() / 4 / width), width * 4);
            writer.image(base + ".actual", actual.data(), width, static_cast<std::uint32_t>(actual.size() / 4 / width), width * 4);
            return false;
        }
    }
    return true;
}

void run_scene(Context& context, VulkanRig& rig, Problems& problems, const std::string& name, const SceneOptions& options, const std::vector<SceneRect>& rects,
               const float clear[4], float clear_depth, int tolerance) {
    VulkanScene scene;
    if (!Kit::succeeds(problems, name + " setup", scene.create(rig, options))) {
        return;
    }
    SceneResult out;
    if (!Kit::succeeds(problems, name + " render", scene.render(rects, clear, clear_depth, out))) {
        return;
    }
    compare_bytes(context, problems, name + " colour", scene.expected_color(rects, clear, clear_depth, false), out.color, options.width, tolerance);
    if (options.second) {
        compare_bytes(context, problems, name + " second attachment", scene.expected_color(rects, clear, clear_depth, true), out.second, options.width, tolerance);
    }
    if (options.depth && options.samples == VK_SAMPLE_COUNT_1_BIT) {
        const std::vector<float> expected = scene.expected_depth(rects, clear_depth);
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (out.depth[i] != expected[i]) {
                problems.add(name + " depth: pixel (" + std::to_string(i % options.width) + "," + std::to_string(i / options.width) + ") holds " + std::to_string(out.depth[i]) +
                             ", expected " + std::to_string(expected[i]));
                break;
            }
        }
    }
}

bool covered(const std::vector<std::uint8_t>& color, std::uint32_t width, std::uint32_t x, std::uint32_t y, const float rgba[4]) {
    const std::uint8_t* pixel = color.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    for (int c = 0; c < 4; ++c) {
        if (pixel[c] != VulkanScene::unorm8(rgba[c])) {
            return false;
        }
    }
    return true;
}

}  // namespace

Outcome GraphicsChecks::vs_twin(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const float clear[4] = {0.0f, 0.0f, 0.2f, 1.0f};
        const std::vector<SceneRect> painters = {rect(2, 2, 18, 18, 1, 0, 0, 1, 0.5f), rect(10, 10, 30, 26, 0, 1, 0, 1, 0.5f), rect(14, 4, 22, 30, 0, 0, 1, 1, 0.5f)};
        const std::vector<SceneRect> depth_set = {rect(4, 4, 20, 20, 1, 0, 0, 1, 0.5f), rect(12, 12, 30, 30, 0, 1, 0, 1, 0.75f), rect(8, 14, 24, 22, 0, 0, 1, 1, 0.25f),
                                                  rect(0, 24, 32, 28, 1, 1, 0, 1, 0.9f)};
        const std::vector<SceneRect> blend_set = {rect(2, 2, 26, 26, 1, 0, 0, 1, 0.5f), rect(8, 8, 30, 30, 0, 1, 0, 0.5f, 0.5f), rect(12, 4, 20, 28, 0, 0, 1, 0.25f, 0.5f)};
        const struct {
            const char* name;
            bool dynamic;
        } modes[] = {{"render pass", false}, {"dynamic rendering", true}};
        for (const auto& mode : modes) {
            if (mode.dynamic && !rig.dynamic_rendering) {
                context.note("dynamic rendering is not reported; its scenes are skipped");
                continue;
            }
            const std::string prefix = mode.name;
            SceneOptions options;
            options.dynamic = mode.dynamic;
            run_scene(context, rig, problems, prefix + " opaque painter's order", options, painters, clear, 1.0f, 0);
            SceneOptions bgra = options;
            bgra.format = VK_FORMAT_B8G8R8A8_UNORM;
            if ((Kit::format_properties(rig, bgra.format).optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
                run_scene(context, rig, problems, prefix + " BGRA target", bgra, painters, clear, 1.0f, 0);
            }
            SceneOptions depth = options;
            depth.depth = true;
            run_scene(context, rig, problems, prefix + " depth test LESS", depth, depth_set, clear, 1.0f, 0);
            depth.depth_op = VK_COMPARE_OP_GREATER;
            run_scene(context, rig, problems, prefix + " depth test GREATER", depth, depth_set, clear, 0.1f, 0);
            SceneOptions blend = options;
            blend.blend = true;
            run_scene(context, rig, problems, prefix + " alpha blending", blend, blend_set, clear, 1.0f, 1);
            SceneOptions mrt = options;
            mrt.second = true;
            run_scene(context, rig, problems, prefix + " two colour attachments", mrt, painters, clear, 1.0f, 0);
            SceneOptions combined = options;
            combined.depth = true;
            combined.blend = true;
            combined.second = true;
            run_scene(context, rig, problems, prefix + " depth, blending and two attachments", combined, depth_set, clear, 1.0f, 1);
            for (const VkSampleCountFlagBits samples : {VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT}) {
                const VkSampleCountFlags bit = static_cast<VkSampleCountFlags>(samples);
                if ((rig.props.limits.framebufferColorSampleCounts & bit) == 0 || (rig.props.limits.framebufferDepthSampleCounts & bit) == 0) {
                    continue;
                }
                VkImageFormatProperties limits{};
                if (!Kit::image_supported(rig, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                          0, limits) ||
                    (limits.sampleCounts & bit) == 0) {
                    continue;
                }
                SceneOptions msaa = options;
                msaa.samples = samples;
                msaa.depth = true;
                run_scene(context, rig, problems, prefix + " " + std::to_string(static_cast<int>(samples)) + " samples with depth", msaa, depth_set, clear, 1.0f, 0);
            }
        }
        return {};
    });
}

Outcome GraphicsChecks::pipeline_features(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const std::vector<SceneRect> block = {rect(8, 8, 24, 24, 1, 0.5f, 0, 1, 0.5f)};
        if (rig.features.tessellationShader != VK_FALSE) {
            SceneOptions options;
            options.kind = SceneKind::Tessellation;
            run_scene(context, rig, problems, "tessellation", options, block, clear, 1.0f, 0);
        } else {
            context.note("tessellationShader is not reported");
        }
        if (rig.features.geometryShader != VK_FALSE) {
            SceneOptions options;
            options.kind = SceneKind::Geometry;
            run_scene(context, rig, problems, "geometry shader", options, block, clear, 1.0f, 0);
        } else {
            context.note("geometryShader is not reported");
        }
        {
            SceneOptions options;
            options.secondary = true;
            run_scene(context, rig, problems, "secondary command buffer", options, block, clear, 1.0f, 0);
        }
        for (const float width : {1.0f, 3.0f}) {
            if (width != 1.0f && (rig.features.wideLines == VK_FALSE || width > rig.props.limits.lineWidthRange[1])) {
                context.note("wideLines is not reported; wide lines skipped");
                continue;
            }
            SceneOptions options;
            options.kind = SceneKind::Line;
            options.line_width = width;
            VulkanScene scene;
            const SceneRect line = rect(4, 16.5f, 28, 16.5f, 1, 1, 1, 1, 0.5f);
            SceneResult out;
            if (!Kit::succeeds(problems, "line scene", scene.create(rig, options)) || !Kit::succeeds(problems, "line render", scene.render({line}, clear, 1.0f, out))) {
                continue;
            }
            const float white[4] = {1, 1, 1, 1};
            const std::uint32_t half = static_cast<std::uint32_t>(width) / 2;
            for (std::uint32_t y = 0; y < 32; ++y) {
                const bool inside = y + half >= 16 && y <= 16 + half;
                for (std::uint32_t x = 6; x < 26; ++x) {
                    if (covered(out.color, 32, x, y, white) != inside) {
                        problems.add("line of width " + std::to_string(static_cast<int>(width)) + ": pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " +
                                     (inside ? "not drawn" : "drawn"));
                        y = 32;
                        break;
                    }
                }
            }
        }
        for (const float size : {1.0f, 5.0f}) {
            if (size != 1.0f && (rig.features.largePoints == VK_FALSE || size > rig.props.limits.pointSizeRange[1])) {
                context.note("largePoints is not reported; large points skipped");
                continue;
            }
            SceneOptions options;
            options.kind = SceneKind::Point;
            VulkanScene scene;
            SceneRect point = rect(16.5f, 16.5f, size, 0, 1, 1, 1, 1, 0.5f);
            SceneResult out;
            if (!Kit::succeeds(problems, "point scene", scene.create(rig, options)) || !Kit::succeeds(problems, "point render", scene.render({point}, clear, 1.0f, out))) {
                continue;
            }
            const float white[4] = {1, 1, 1, 1};
            const std::uint32_t half = static_cast<std::uint32_t>(size) / 2;
            for (std::uint32_t y = 0; y < 32; ++y) {
                for (std::uint32_t x = 0; x < 32; ++x) {
                    const bool inside = x + half >= 16 && x <= 16 + half && y + half >= 16 && y <= 16 + half;
                    if (covered(out.color, 32, x, y, white) != inside) {
                        problems.add("point of size " + std::to_string(static_cast<int>(size)) + ": pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " +
                                     (inside ? "not drawn" : "drawn"));
                        y = 32;
                        break;
                    }
                }
            }
        }
        if (rig.features.depthClamp != VK_FALSE) {
            const std::vector<SceneRect> beyond = {rect(2, 2, 14, 30, 1, 0, 0, 1, -0.5f), rect(18, 2, 30, 30, 0, 1, 0, 1, 1.5f), rect(8, 12, 26, 20, 0, 0, 1, 1, 0.5f)};
            for (const bool clamp : {true, false}) {
                SceneOptions options;
                options.depth = true;
                options.depth_clamp = clamp;
                options.depth_op = VK_COMPARE_OP_LESS_OR_EQUAL;
                run_scene(context, rig, problems, clamp ? "depth clamp enabled" : "depth clamp disabled", options, beyond, clear, 1.0f, 0);
            }
        } else {
            context.note("depthClamp is not reported");
        }
        VkPipelineCacheCreateInfo cache_info{};
        cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VulkanOwned<VkPipelineCache> cache;
        if (Kit::succeeds(problems, "pipeline cache", rig.make(cache, rig.fn.vkCreatePipelineCache, rig.fn.vkDestroyPipelineCache, cache_info))) {
            SceneOptions options;
            options.cache = cache.get();
            run_scene(context, rig, problems, "pipeline built into an empty cache", options, block, clear, 1.0f, 0);
            std::size_t size = 0;
            if (Kit::succeeds(problems, "pipeline cache size query", rig.fn.vkGetPipelineCacheData(rig.device, cache.get(), &size, nullptr)) && size >= 32) {
                std::vector<std::uint8_t> data(size);
                std::size_t got = size;
                if (Kit::succeeds(problems, "pipeline cache data", rig.fn.vkGetPipelineCacheData(rig.device, cache.get(), &got, data.data()))) {
                    std::uint32_t header[4] = {};
                    std::memcpy(header, data.data(), 16);
                    if (header[0] < 32 || header[1] != VK_PIPELINE_CACHE_HEADER_VERSION_ONE || header[2] != rig.props.vendorID || header[3] != rig.props.deviceID ||
                        std::memcmp(data.data() + 16, rig.props.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
                        problems.add("the pipeline cache header does not match VK_PIPELINE_CACHE_HEADER_VERSION_ONE for this device (length " + std::to_string(header[0]) +
                                     ", version " + std::to_string(header[1]) + ", vendor 0x" + Kit::hex(header[2], 4) + ", device 0x" + Kit::hex(header[3], 4) + ")");
                    }
                    std::size_t small = size - 1;
                    std::vector<std::uint8_t> shorter(small);
                    Kit::expect(problems, "pipeline cache data with a too small buffer", rig.fn.vkGetPipelineCacheData(rig.device, cache.get(), &small, shorter.data()),
                                {VK_INCOMPLETE});
                    VkPipelineCacheCreateInfo reuse_info{};
                    reuse_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
                    reuse_info.initialDataSize = got;
                    reuse_info.pInitialData = data.data();
                    VulkanOwned<VkPipelineCache> reused;
                    if (Kit::succeeds(problems, "pipeline cache created from its own data", rig.make(reused, rig.fn.vkCreatePipelineCache, rig.fn.vkDestroyPipelineCache, reuse_info))) {
                        SceneOptions again;
                        again.cache = reused.get();
                        run_scene(context, rig, problems, "pipeline built from a reloaded cache", again, block, clear, 1.0f, 0);
                    }
                }
            } else {
                context.note("the pipeline cache holds " + std::to_string(size) + " bytes after one pipeline; data checks skipped");
            }
        }
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
