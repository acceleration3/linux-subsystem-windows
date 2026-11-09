#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/vk/presenter.hpp"
#include "render/vk/target.hpp"
#include "render/vk/vk.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

constexpr unsigned kRingSlots = 4;
constexpr unsigned kStampVertices = 12;

void stamp(ColoredVertex* out, unsigned frame) {
    const float angle = static_cast<float>(frame) * 0.7854f;
    const float cx = 0.6f * std::cos(angle);
    const float cy = 0.6f * std::sin(angle);
    for (unsigned t = 0; t < 4; ++t) {
        const float a0 = angle + static_cast<float>(t) * 1.5708f;
        const float r = 0.16f + 0.01f * static_cast<float>(frame);
        ColoredVertex* v = out + t * 3;
        v[0] = {{cx, cy, 0.0f}, {1.0f, static_cast<float>(frame) / 7.0f, 0.2f}};
        v[1] = {{cx + r * std::cos(a0), cy + r * std::sin(a0), 0.0f}, {0.2f, 1.0f, static_cast<float>(frame) / 7.0f}};
        v[2] = {{cx + r * std::cos(a0 + 1.5708f), cy + r * std::sin(a0 + 1.5708f), 0.0f}, {static_cast<float>(frame) / 7.0f, 0.3f, 1.0f}};
    }
}

