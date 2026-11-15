#include "render/vk/target.hpp"

namespace lsw::render::vk {

bool Target::make_pass(Kit& kit, bool clear, VkRenderPass& out, std::string& error) const {
    const bool multisampled = desc.samples != VK_SAMPLE_COUNT_1_BIT;
    const bool has_depth = desc.depth != VK_FORMAT_UNDEFINED;
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;
    std::vector<VkAttachmentReference> resolve_refs;
    for (VkFormat format : desc.colors) {
        VkAttachmentDescription a{};
        a.format = format;
        a.samples = desc.samples;
        a.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        color_refs.push_back({static_cast<std::uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        attachments.push_back(a);
    }
    VkAttachmentReference depth_ref{};
    if (has_depth) {
        VkAttachmentDescription a{};
        a.format = desc.depth;
        a.samples = desc.samples;
        a.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        depth_ref = {static_cast<std::uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        attachments.push_back(a);
    }
    if (multisampled) {
        for (VkFormat format : desc.colors) {
            VkAttachmentDescription a{};
            a.format = format;
            a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            resolve_refs.push_back({static_cast<std::uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            attachments.push_back(a);
        }
    }
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<std::uint32_t>(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    subpass.pResolveAttachments = multisampled ? resolve_refs.data() : nullptr;
    subpass.pDepthStencilAttachment = has_depth ? &depth_ref : nullptr;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    if (!kit.check(kit.api.vkCreateRenderPass(kit.device, &info, nullptr, &out), "vkCreateRenderPass", error)) {
        return false;
    }
    const VkRenderPass pass = out;
    kit.defer([&kit, pass] { kit.api.vkDestroyRenderPass(kit.device, pass, nullptr); });
    return true;
}

bool Target::create(Kit& kit, const TargetDesc& wanted, std::string& error) {
    desc = wanted;
    const bool multisampled = desc.samples != VK_SAMPLE_COUNT_1_BIT;
    std::vector<VkImageView> views;
    for (VkFormat format : desc.colors) {
        TextureDesc t;
        t.width = desc.width;
        t.height = desc.height;
        t.format = format;
        t.samples = desc.samples;
        t.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  desc.color_usage | (multisampled ? 0 : VK_IMAGE_USAGE_SAMPLED_BIT);
        Texture texture;
        if (!kit.make_texture(texture, t, error)) {
            return false;
        }
        color_.push_back(texture);
        views.push_back(texture.view);
    }
    if (desc.depth != VK_FORMAT_UNDEFINED) {
        TextureDesc t;
        t.width = desc.width;
        t.height = desc.height;
        t.format = desc.depth;
        t.samples = desc.samples;
        t.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        t.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (desc.depth == VK_FORMAT_D24_UNORM_S8_UINT || desc.depth == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            t.aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        if (!kit.make_texture(depth_, t, error)) {
            return false;
        }
        views.push_back(depth_.view);
    }
    if (multisampled) {
        for (VkFormat format : desc.colors) {
            TextureDesc t;
            t.width = desc.width;
            t.height = desc.height;
            t.format = format;
            t.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            Texture texture;
            if (!kit.make_texture(texture, t, error)) {
                return false;
            }
            resolved_.push_back(texture);
            views.push_back(texture.view);
        }
    }
    if (!make_pass(kit, true, clear_pass, error) || !make_pass(kit, false, load_pass, error)) {
        return false;
    }
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = clear_pass;
    info.attachmentCount = static_cast<std::uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = desc.width;
    info.height = desc.height;
    info.layers = 1;
    if (!kit.check(kit.api.vkCreateFramebuffer(kit.device, &info, nullptr, &framebuffer), "vkCreateFramebuffer", error)) {
        return false;
    }
    const VkFramebuffer fb = framebuffer;
    kit.defer([&kit, fb] { kit.api.vkDestroyFramebuffer(kit.device, fb, nullptr); });
    return true;
}

void Target::begin(Kit& kit, VkCommandBuffer cb, bool clear) const {
    std::vector<VkClearValue> clears;
    for (std::size_t i = 0; i < desc.colors.size(); ++i) {
        VkClearValue v{};
        for (int c = 0; c < 4; ++c) {
            v.color.float32[c] = clear_color[c];
        }
        clears.push_back(v);
    }
    if (desc.depth != VK_FORMAT_UNDEFINED) {
        VkClearValue v{};
        v.depthStencil = {clear_depth, clear_stencil};
        clears.push_back(v);
    }
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = clear ? clear_pass : load_pass;
    info.framebuffer = framebuffer;
    info.renderArea = {{0, 0}, {desc.width, desc.height}};
    info.clearValueCount = static_cast<std::uint32_t>(clears.size());
    info.pClearValues = clears.data();
    kit.api.vkCmdBeginRenderPass(cb, &info, VK_SUBPASS_CONTENTS_INLINE);
    set_viewport(kit, cb);
}

void Target::set_viewport(Kit& kit, VkCommandBuffer cb) const {
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(desc.width), static_cast<float>(desc.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {desc.width, desc.height}};
    kit.api.vkCmdSetViewport(cb, 0, 1, &viewport);
    kit.api.vkCmdSetScissor(cb, 0, 1, &scissor);
}

void Target::end(Kit& kit, VkCommandBuffer cb) const { kit.api.vkCmdEndRenderPass(cb); }

bool Target::read_color(Kit& kit, std::size_t index, Image& out, std::string& error) const {
    return kit.read_texture(color(index), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 0, out, error);
}

bool Target::read_depth(Kit& kit, Image& out, std::string& error) const {
    return kit.read_texture(depth_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 0, out, error);
}

bool Target::read_stencil(Kit& kit, Image& out, std::string& error) const {
    return kit.read_texture(depth_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_STENCIL_BIT, 0, out, error);
}

}  // namespace lsw::render::vk
