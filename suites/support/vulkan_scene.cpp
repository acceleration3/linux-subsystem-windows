#include "support/vulkan_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites {

namespace {

struct Push {
    float rect[4];
    float color[4];
    float depth;
    float pad[3];
};

constexpr std::uint32_t kPushSize = sizeof(Push);

}  // namespace

std::uint8_t VulkanScene::unorm8(float value) {
    const float clamped = std::min(1.0f, std::max(0.0f, value));
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

VkResult VulkanScene::create(VulkanRig& rig, const SceneOptions& options) {
    rig_ = &rig;
    options_ = options;
    const bool multisampled = options.samples != VK_SAMPLE_COUNT_1_BIT;
    VkResult result = color_.create_2d(rig, options.format, options.width, options.height,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (multisampled ? VkImageUsageFlags{0} : VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT}),
                                       options.samples);
    if (result != VK_SUCCESS) {
        return result;
    }
    result = color_.view(color_view_, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
    if (result != VK_SUCCESS) {
        return result;
    }
    if (multisampled) {
        result = resolve_.create_2d(rig, options.format, options.width, options.height,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        if (result == VK_SUCCESS) {
            result = resolve_.view(resolve_view_, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        }
        if (result != VK_SUCCESS) {
            return result;
        }
    }
    if (options.second) {
        result = second_.create_2d(rig, options.format, options.width, options.height,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (multisampled ? VkImageUsageFlags{0} : VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT}),
                                   options.samples);
        if (result == VK_SUCCESS) {
            result = second_.view(second_view_, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        }
        if (result != VK_SUCCESS) {
            return result;
        }
        if (multisampled) {
            result = second_resolve_.create_2d(rig, options.format, options.width, options.height,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            if (result == VK_SUCCESS) {
                result = second_resolve_.view(second_resolve_view_, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
            }
            if (result != VK_SUCCESS) {
                return result;
            }
        }
    }
    if (options.depth) {
        depth_format_ = VK_FORMAT_D32_SFLOAT;
        result = depth_.create_2d(rig, depth_format_, options.width, options.height,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | (multisampled ? VkImageUsageFlags{0} : VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT}),
                                  options.samples);
        if (result == VK_SUCCESS) {
            result = depth_.view(depth_view_, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT);
        }
        if (result != VK_SUCCESS) {
            return result;
        }
    }
    const VkDeviceSize color_bytes = static_cast<VkDeviceSize>(options.width) * options.height * 4;
    result = readback_color_.create(rig, color_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (result == VK_SUCCESS && options.second) {
        result = readback_second_.create(rig, color_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (result == VK_SUCCESS && options.depth && !multisampled) {
        result = readback_depth_.create(rig, color_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (result != VK_SUCCESS) {
        return result;
    }
    return build_pipeline();
}

VkResult VulkanScene::build_pipeline() {
    VulkanRig& rig = *rig_;
    const bool multisampled = options_.samples != VK_SAMPLE_COUNT_1_BIT;
    VkResult result = VK_SUCCESS;
    if (!options_.dynamic) {
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkImageView> views;
        std::vector<VkAttachmentReference> color_refs;
        std::vector<VkAttachmentReference> resolve_refs;
        VkAttachmentReference depth_ref{VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
        const auto add_color = [&](VkImageView view, VkSampleCountFlagBits samples, bool keep, VkImageLayout final_layout) {
            VkAttachmentDescription description{};
            description.format = options_.format;
            description.samples = samples;
            description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            description.storeOp = keep ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            description.finalLayout = final_layout;
            attachments.push_back(description);
            views.push_back(view);
            return static_cast<std::uint32_t>(attachments.size() - 1);
        };
        const VkImageLayout read_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        const std::uint32_t c0 = add_color(color_view_.get(), options_.samples, !multisampled,
                                           multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : read_layout);
        color_refs.push_back({c0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        if (options_.second) {
            const std::uint32_t c1 = add_color(second_view_.get(), options_.samples, !multisampled,
                                               multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : read_layout);
            color_refs.push_back({c1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        }
        if (multisampled) {
            const std::uint32_t r0 = add_color(resolve_view_.get(), VK_SAMPLE_COUNT_1_BIT, true, read_layout);
            resolve_refs.push_back({r0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            if (options_.second) {
                const std::uint32_t r1 = add_color(second_resolve_view_.get(), VK_SAMPLE_COUNT_1_BIT, true, read_layout);
                resolve_refs.push_back({r1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            }
        }
        if (options_.depth) {
            VkAttachmentDescription description{};
            description.format = depth_format_;
            description.samples = options_.samples;
            description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            description.storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
            description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            description.finalLayout = multisampled ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : read_layout;
            attachments.push_back(description);
            views.push_back(depth_view_.get());
            depth_ref = {static_cast<std::uint32_t>(attachments.size() - 1), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        }
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(color_refs.size());
        subpass.pColorAttachments = color_refs.data();
        subpass.pResolveAttachments = resolve_refs.empty() ? nullptr : resolve_refs.data();
        subpass.pDepthStencilAttachment = options_.depth ? &depth_ref : nullptr;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo pass_info{};
        pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        pass_info.pAttachments = attachments.data();
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = 2;
        pass_info.pDependencies = dependencies;
        result = rig.make(pass_, rig.fn.vkCreateRenderPass, rig.fn.vkDestroyRenderPass, pass_info);
        if (result != VK_SUCCESS) {
            return result;
        }
        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = pass_.get();
        framebuffer_info.attachmentCount = static_cast<std::uint32_t>(views.size());
        framebuffer_info.pAttachments = views.data();
        framebuffer_info.width = options_.width;
        framebuffer_info.height = options_.height;
        framebuffer_info.layers = 1;
        result = rig.make(framebuffer_, rig.fn.vkCreateFramebuffer, rig.fn.vkDestroyFramebuffer, framebuffer_info);
        if (result != VK_SUCCESS) {
            return result;
        }
    }
    push_stages_ = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (options_.kind == SceneKind::Geometry) {
        push_stages_ |= VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    const VkPushConstantRange range{push_stages_, 0, kPushSize};
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &range;
    result = rig.make(layout_, rig.fn.vkCreatePipelineLayout, rig.fn.vkDestroyPipelineLayout, layout_info);
    if (result != VK_SUCCESS) {
        return result;
    }
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::size_t module_index = 0;
    const auto add_stage = [&](VkShaderStageFlagBits stage, VulkanShader shader, const std::vector<std::uint32_t>* words = nullptr,
                               const VkSpecializationInfo* specialization = nullptr) -> VkResult {
        const VkResult made = rig.make_shader(modules_[module_index], words != nullptr ? *words : VulkanShaders::words(shader));
        if (made != VK_SUCCESS) {
            return made;
        }
        VkPipelineShaderStageCreateInfo entry{};
        entry.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        entry.stage = stage;
        entry.module = modules_[module_index].get();
        entry.pName = "main";
        entry.pSpecializationInfo = specialization;
        stages.push_back(entry);
        ++module_index;
        return VK_SUCCESS;
    };
    const bool point = options_.kind == SceneKind::Point;
    result = add_stage(VK_SHADER_STAGE_VERTEX_BIT, point ? VulkanShader::PointVert
                       : options_.kind == SceneKind::Vertex ? VulkanShader::VertexInput
                                                            : VulkanShader::RectVert);
    if (result == VK_SUCCESS && options_.kind == SceneKind::Tessellation) {
        result = add_stage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VulkanShader::TessControl);
        if (result == VK_SUCCESS) {
            result = add_stage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VulkanShader::TessEval);
        }
    }
    if (result == VK_SUCCESS && options_.kind == SceneKind::Geometry) {
        result = add_stage(VK_SHADER_STAGE_GEOMETRY_BIT, VulkanShader::RectGeom);
    }
    if (result == VK_SUCCESS) {
        if (!options_.fragment_words.empty()) {
            result = add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, VulkanShader::RectFrag, &options_.fragment_words, options_.fragment_specialization);
        } else {
            result = add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, options_.second ? VulkanShader::Rect2Frag : VulkanShader::RectFrag);
        }
    }
    if (result != VK_SUCCESS) {
        return result;
    }
    const VkVertexInputBindingDescription binding{0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (options_.kind == SceneKind::Vertex) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 1;
        vertex_input.pVertexAttributeDescriptions = &attribute;
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    switch (options_.kind) {
        case SceneKind::Rect:
        case SceneKind::Vertex:
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            break;
        case SceneKind::Geometry:
        case SceneKind::Point:
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        case SceneKind::Tessellation:
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
            break;
        case SceneKind::Line:
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            break;
    }
    VkPipelineTessellationStateCreateInfo tessellation{};
    tessellation.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellation.patchControlPoints = 4;
    const VkViewport viewport{0, 0, static_cast<float>(viewport_width()), static_cast<float>(viewport_height()), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {options_.width, options_.height}};
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = options_.depth_clamp ? VK_TRUE : VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = options_.line_width;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = options_.samples;
    VkPipelineDepthStencilStateCreateInfo depth_state{};
    depth_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.depthTestEnable = options_.depth ? VK_TRUE : VK_FALSE;
    depth_state.depthWriteEnable = options_.depth ? VK_TRUE : VK_FALSE;
    depth_state.depthCompareOp = options_.depth_op;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = options_.blend ? VK_TRUE : VK_FALSE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    const VkPipelineColorBlendAttachmentState blend_attachments[2] = {blend_attachment, blend_attachment};
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = options_.second ? 2 : 1;
    blend.pAttachments = blend_attachments;
    const VkFormat color_formats[2] = {options_.format, options_.format};
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = options_.second ? 2 : 1;
    rendering.pColorAttachmentFormats = color_formats;
    rendering.depthAttachmentFormat = options_.depth ? depth_format_ : VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = options_.dynamic ? &rendering : nullptr;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pTessellationState = options_.kind == SceneKind::Tessellation ? &tessellation : nullptr;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = options_.depth ? &depth_state : nullptr;
    info.pColorBlendState = &blend;
    info.layout = layout_.get();
    info.renderPass = options_.dynamic ? VK_NULL_HANDLE : pass_.get();
    VkPipeline created = VK_NULL_HANDLE;
    result = rig.fn.vkCreateGraphicsPipelines(rig.device, options_.cache, 1, &info, nullptr, &created);
    if (result != VK_SUCCESS) {
        return result;
    }
    pipeline_ = VulkanOwned<VkPipeline>(&rig, created, rig.fn.vkDestroyPipeline);
    (void)multisampled;
    return VK_SUCCESS;
}

void VulkanScene::draw_all(VkCommandBuffer commands, const std::vector<SceneRect>& rects) {
    rig_->fn.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    std::uint32_t vertices = 4;
    if (options_.kind == SceneKind::Geometry || options_.kind == SceneKind::Point) {
        vertices = 1;
    } else if (options_.kind == SceneKind::Line) {
        vertices = 2;
    }
    if (options_.statistics_pool != VK_NULL_HANDLE) {
        rig_->fn.vkCmdBeginQuery(commands, options_.statistics_pool, 0, 0);
    }
    std::uint32_t query = 0;
    for (const SceneRect& rect : rects) {
        if (options_.occlusion_pool != VK_NULL_HANDLE) {
            rig_->fn.vkCmdBeginQuery(commands, options_.occlusion_pool, query, options_.occlusion_flags);
        }
        Push push{};
        const float vw = static_cast<float>(viewport_width());
        const float vh = static_cast<float>(viewport_height());
        push.rect[0] = rect.x0 / vw * 2.0f - 1.0f;
        push.rect[1] = rect.y0 / vh * 2.0f - 1.0f;
        push.rect[2] = rect.x1 / vw * 2.0f - 1.0f;
        push.rect[3] = rect.y1 / vh * 2.0f - 1.0f;
        if (options_.kind == SceneKind::Point) {
            push.rect[2] = rect.x1;
        }
        std::memcpy(push.color, rect.color, sizeof(push.color));
        push.depth = rect.depth;
        rig_->fn.vkCmdPushConstants(commands, layout_.get(), push_stages_, 0, kPushSize, &push);
        if (options_.kind != SceneKind::Vertex) {
            rig_->fn.vkCmdDraw(commands, vertices, 1, 0, 0);
            if (options_.occlusion_pool != VK_NULL_HANDLE) {
                rig_->fn.vkCmdEndQuery(commands, options_.occlusion_pool, query++);
            }
            continue;
        }
        const VkDeviceSize zero = 0;
        rig_->fn.vkCmdBindVertexBuffers(commands, 0, 1, &options_.vertex_buffer, &zero);
        if (options_.draw == SceneDraw::Direct) {
            rig_->fn.vkCmdDraw(commands, 4, 1, 0, 0);
        } else if (options_.draw == SceneDraw::Indirect) {
            rig_->fn.vkCmdDrawIndirect(commands, options_.indirect_buffer, 0, 1, sizeof(VkDrawIndirectCommand));
        } else {
            rig_->fn.vkCmdBindIndexBuffer(commands, options_.index_buffer, 0, VK_INDEX_TYPE_UINT16);
            if (options_.draw == SceneDraw::Indexed) {
                rig_->fn.vkCmdDrawIndexed(commands, 4, 1, 0, 0, 0);
            } else {
                rig_->fn.vkCmdDrawIndexedIndirect(commands, options_.indirect_buffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
            }
        }
    }
    if (options_.statistics_pool != VK_NULL_HANDLE) {
        rig_->fn.vkCmdEndQuery(commands, options_.statistics_pool, 0);
    }
}

void VulkanScene::record(VkCommandBuffer commands, const std::vector<SceneRect>& rects, const float clear_color[4],
                         float clear_depth) {
    VulkanRig& rig = *rig_;
    const bool multisampled = options_.samples != VK_SAMPLE_COUNT_1_BIT;
    VkClearValue clears[6]{};
    std::uint32_t clear_count = 0;
    const auto push_color = [&]() {
        for (int i = 0; i < 4; ++i) {
            clears[clear_count].color.float32[i] = clear_color[i];
        }
        ++clear_count;
    };
    const VkRect2D area{{0, 0}, {options_.width, options_.height}};
    if (options_.occlusion_pool != VK_NULL_HANDLE) {
        rig.fn.vkCmdResetQueryPool(commands, options_.occlusion_pool, 0, options_.query_count);
    }
    if (options_.statistics_pool != VK_NULL_HANDLE) {
        rig.fn.vkCmdResetQueryPool(commands, options_.statistics_pool, 0, 1);
    }
    if (!options_.dynamic) {
        push_color();
        if (options_.second) {
            push_color();
        }
        if (multisampled) {
            clear_count += options_.second ? 2 : 1;
        }
        if (options_.depth) {
            clears[clear_count].depthStencil = {clear_depth, 0};
            ++clear_count;
        }
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = pass_.get();
        begin.framebuffer = framebuffer_.get();
        begin.renderArea = area;
        begin.clearValueCount = clear_count;
        begin.pClearValues = clears;
        rig.fn.vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
        draw_all(commands, rects);
        rig.fn.vkCmdEndRenderPass(commands);
    } else {
        std::vector<VkImageMemoryBarrier> to_attach;
        to_attach.push_back(VulkanImage::barrier(color_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT));
        if (multisampled) {
            to_attach.push_back(VulkanImage::barrier(resolve_.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                                                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT));
        }
        if (options_.second) {
            to_attach.push_back(VulkanImage::barrier(second_.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                                                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT));
            if (multisampled) {
                to_attach.push_back(VulkanImage::barrier(second_resolve_.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                                                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT));
            }
        }
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                    0, nullptr, 0, nullptr, static_cast<std::uint32_t>(to_attach.size()), to_attach.data());
        if (options_.depth) {
            const VkImageMemoryBarrier depth_barrier = VulkanImage::barrier(
                depth_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
            rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                                        0, nullptr, 0, nullptr, 1, &depth_barrier);
        }
        VkRenderingAttachmentInfo colors[2]{};
        const VkImageView sources[2] = {color_view_.get(), second_view_.get()};
        const VkImageView resolves[2] = {resolve_view_.get(), second_resolve_view_.get()};
        const std::uint32_t count = options_.second ? 2 : 1;
        for (std::uint32_t i = 0; i < count; ++i) {
            colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colors[i].imageView = sources[i];
            colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            if (multisampled) {
                colors[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                colors[i].resolveImageView = resolves[i];
                colors[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colors[i].storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
            for (int c = 0; c < 4; ++c) {
                colors[i].clearValue.color.float32[c] = clear_color[c];
            }
        }
        VkRenderingAttachmentInfo depth_info{};
        depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_info.imageView = depth_view_.get();
        depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_info.storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
        depth_info.clearValue.depthStencil = {clear_depth, 0};
        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea = area;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = count;
        rendering.pColorAttachments = colors;
        rendering.pDepthAttachment = options_.depth ? &depth_info : nullptr;
        rig.fn.vkCmdBeginRendering(commands, &rendering);
        draw_all(commands, rects);
        rig.fn.vkCmdEndRendering(commands);
        std::vector<VkImageMemoryBarrier> to_read;
        const VulkanImage& first = multisampled ? resolve_ : color_;
        to_read.push_back(VulkanImage::barrier(first.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                               VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT));
        if (options_.second) {
            const VulkanImage& other = multisampled ? second_resolve_ : second_;
            to_read.push_back(VulkanImage::barrier(other.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                   VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT));
        }
        if (options_.depth && !multisampled) {
            to_read.push_back(VulkanImage::barrier(depth_.image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                   VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT));
        }
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                    static_cast<std::uint32_t>(to_read.size()), to_read.data());
    }
    const VkBufferImageCopy region{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {options_.width, options_.height, 1}};
    rig.fn.vkCmdCopyImageToBuffer(commands, (multisampled ? resolve_ : color_).image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  readback_color_.buffer, 1, &region);
    if (options_.second) {
        rig.fn.vkCmdCopyImageToBuffer(commands, (multisampled ? second_resolve_ : second_).image,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_second_.buffer, 1, &region);
    }
    if (options_.depth && !multisampled) {
        const VkBufferImageCopy depth_region{0, 0, 0, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1}, {0, 0, 0}, {options_.width, options_.height, 1}};
        rig.fn.vkCmdCopyImageToBuffer(commands, depth_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_depth_.buffer, 1,
                                      &depth_region);
    }
    VkMemoryBarrier host{};
    host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0,
                                nullptr);
}

VkResult VulkanScene::render(const std::vector<SceneRect>& rects, const float clear_color[4], float clear_depth,
                             SceneResult& result) {
    VulkanRig& rig = *rig_;
    VkCommandBuffer secondary = nullptr;
    if (options_.secondary && !options_.dynamic) {
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = rig.main().pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocate.commandBufferCount = 1;
        VkResult made = rig.fn.vkAllocateCommandBuffers(rig.device, &allocate, &secondary);
        if (made != VK_SUCCESS) {
            return made;
        }
        VkCommandBufferInheritanceInfo inherit{};
        inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inherit.renderPass = pass_.get();
        inherit.framebuffer = framebuffer_.get();
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        begin.pInheritanceInfo = &inherit;
        made = rig.fn.vkBeginCommandBuffer(secondary, &begin);
        if (made == VK_SUCCESS) {
            draw_all(secondary, rects);
            made = rig.fn.vkEndCommandBuffer(secondary);
        }
        if (made != VK_SUCCESS) {
            rig.fn.vkFreeCommandBuffers(rig.device, rig.main().pool, 1, &secondary);
            return made;
        }
    }
    const VkResult ran = rig.run([&](VkCommandBuffer commands) {
        if (secondary == nullptr) {
            record(commands, rects, clear_color, clear_depth);
            return;
        }
        VkClearValue clears[6]{};
        std::uint32_t clear_count = options_.second ? 2 : 1;
        for (std::uint32_t i = 0; i < clear_count; ++i) {
            for (int c = 0; c < 4; ++c) {
                clears[i].color.float32[c] = clear_color[c];
            }
        }
        if (options_.samples != VK_SAMPLE_COUNT_1_BIT) {
            clear_count += options_.second ? 2 : 1;
        }
        if (options_.depth) {
            clears[clear_count].depthStencil = {clear_depth, 0};
            ++clear_count;
        }
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = pass_.get();
        begin.framebuffer = framebuffer_.get();
        begin.renderArea = {{0, 0}, {options_.width, options_.height}};
        begin.clearValueCount = clear_count;
        begin.pClearValues = clears;
        rig.fn.vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        rig.fn.vkCmdExecuteCommands(commands, 1, &secondary);
        rig.fn.vkCmdEndRenderPass(commands);
        const bool multisampled = options_.samples != VK_SAMPLE_COUNT_1_BIT;
        const VkBufferImageCopy region{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {options_.width, options_.height, 1}};
        rig.fn.vkCmdCopyImageToBuffer(commands, (multisampled ? resolve_ : color_).image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                      readback_color_.buffer, 1, &region);
        if (options_.second) {
            rig.fn.vkCmdCopyImageToBuffer(commands, (multisampled ? second_resolve_ : second_).image,
                                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_second_.buffer, 1, &region);
        }
        if (options_.depth && !multisampled) {
            const VkBufferImageCopy depth_region{0, 0, 0, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1}, {0, 0, 0}, {options_.width, options_.height, 1}};
            rig.fn.vkCmdCopyImageToBuffer(commands, depth_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_depth_.buffer, 1,
                                          &depth_region);
        }
        VkMemoryBarrier host{};
        host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0,
                                    nullptr);
    });
    if (secondary != nullptr && ran == VK_SUCCESS) {
        rig.fn.vkFreeCommandBuffers(rig.device, rig.main().pool, 1, &secondary);
    }
    if (ran != VK_SUCCESS) {
        return ran;
    }
    const std::size_t bytes = static_cast<std::size_t>(options_.width) * options_.height * 4;
    readback_color_.acquire();
    result.color.assign(readback_color_.bytes(), readback_color_.bytes() + bytes);
    if (options_.second) {
        readback_second_.acquire();
        result.second.assign(readback_second_.bytes(), readback_second_.bytes() + bytes);
    }
    if (options_.depth && options_.samples == VK_SAMPLE_COUNT_1_BIT) {
        readback_depth_.acquire();
        result.depth.resize(static_cast<std::size_t>(options_.width) * options_.height);
        std::memcpy(result.depth.data(), readback_depth_.bytes(), result.depth.size() * sizeof(float));
    }
    return VK_SUCCESS;
}

std::vector<std::uint8_t> VulkanScene::expected_color(const std::vector<SceneRect>& rects, const float clear_color[4],
                                                      float clear_depth, bool second_attachment) const {
    const std::size_t pixels = static_cast<std::size_t>(options_.width) * options_.height;
    std::vector<float> color(pixels * 4);
    std::vector<float> depth(pixels, clear_depth);
    for (std::size_t p = 0; p < pixels; ++p) {
        for (std::size_t c = 0; c < 4; ++c) {
            color[p * 4 + c] = static_cast<float>(unorm8(clear_color[c])) / 255.0f;
        }
    }
    for (const SceneRect& rect : rects) {
        float z = rect.depth;
        if (z < 0.0f || z > 1.0f) {
            if (!options_.depth_clamp) {
                continue;
            }
            z = std::min(1.0f, std::max(0.0f, z));
        }
        for (std::uint32_t y = 0; y < options_.height; ++y) {
            for (std::uint32_t x = 0; x < options_.width; ++x) {
                const float cx = static_cast<float>(x) + 0.5f;
                const float cy = static_cast<float>(y) + 0.5f;
                if (cx < rect.x0 || cx >= rect.x1 || cy < rect.y0 || cy >= rect.y1) {
                    continue;
                }
                const std::size_t p = static_cast<std::size_t>(y) * options_.width + x;
                if (options_.depth) {
                    bool pass = true;
                    switch (options_.depth_op) {
                        case VK_COMPARE_OP_LESS:
                            pass = z < depth[p];
                            break;
                        case VK_COMPARE_OP_LESS_OR_EQUAL:
                            pass = z <= depth[p];
                            break;
                        case VK_COMPARE_OP_GREATER:
                            pass = z > depth[p];
                            break;
                        default:
                            pass = true;
                            break;
                    }
                    if (!pass) {
                        continue;
                    }
                    depth[p] = z;
                }
                for (int c = 0; c < 4; ++c) {
                    float source = second_attachment ? 1.0f - rect.color[c] : rect.color[c];
                    const float alpha = second_attachment ? 1.0f - rect.color[3] : rect.color[3];
                    source = static_cast<float>(unorm8(source)) / 255.0f;
                    float& target = color[p * 4 + static_cast<std::size_t>(c)];
                    target = options_.blend ? source * alpha + target * (1.0f - alpha) : source;
                }
            }
        }
    }
    std::vector<std::uint8_t> out(pixels * 4);
    for (std::size_t p = 0; p < pixels; ++p) {
        for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t slot = bgra() && c < 3 ? 2 - c : c;
            out[p * 4 + slot] = unorm8(color[p * 4 + c]);
        }
    }
    return out;
}

std::vector<float> VulkanScene::expected_depth(const std::vector<SceneRect>& rects, float clear_depth) const {
    const std::size_t pixels = static_cast<std::size_t>(options_.width) * options_.height;
    std::vector<float> depth(pixels, clear_depth);
    for (const SceneRect& rect : rects) {
        float z = rect.depth;
        if (z < 0.0f || z > 1.0f) {
            if (!options_.depth_clamp) {
                continue;
            }
            z = std::min(1.0f, std::max(0.0f, z));
        }
        for (std::uint32_t y = 0; y < options_.height; ++y) {
            for (std::uint32_t x = 0; x < options_.width; ++x) {
                const float cx = static_cast<float>(x) + 0.5f;
                const float cy = static_cast<float>(y) + 0.5f;
                if (cx < rect.x0 || cx >= rect.x1 || cy < rect.y0 || cy >= rect.y1) {
                    continue;
                }
                float& stored = depth[static_cast<std::size_t>(y) * options_.width + x];
                const bool pass = options_.depth_op == VK_COMPARE_OP_LESS ? z < stored
                                  : options_.depth_op == VK_COMPARE_OP_LESS_OR_EQUAL ? z <= stored
                                  : options_.depth_op == VK_COMPARE_OP_GREATER ? z > stored
                                                                                : true;
                if (pass) {
                    stored = z;
                }
            }
        }
    }
    return depth;
}

}  // namespace lsw::suites