class RingAccumulate : public VkScene {
public:
    bool setup(Kit& kit, std::string& error) override {
        if (!target_.create(kit, TargetDesc{}, error)) {
            return false;
        }
        target_.clear_color[0] = 0.05f;
        target_.clear_color[1] = 0.05f;
        target_.clear_color[2] = 0.08f;
        slot_bytes_ = kStampVertices * sizeof(ColoredVertex);
        if (!kit.make_buffer(ring_, slot_bytes_ * kRingSlots, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, error)) {
            return false;
        }
        layout_ = VkCases::push_layout(kit, 64, VK_SHADER_STAGE_VERTEX_BIT, {}, error);
        PipelineDesc pipe;
        pipe.vertex = kit.shader("mesh.vert", error);
        pipe.fragment = kit.shader("mesh.frag", error);
        if (layout_ == VK_NULL_HANDLE || pipe.vertex == VK_NULL_HANDLE || pipe.fragment == VK_NULL_HANDLE) {
            return false;
        }
        pipe.bindings = {{0, sizeof(ColoredVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
        pipe.layout = layout_;
        pipe.pass = target_.clear_pass;
        return kit.make_pipeline(pipeline_, pipe, error);
    }

    bool frame(Kit& kit, unsigned index, std::string& error) override {
        const unsigned slot = index % kRingSlots;
        ColoredVertex vertices[kStampVertices];
        stamp(vertices, index);
        if (!kit.write(ring_, slot * slot_bytes_, vertices, slot_bytes_, error)) {
            return false;
        }
        const Mat4 transform = Mat4::rotate_z(0.05f * static_cast<float>(index));
        return kit.submit(
            [&](VkCommandBuffer cb) {
                target_.begin(kit, cb, index == 0);
                kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
                const VkDeviceSize offset = slot * slot_bytes_;
                kit.api.vkCmdBindVertexBuffers(cb, 0, 1, &ring_.buffer, &offset);
                kit.api.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, transform.m);
                kit.api.vkCmdDraw(cb, kStampVertices, 1, 0, 0);
                target_.end(kit, cb);
            },
            error);
    }

    bool capture(Kit& kit, unsigned, CaptureSet& out, std::string& error) override {
        Image color;
        if (!target_.read_color(kit, 0, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }

private:
    Target target_;
    Buffer ring_;
    VkDeviceSize slot_bytes_ = 0;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

const VkRegister<RingAccumulate> kRingAccumulate{CaseSpec("frames", "ring_accumulate").frames(8, {0, 3, 7}).tolerance(Tolerance::tight(1))};

class SwapchainCube : public VkScene {
public:
    bool setup(Kit& kit, std::string& error) override {
        Presenter& presenter = *kit.presenter;
        if (!presenter.open(kit, 128, 128, error)) {
            return false;
        }
        const VkFormat format = presenter.format();
        VkAttachmentDescription color{};
        color.format = format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentDescription depth{};
        depth.format = VK_FORMAT_D32_SFLOAT;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const VkAttachmentDescription attachments[2] = {color, depth};
        const VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass_info{};
        pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass_info.attachmentCount = 2;
        pass_info.pAttachments = attachments;
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = 1;
        pass_info.pDependencies = &dependency;
        if (!kit.check(kit.api.vkCreateRenderPass(kit.device, &pass_info, nullptr, &pass_), "vkCreateRenderPass", error)) {
            return false;
        }
        const VkRenderPass pass = pass_;
        kit.defer([&kit, pass] { kit.api.vkDestroyRenderPass(kit.device, pass, nullptr); });
        TextureDesc depth_desc;
        depth_desc.width = 128;
        depth_desc.height = 128;
        depth_desc.format = VK_FORMAT_D32_SFLOAT;
        depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (!kit.make_texture(depth_, depth_desc, error)) {
            return false;
        }
        for (std::uint32_t i = 0; i < presenter.image_count(); ++i) {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = presenter.image(i);
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format;
            view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView view = VK_NULL_HANDLE;
            if (!kit.check(kit.api.vkCreateImageView(kit.device, &view_info, nullptr, &view), "vkCreateImageView", error)) {
                return false;
            }
            kit.defer([&kit, view] { kit.api.vkDestroyImageView(kit.device, view, nullptr); });
            const VkImageView views[2] = {view, depth_.view};
            VkFramebufferCreateInfo fb_info{};
            fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb_info.renderPass = pass_;
            fb_info.attachmentCount = 2;
            fb_info.pAttachments = views;
            fb_info.width = 128;
            fb_info.height = 128;
            fb_info.layers = 1;
            VkFramebuffer fb = VK_NULL_HANDLE;
            if (!kit.check(kit.api.vkCreateFramebuffer(kit.device, &fb_info, nullptr, &fb), "vkCreateFramebuffer", error)) {
                return false;
            }
            kit.defer([&kit, fb] { kit.api.vkDestroyFramebuffer(kit.device, fb, nullptr); });
            framebuffers_.push_back(fb);
        }
        const std::vector<ColoredVertex> vertices = Mesh::cube_vertices();
        const std::vector<std::uint16_t> indices = Mesh::cube_indices();
        index_count_ = static_cast<std::uint32_t>(indices.size());
        if (!kit.make_buffer_with(vertex_, vertices.data(), vertices.size() * sizeof(ColoredVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, error) ||
            !kit.make_buffer_with(index_, indices.data(), indices.size() * sizeof(std::uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, error) ||
            !kit.make_buffer(readback_, 128 * 128 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, error)) {
            return false;
        }
        layout_ = VkCases::push_layout(kit, 64, VK_SHADER_STAGE_VERTEX_BIT, {}, error);
        PipelineDesc pipe;
        pipe.vertex = kit.shader("mesh.vert", error);
        pipe.fragment = kit.shader("mesh.frag", error);
        if (layout_ == VK_NULL_HANDLE || pipe.vertex == VK_NULL_HANDLE || pipe.fragment == VK_NULL_HANDLE) {
            return false;
        }
        pipe.bindings = {{0, sizeof(ColoredVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
        pipe.depth_test = true;
        pipe.cull = VK_CULL_MODE_BACK_BIT;
        pipe.layout = layout_;
        pipe.pass = pass_;
        return kit.make_pipeline(pipeline_, pipe, error) && kit.make_semaphore(acquired_, error) && kit.make_semaphore(rendered_, error);
    }

    bool frame(Kit& kit, unsigned index, std::string& error) override {
        Presenter& presenter = *kit.presenter;
        std::uint32_t image = 0;
        if (!presenter.acquire(kit, image, acquired_, error)) {
            return false;
        }
        const Mat4 transform = Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, true) * Mat4::translate(0, 0, -5.0f) *
                               Mat4::rotate_y(0.5f * static_cast<float>(index)) * Mat4::rotate_x(0.4f);
        const VkImage target = presenter.image(image);
        const bool ok = kit.submit_sync(
            [&](VkCommandBuffer cb) {
                VkClearValue clears[2]{};
                clears[0].color = {{0.1f, 0.12f, 0.2f, 1.0f}};
                clears[1].depthStencil = {1.0f, 0};
                VkRenderPassBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                begin.renderPass = pass_;
                begin.framebuffer = framebuffers_[image];
                begin.renderArea = {{0, 0}, {128, 128}};
                begin.clearValueCount = 2;
                begin.pClearValues = clears;
                kit.api.vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
                const VkViewport viewport{0, 0, 128, 128, 0, 1};
                const VkRect2D scissor{{0, 0}, {128, 128}};
                kit.api.vkCmdSetViewport(cb, 0, 1, &viewport);
                kit.api.vkCmdSetScissor(cb, 0, 1, &scissor);
                kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
                const VkDeviceSize offset = 0;
                kit.api.vkCmdBindVertexBuffers(cb, 0, 1, &vertex_.buffer, &offset);
                kit.api.vkCmdBindIndexBuffer(cb, index_.buffer, 0, VK_INDEX_TYPE_UINT16);
                kit.api.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, transform.m);
                kit.api.vkCmdDrawIndexed(cb, index_count_, 1, 0, 0, 0);
                kit.api.vkCmdEndRenderPass(cb);
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {128, 128, 1};
                kit.api.vkCmdCopyImageToBuffer(cb, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buffer, 1, &region);
                kit.barrier(cb, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, presenter.present_layout());
            },
            acquired_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, rendered_, error);
        if (!ok) {
            return false;
        }
        last_ = Image(128, 128, Kit::pixel_kind(presenter.format(), VK_IMAGE_ASPECT_COLOR_BIT));
        std::memcpy(last_.bytes().data(), readback_.mapped, last_.bytes().size());
        return presenter.present(kit, image, rendered_, error);
    }

    bool capture(Kit&, unsigned, CaptureSet& out, std::string&) override {
        out.add("", last_.to_rgba8());
        return true;
    }

private:
    VkRenderPass pass_ = VK_NULL_HANDLE;
    Texture depth_;
    std::vector<VkFramebuffer> framebuffers_;
    Buffer vertex_;
    Buffer index_;
    Buffer readback_;
    std::uint32_t index_count_ = 0;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSemaphore acquired_ = VK_NULL_HANDLE;
    VkSemaphore rendered_ = VK_NULL_HANDLE;
    Image last_;
};

const VkRegister<SwapchainCube> kSwapchainCube{
    CaseSpec("frames", "swapchain_cube").frames(4, {0, 3}).swapchain().tolerance(Tolerance::tight(1))};

}  // namespace

}  // namespace lsw::render::vk
