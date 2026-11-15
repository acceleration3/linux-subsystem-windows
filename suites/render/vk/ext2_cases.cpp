#include "render/vk/quadkit.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};

CaseSpec spec_of(const std::string& group, const std::string& name) {
    CaseSpec spec(group, name);
    spec.tolerance(Tolerance::exact());
    return spec;
}

std::vector<Vertex2> big_quad() {
    return {vertex(-0.95f, -0.95f, 0), vertex(0.95f, -0.95f, 1), vertex(-0.95f, 0.95f, 2),
            vertex(0.95f, -0.95f, 1),  vertex(0.95f, 0.95f, 3),  vertex(-0.95f, 0.95f, 2)};
}

std::string shading_rate_feature(Kit& kit, int which) {
    const auto f = kit.query_features<VkPhysicalDeviceFragmentShadingRateFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR);
    if (which == 0) {
        return f.primitiveFragmentShadingRate != VK_FALSE ? std::string() : std::string("primitiveFragmentShadingRate");
    }
    return f.attachmentFragmentShadingRate != VK_FALSE ? std::string() : std::string("attachmentFragmentShadingRate");
}

void register_primitive_rate() {
    CaseSpec spec = spec_of("stages", "shading_rate_primitive");
    spec.extension("VK_KHR_fragment_shading_rate").min_version(1, 2);
    Cases::add(
        std::move(spec),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            const Buffer vb = rig.buffer(big_quad(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const VkPipelineLayout layout = rig.playout({}, 4, VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineFragmentShadingRateStateCreateInfoKHR rate{};
            rate.sType = VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR;
            rate.fragmentSize = {1, 1};
            rate.combinerOps[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR;
            rate.combinerOps[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
            PipelineDesc pipe = rig.pos2c(target, layout);
            pipe.vertex = rig.sh("prim_rate.v12.vert");
            pipe.fragment = rig.sh("shadingrate.v12.frag");
            pipe.next = &rate;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                const std::uint32_t rates[4] = {0, 1, 4, 5};
                for (unsigned i = 0; i < 4; ++i) {
                    rig.viewport(cb, static_cast<float>(i % 2) * 64.0f, static_cast<float>(i / 2) * 64.0f, 64, 64);
                    rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, &rates[i], 4);
                    rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                }
            }, layout));
        },
        [](Kit& kit) -> std::string { return shading_rate_feature(kit, 0); });
}

void register_attachment_rate() {
    CaseSpec spec = spec_of("stages", "shading_rate_attachment");
    spec.extension("VK_KHR_fragment_shading_rate").min_version(1, 3);
    Cases::add(
        std::move(spec),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            const auto props = kit.query_properties<VkPhysicalDeviceFragmentShadingRatePropertiesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR);
            const VkExtent2D texel = props.minFragmentShadingRateAttachmentTexelSize;
            const std::uint32_t rw = (128 + texel.width - 1) / texel.width;
            const std::uint32_t rh = (128 + texel.height - 1) / texel.height;
            TextureDesc rate_desc;
            rate_desc.width = rw;
            rate_desc.height = rh;
            rate_desc.format = VK_FORMAT_R8_UINT;
            rate_desc.usage = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            const Texture rate_image = rig.texture(rate_desc);
            std::vector<std::uint8_t> rates(static_cast<std::size_t>(rw) * rh);
            const std::uint8_t values[4] = {0, 1, 4, 5};
            for (std::uint32_t y = 0; y < rh; ++y) {
                for (std::uint32_t x = 0; x < rw; ++x) {
                    rates[y * rw + x] = values[((x * 4) / rw + (y * 4) / rh) % 4];
                }
            }
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {rw, rh, 1};
            std::string error;
            rig.must(kit.upload_texture(rate_image, {region}, rates, VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR, error), error);
            TextureDesc color_desc;
            color_desc.width = color_desc.height = 128;
            color_desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            const Texture color = rig.texture(color_desc);
            const VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
            VkPipelineFragmentShadingRateStateCreateInfoKHR rate{};
            rate.sType = VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR;
            rate.fragmentSize = {1, 1};
            rate.combinerOps[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
            rate.combinerOps[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR;
            VkPipelineRenderingCreateInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering.pNext = &rate;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachmentFormats = &color_format;
            PipelineDesc pipe = rig.pos2c(rig.make_target(TargetDesc{}));
            pipe.pass = VK_NULL_HANDLE;
            pipe.fragment = rig.sh("shadingrate.v12.frag");
            pipe.next = &rendering;
            pipe.flags = VK_PIPELINE_CREATE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
            const VkPipeline pipeline = rig.pipe(pipe);
            const Buffer vb = rig.buffer(big_quad(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            rig.run([&](VkCommandBuffer cb) {
                kit.barrier(cb, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                VkRenderingAttachmentInfo ca{};
                ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                ca.imageView = color.view;
                ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                std::copy(kClear, kClear + 4, ca.clearValue.color.float32);
                VkRenderingFragmentShadingRateAttachmentInfoKHR rate_info{};
                rate_info.sType = VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR;
                rate_info.imageView = rate_image.view;
                rate_info.imageLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
                rate_info.shadingRateAttachmentTexelSize = texel;
                VkRenderingInfo info{};
                info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                info.pNext = &rate_info;
                info.renderArea = {{0, 0}, {128, 128}};
                info.layerCount = 1;
                info.colorAttachmentCount = 1;
                info.pColorAttachments = &ca;
                kit.api.vkCmdBeginRendering(cb, &info);
                rig.bind_graphics(cb, pipeline);
                rig.bind_vertex(cb, vb);
                rig.viewport(cb, 0, 0, 128, 128);
                rig.scissor(cb, 0, 0, 128, 128);
                kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                kit.api.vkCmdEndRendering(cb);
            });
            out.add("", rig.read(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
        },
        [](Kit& kit) -> std::string {
            const std::string feature = shading_rate_feature(kit, 1);
            if (!feature.empty()) {
                return feature;
            }
            return Cases::need_format(kit, VK_FORMAT_R8_UINT, VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR, "R8 uint shading rate attachment");
        });
}

struct ResolveMode {
    const char* name;
    VkResolveModeFlagBits depth;
    VkResolveModeFlagBits stencil;
};

void register_depth_stencil_resolve() {
    const std::vector<ResolveMode> modes = {{"sample_zero", VK_RESOLVE_MODE_SAMPLE_ZERO_BIT, VK_RESOLVE_MODE_SAMPLE_ZERO_BIT},
                                            {"min", VK_RESOLVE_MODE_MIN_BIT, VK_RESOLVE_MODE_MIN_BIT},
                                            {"max", VK_RESOLVE_MODE_MAX_BIT, VK_RESOLVE_MODE_MAX_BIT},
                                            {"average_stencil_none", VK_RESOLVE_MODE_AVERAGE_BIT, VK_RESOLVE_MODE_NONE}};
    for (const ResolveMode& mode : modes) {
        CaseSpec spec = spec_of("msaa", std::string("depth_stencil_resolve_") + mode.name);
        spec.min_version(1, 3);
        Cases::add(
            std::move(spec),
            [mode](Rig& rig, CaptureSet& out) {
                Kit& kit = rig.kit;
                TextureDesc ms_desc;
                ms_desc.width = ms_desc.height = 128;
                ms_desc.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
                ms_desc.samples = VK_SAMPLE_COUNT_4_BIT;
                ms_desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                ms_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                const Texture ms = rig.texture(ms_desc);
                TextureDesc res_desc = ms_desc;
                res_desc.samples = VK_SAMPLE_COUNT_1_BIT;
                res_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                const Texture resolved = rig.texture(res_desc);
                TargetDesc td;
                td.samples = VK_SAMPLE_COUNT_4_BIT;
                QuadKit quads(rig);
                PipelineDesc pipe = quads.desc(rig.make_target(td));
                const VkFormat none = VK_FORMAT_UNDEFINED;
                VkPipelineRenderingCreateInfo rendering{};
                rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
                rendering.stencilAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
                (void)none;
                pipe.pass = VK_NULL_HANDLE;
                pipe.fragment = VK_NULL_HANDLE;
                pipe.color_attachments = 0;
                pipe.next = &rendering;
                pipe.depth_test = true;
                pipe.stencil_test = true;
                pipe.stencil = VkStencilOpState{VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 0xff, 0xff, 0};
                pipe.dynamic = {VK_DYNAMIC_STATE_STENCIL_REFERENCE};
                const VkPipeline pipeline = rig.pipe(pipe);
                rig.run([&](VkCommandBuffer cb) {
                    const VkImageAspectFlags both = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                    kit.barrier(cb, ms.image, both, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                    kit.barrier(cb, resolved.image, both, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                    VkRenderingAttachmentInfo da{};
                    da.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    da.imageView = ms.view;
                    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    da.resolveMode = mode.depth;
                    da.resolveImageView = resolved.view;
                    da.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    da.clearValue.depthStencil = {1.0f, 0};
                    VkRenderingAttachmentInfo sa = da;
                    sa.resolveMode = mode.stencil;
                    VkRenderingInfo info{};
                    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    info.renderArea = {{0, 0}, {128, 128}};
                    info.layerCount = 1;
                    info.pDepthAttachment = &da;
                    info.pStencilAttachment = &sa;
                    kit.api.vkCmdBeginRendering(cb, &info);
                    rig.bind_graphics(cb, pipeline);
                    rig.viewport(cb, 0, 0, 128, 128);
                    rig.scissor(cb, 0, 0, 128, 128);
                    kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0x10);
                    quads.quad(cb, -0.8f, -0.8f, 1.3f, 1.2f, palette(0), 0.35f, 0.3f);
                    kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0x23);
                    quads.quad(cb, -0.25f, -0.35f, 1.1f, 1.15f, palette(1), 0.7f, -0.35f);
                    kit.api.vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0x37);
                    quads.quad(cb, -0.6f, 0.2f, 0.9f, 0.6f, palette(2), 0.3f, 0.15f);
                    kit.api.vkCmdEndRendering(cb);
                });
                out.add("depth", rig.read(resolved, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT));
                if (mode.stencil != VK_RESOLVE_MODE_NONE) {
                    out.add("stencil", rig.read(resolved, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_STENCIL_BIT));
                }
            },
            [mode](Kit& kit) -> std::string {
                const auto p = kit.query_properties<VkPhysicalDeviceDepthStencilResolveProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES);
                if ((p.supportedDepthResolveModes & mode.depth) == 0) {
                    return "depth resolve mode";
                }
                if (mode.stencil == VK_RESOLVE_MODE_NONE) {
                    if (p.independentResolveNone == VK_FALSE) {
                        return "independentResolveNone";
                    }
                } else if ((p.supportedStencilResolveModes & mode.stencil) == 0) {
                    return "stencil resolve mode";
                }
                if ((kit.properties.limits.framebufferDepthSampleCounts & VK_SAMPLE_COUNT_4_BIT) == 0 ||
                    (kit.properties.limits.framebufferStencilSampleCounts & VK_SAMPLE_COUNT_4_BIT) == 0) {
                    return "4x depth and stencil samples";
                }
                return Cases::need_format(kit, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "D32S8");
            });
    }
}

const int registered = (register_primitive_rate(), register_attachment_rate(), register_depth_stencil_resolve(), 0);

}  // namespace

}  // namespace lsw::render::vk
