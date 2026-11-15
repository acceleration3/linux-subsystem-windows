#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/common/pattern.hpp"
#include "render/vk/quadkit.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};

CaseSpec scene_spec(const std::string& name, unsigned frames, std::vector<unsigned> captured) {
    CaseSpec spec("scene", name);
    spec.size(128, 128).frames(frames, std::move(captured)).tolerance(Tolerance::exact()).timeout(120);
    return spec;
}

VkDescriptorPool pool_for(Rig& rig, const std::vector<VkDescriptorPoolSize>& sizes, std::uint32_t sets) {
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = sets;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    rig.vk_ok(rig.kit.api.vkCreateDescriptorPool(rig.kit.device, &info, nullptr, &pool), "vkCreateDescriptorPool");
    Kit* kit = &rig.kit;
    rig.kit.defer([kit, pool] { kit->api.vkDestroyDescriptorPool(kit->device, pool, nullptr); });
    return pool;
}

VkDescriptorSet set_of(Rig& rig, VkDescriptorPool pool, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    rig.vk_ok(rig.kit.api.vkAllocateDescriptorSets(rig.kit.device, &info, &set), "vkAllocateDescriptorSets");
    return set;
}

void write_buffer(Rig& rig, VkDescriptorSet set, std::uint32_t binding, const Buffer& buffer) {
    const VkDescriptorBufferInfo info{buffer.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &info;
    rig.kit.api.vkUpdateDescriptorSets(rig.kit.device, 1, &w, 0, nullptr);
}

void write_image(Rig& rig, VkDescriptorSet set, std::uint32_t binding, VkSampler sampler, VkImageView view, VkImageLayout layout) {
    const VkDescriptorImageInfo info{sampler, view, layout};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    rig.kit.api.vkUpdateDescriptorSets(rig.kit.device, 1, &w, 0, nullptr);
}

class LifeScene : public RigScene {
protected:
    void init(Rig& rig) override {
        target_ = &rig.make_target(TargetDesc{});
        std::vector<std::uint32_t> cells(32 * 32, 0);
        std::uint32_t state = 4242;
        for (std::uint32_t& c : cells) {
            c = Pattern::unit_random(state) > 0.62f ? 1u : 0u;
        }
        for (int i = 0; i < 5; ++i) {
            const int gx[5] = {1, 2, 0, 1, 2};
            const int gy[5] = {0, 1, 2, 2, 2};
            cells[static_cast<std::size_t>((4 + gy[i]) * 32 + 4 + gx[i])] = 1;
        }
        buf_[0] = rig.buffer(cells.data(), cells.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        buf_[1] = rig.blank(cells.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const VkDescriptorSetLayout step_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                                               {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}});
        const VkDescriptorSetLayout show_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorPool pool = pool_for(rig, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6}}, 4);
        for (unsigned i = 0; i < 2; ++i) {
            step_set_[i] = set_of(rig, pool, step_layout);
            write_buffer(rig, step_set_[i], 0, buf_[i]);
            write_buffer(rig, step_set_[i], 1, buf_[1 - i]);
            show_set_[i] = set_of(rig, pool, show_layout);
            write_buffer(rig, show_set_[i], 0, buf_[i]);
        }
        step_pl_ = rig.playout({step_layout});
        show_pl_ = rig.playout({show_layout});
        compute_ = rig.compute("life.comp", step_pl_);
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("life_show.frag");
        pipe.layout = show_pl_;
        pipe.pass = target_->clear_pass;
        show_ = rig.pipe(pipe);
    }

    void step(Rig& rig, unsigned frame) override {
        const unsigned from = frame % 2;
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_);
            rig.kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, step_pl_, 0, 1, &step_set_[from], 0, nullptr);
            rig.kit.api.vkCmdDispatch(cb, 4, 4, 1);
            rig.barrier_all(cb);
            target_->begin(rig.kit, cb, true);
            rig.bind_graphics(cb, show_, show_pl_, {show_set_[1 - from]});
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            target_->end(rig.kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Buffer buf_[2];
    VkDescriptorSet step_set_[2] = {};
    VkDescriptorSet show_set_[2] = {};
    VkPipelineLayout step_pl_ = VK_NULL_HANDLE;
    VkPipelineLayout show_pl_ = VK_NULL_HANDLE;
    VkPipeline compute_ = VK_NULL_HANDLE;
    VkPipeline show_ = VK_NULL_HANDLE;
};

class ParticleScene : public RigScene {
protected:
    void init(Rig& rig) override {
        target_ = &rig.make_target(TargetDesc{});
        std::vector<float> parts;
        std::uint32_t state = 99;
        for (int i = 0; i < 256; ++i) {
            parts.push_back(Pattern::unit_random(state) * 1.6f - 0.8f);
            parts.push_back(Pattern::unit_random(state) * 0.8f);
            parts.push_back(Pattern::unit_random(state) * 0.06f - 0.03f);
            parts.push_back(Pattern::unit_random(state) * 0.04f);
        }
        buffer_ = rig.buffer(parts.data(), parts.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const VkDescriptorSetLayout layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr}});
        const VkDescriptorPool pool = pool_for(rig, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, 1);
        set_ = set_of(rig, pool, layout);
        write_buffer(rig, set_, 0, buffer_);
        compute_pl_ = rig.playout({layout}, 16, VK_SHADER_STAGE_COMPUTE_BIT);
        draw_pl_ = rig.playout({layout});
        compute_ = rig.compute("particles.comp", compute_pl_);
        PipelineDesc pipe;
        pipe.vertex = rig.sh("particles.vert");
        pipe.fragment = rig.sh("col4.frag");
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        pipe.layout = draw_pl_;
        pipe.pass = target_->clear_pass;
        draw_ = rig.pipe(pipe);
    }

    void step(Rig& rig, unsigned frame) override {
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_);
            rig.kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pl_, 0, 1, &set_, 0, nullptr);
            const float params[4] = {8.0f + static_cast<float>(frame), 0, 0, 0};
            rig.push(cb, compute_pl_, VK_SHADER_STAGE_COMPUTE_BIT, params, 16);
            rig.kit.api.vkCmdDispatch(cb, 4, 1, 1);
            rig.barrier_all(cb);
            target_->begin(rig.kit, cb, true);
            rig.bind_graphics(cb, draw_, draw_pl_, {set_});
            rig.kit.api.vkCmdDraw(cb, 256, 1, 0, 0);
            target_->end(rig.kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Buffer buffer_;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout compute_pl_ = VK_NULL_HANDLE;
    VkPipelineLayout draw_pl_ = VK_NULL_HANDLE;
    VkPipeline compute_ = VK_NULL_HANDLE;
    VkPipeline draw_ = VK_NULL_HANDLE;
};

class TaaScene : public RigScene {
protected:
    void init(Rig& rig) override {
        TargetDesc td;
        td.color_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        current_ = &rig.make_target(td);
        history_[0] = &rig.make_target(td);
        history_[1] = &rig.make_target(td);
        std::copy(kClear, kClear + 4, current_->clear_color);
        scene_ = rig.pipe(rig.pos2c(*current_));
        const VkSampler sampler = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                          {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorPool pool = pool_for(rig, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}}, 2);
        for (unsigned i = 0; i < 2; ++i) {
            sets_[i] = set_of(rig, pool, layout);
            write_image(rig, sets_[i], 0, sampler, current_->color_[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            write_image(rig, sets_[i], 1, sampler, history_[i]->color_[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        pl_ = rig.playout({layout});
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("taa.frag");
        pipe.layout = pl_;
        pipe.pass = history_[0]->clear_pass;
        resolve_ = rig.pipe(pipe);
        const VkPipeline blank = scene_;
        for (Target* h : history_) {
            rig.draw(*h, blank, nullptr, [](VkCommandBuffer) {});
        }
    }

    void step(Rig& rig, unsigned frame) override {
        static const float jitter[6][2] = {{0.0f, 0.0f}, {0.5f, 0.33f}, {-0.25f, 0.66f}, {0.25f, -0.33f}, {-0.5f, 0.1f}, {0.1f, -0.6f}};
        const float jx = jitter[frame % 6][0] * 2.0f / 128.0f;
        const float jy = jitter[frame % 6][1] * 2.0f / 128.0f;
        const std::vector<Vertex2> tris = {vertex(-0.8f + jx, -0.7f + jy, 0), vertex(0.6f + jx, -0.85f + jy, 1), vertex(-0.1f + jx, 0.8f + jy, 2),
                                           vertex(0.2f + jx, 0.9f + jy, 3),   vertex(0.9f + jx, 0.2f + jy, 4),   vertex(0.65f + jx, -0.5f + jy, 5)};
        const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const unsigned prev = frame % 2;
        const unsigned next = 1 - prev;
        rig.draw(*current_, scene_, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0); });
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.barrier(cb, current_->color_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            rig.kit.barrier(cb, history_[prev]->color_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            history_[next]->begin(rig.kit, cb, true);
            rig.bind_graphics(cb, resolve_, pl_, {sets_[prev]});
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            history_[next]->end(rig.kit, cb);
            rig.kit.barrier(cb, history_[prev]->color_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        });
        last_ = next;
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*history_[last_])); }

private:
    Target* current_ = nullptr;
    Target* history_[2] = {};
    VkPipeline scene_ = VK_NULL_HANDLE;
    VkPipeline resolve_ = VK_NULL_HANDLE;
    VkPipelineLayout pl_ = VK_NULL_HANDLE;
    VkDescriptorSet sets_[2] = {};
    unsigned last_ = 0;
};

class ShadowScene : public RigScene {
protected:
    std::string caps(Kit& kit) override {
        return Cases::need_format(kit, VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "sampled linear-filtered D32");
    }

    void init(Rig& rig) override {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        target_ = &rig.make_target(td);
        std::copy(kClear, kClear + 4, target_->clear_color);
        TextureDesc desc;
        desc.width = desc.height = 128;
        desc.format = VK_FORMAT_D32_SFLOAT;
        desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        shadow_ = rig.texture(desc);
        VkAttachmentDescription a{};
        a.format = VK_FORMAT_D32_SFLOAT;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.pDepthStencilAttachment = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = 0;
        dep.dstSubpass = VK_SUBPASS_EXTERNAL;
        dep.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &a;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;
        shadow_pass_ = rig.render_pass(rp);
        shadow_fb_ = rig.framebuffer(shadow_pass_, {shadow_.view}, 128, 128);
        const auto cube_v = Mesh::cube_vertices();
        const auto cube_i = Mesh::cube_indices();
        std::vector<ColoredVertex> verts = cube_v;
        for (ColoredVertex& v : verts) {
            v.position[1] = v.position[1] * 0.5f + 0.0f;
            v.position[0] *= 0.5f;
            v.position[2] *= 0.5f;
        }
        const float plane[4][3] = {{-2, -0.5f, -2}, {2, -0.5f, -2}, {2, -0.5f, 2}, {-2, -0.5f, 2}};
        for (const auto& p : plane) {
            verts.push_back({{p[0], p[1], p[2]}, {0.55f, 0.6f, 0.5f}});
        }
        std::vector<std::uint16_t> idx = cube_i;
        for (std::uint16_t i : {8, 10, 9, 8, 11, 10}) {
            idx.push_back(static_cast<std::uint16_t>(i));
        }
        count_ = static_cast<std::uint32_t>(idx.size());
        vb_ = rig.buffer(verts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        ib_ = rig.buffer(idx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        VkSamplerCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sci.compareEnable = VK_TRUE;
        sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        sci.maxAnisotropy = 1.0f;
        const VkSampler sampler = rig.sampler(sci);
        const VkDescriptorSetLayout layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorPool pool = pool_for(rig, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}, 1);
        set_ = set_of(rig, pool, layout);
        write_image(rig, set_, 0, sampler, shadow_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        layout_ = rig.playout({layout}, 128, VK_SHADER_STAGE_VERTEX_BIT);
        const VkPipelineLayout depth_layout = rig.playout({}, 128, VK_SHADER_STAGE_VERTEX_BIT);
        PipelineDesc depth_pipe;
        depth_pipe.vertex = rig.sh("depth_only.vert");
        depth_pipe.bindings = {{0, sizeof(ColoredVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
        depth_pipe.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
        depth_pipe.layout = depth_layout;
        depth_pipe.pass = shadow_pass_;
        depth_pipe.depth_test = true;
        depth_pipe.color_attachments = 0;
        depth_pipe.depth_bias = true;
        depth_pipe.bias_constant = 2.0f;
        depth_pipe.bias_slope = 1.5f;
        depth_layout_ = depth_layout;
        shadow_pipe_ = rig.pipe(depth_pipe);
        PipelineDesc lit;
        lit.vertex = rig.sh("shadow_lit.vert");
        lit.fragment = rig.sh("shadow_lit.frag");
        lit.bindings = depth_pipe.bindings;
        lit.attributes = depth_pipe.attributes;
        lit.layout = layout_;
        lit.pass = target_->clear_pass;
        lit.depth_test = true;
        lit_pipe_ = rig.pipe(lit);
    }

    void step(Rig& rig, unsigned frame) override {
        const float angle = 0.5f + 0.45f * static_cast<float>(frame);
        const Mat4 light = Mat4::perspective(1.2f, 1.0f, 1.0f, 12.0f, true) * Mat4::translate(0, 0, -4.5f) * Mat4::rotate_x(0.9f) * Mat4::rotate_y(angle);
        const Mat4 camera = Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, true) * Mat4::translate(0, 0, -5.0f) * Mat4::rotate_x(0.55f) * Mat4::rotate_y(-0.4f);
        float push[32];
        std::memcpy(push, camera.m, 64);
        std::memcpy(push + 16, light.m, 64);
        rig.run([&](VkCommandBuffer cb) {
            VkClearValue clear{};
            clear.depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            begin.renderPass = shadow_pass_;
            begin.framebuffer = shadow_fb_;
            begin.renderArea = {{0, 0}, {128, 128}};
            begin.clearValueCount = 1;
            begin.pClearValues = &clear;
            rig.kit.api.vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
            rig.viewport(cb, 0, 0, 128, 128);
            rig.scissor(cb, 0, 0, 128, 128);
            rig.bind_graphics(cb, shadow_pipe_);
            rig.bind_vertex(cb, vb_);
            rig.kit.api.vkCmdBindIndexBuffer(cb, ib_.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.push(cb, depth_layout_, VK_SHADER_STAGE_VERTEX_BIT, push, 128);
            rig.kit.api.vkCmdDrawIndexed(cb, count_, 1, 0, 0, 0);
            rig.kit.api.vkCmdEndRenderPass(cb);
            target_->begin(rig.kit, cb, true);
            rig.bind_graphics(cb, lit_pipe_, layout_, {set_});
            rig.bind_vertex(cb, vb_);
            rig.kit.api.vkCmdBindIndexBuffer(cb, ib_.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.push(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, push, 128);
            rig.kit.api.vkCmdDrawIndexed(cb, count_, 1, 0, 0, 0);
            target_->end(rig.kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Texture shadow_;
    VkRenderPass shadow_pass_ = VK_NULL_HANDLE;
    VkFramebuffer shadow_fb_ = VK_NULL_HANDLE;
    Buffer vb_;
    Buffer ib_;
    std::uint32_t count_ = 0;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipelineLayout depth_layout_ = VK_NULL_HANDLE;
    VkPipeline shadow_pipe_ = VK_NULL_HANDLE;
    VkPipeline lit_pipe_ = VK_NULL_HANDLE;
};

struct SkinVertex {
    float pos[2];
    float weights[4];
};

class SkinScene : public RigScene {
protected:
    void init(Rig& rig) override {
        target_ = &rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target_->clear_color);
        std::vector<SkinVertex> v;
        for (int i = 0; i < 9; ++i) {
            const float t = static_cast<float>(i) / 8.0f;
            const float w0 = std::max(0.0f, 1.0f - t * 2.0f);
            const float w2 = std::max(0.0f, t * 2.0f - 1.0f);
            const float w1 = 1.0f - w0 - w2;
            v.push_back({{-0.8f + 1.6f * t, -0.15f}, {w0, w1, w2, 0}});
            v.push_back({{-0.8f + 1.6f * t, 0.15f}, {w0, w1, w2, 0}});
        }
        vb_ = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        bones_ = rig.blank(3 * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const VkDescriptorSetLayout layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}});
        const VkDescriptorPool pool = pool_for(rig, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, 1);
        set_ = set_of(rig, pool, layout);
        write_buffer(rig, set_, 0, bones_);
        layout_ = rig.playout({layout});
        PipelineDesc pipe = rig.pos2c(*target_, layout_);
        pipe.vertex = rig.sh("skin.vert");
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        pipe.bindings = {{0, sizeof(SkinVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8}};
        pipeline_ = rig.pipe(pipe);
    }

    void step(Rig& rig, unsigned frame) override {
        const float t = static_cast<float>(frame);
        const Mat4 b0 = Mat4::rotate_z(0.35f * std::sin(t * 0.9f));
        const Mat4 b1 = Mat4::translate(0.0f, 0.18f * std::sin(t * 0.7f + 1.0f), 0.0f);
        const Mat4 b2 = Mat4::rotate_z(-0.5f * std::sin(t * 0.8f + 0.5f)) * Mat4::scale(1.0f, 1.0f + 0.3f * std::sin(t), 1.0f);
        float data[48];
        std::memcpy(data, b0.m, 64);
        std::memcpy(data + 16, b1.m, 64);
        std::memcpy(data + 32, b2.m, 64);
        std::string error;
        rig.must(rig.kit.write(bones_, 0, data, sizeof data, error), error);
        rig.draw(*target_, pipeline_, &vb_, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 18, 1, 0, 0); }, layout_, {set_});
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Buffer vb_;
    Buffer bones_;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

class ForestScene : public RigScene {
protected:
    void init(Rig& rig) override {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        target_ = &rig.make_target(td);
        std::copy(kClear, kClear + 4, target_->clear_color);
        const std::vector<Vertex2> tree = {vertex(-0.5f, -1.0f, 1), vertex(0.5f, -1.0f, 1), vertex(0.0f, 1.0f, 3)};
        vb_ = rig.buffer(tree, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        std::vector<float> inst;
        std::uint32_t state = 7;
        for (int i = 0; i < 160; ++i) {
            inst.push_back(Pattern::unit_random(state) * 1.8f - 0.9f);
            inst.push_back(Pattern::unit_random(state) * 1.6f - 0.8f);
            inst.push_back(0.05f + 0.1f * Pattern::unit_random(state));
        }
        ib_ = rig.buffer(inst.data(), inst.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        layout_ = rig.playout({}, 16, VK_SHADER_STAGE_VERTEX_BIT);
        PipelineDesc pipe = rig.pos2c(*target_, layout_);
        pipe.vertex = rig.sh("forest.vert");
        pipe.bindings = {{0, sizeof(Vertex2), VK_VERTEX_INPUT_RATE_VERTEX}, {1, 12, VK_VERTEX_INPUT_RATE_INSTANCE}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}};
        pipe.depth_test = true;
        pipeline_ = rig.pipe(pipe);
    }

    void step(Rig& rig, unsigned frame) override {
        rig.draw(*target_, pipeline_, &vb_, [&](VkCommandBuffer cb) {
            rig.bind_vertex(cb, ib_, 0, 1);
            const float wind[4] = {0.9f * static_cast<float>(frame), 0, 0, 0};
            rig.push(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, wind, 16);
            rig.kit.api.vkCmdDraw(cb, 3, 160, 0, 0);
        }, layout_);
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Buffer vb_;
    Buffer ib_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

class StreamTextureScene : public RigScene {
protected:
    void init(Rig& rig) override {
        target_ = &rig.make_target(TargetDesc{});
        TextureDesc desc;
        desc.width = desc.height = 32;
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        texture_ = rig.texture(desc);
        std::string error;
        VkBufferImageCopy full{};
        full.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        full.imageExtent = {32, 32, 1};
        rig.must(rig.kit.upload_texture(texture_, {full}, std::vector<std::uint8_t>(32 * 32 * 4, 0x18), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error), error);
        staging_ = rig.blank(8 * 32 * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorPool pool = pool_for(rig, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}, 1);
        set_ = set_of(rig, pool, layout);
        write_image(rig, set_, 0, sampler, texture_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        layout_ = rig.playout({layout}, 48, VK_SHADER_STAGE_FRAGMENT_BIT);
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh("tex_f.frag");
        pipe.layout = layout_;
        pipe.pass = target_->clear_pass;
        pipeline_ = rig.pipe(pipe);
    }

    void step(Rig& rig, unsigned frame) override {
        std::vector<std::uint8_t> band = Pattern::checker(8, 32, 2 + frame % 3, Pattern::pack(220 - 25 * frame, 60 + 30 * frame, 90), Pattern::pack(30, 40 + 20 * frame, 210 - 20 * frame));
        std::string error;
        rig.must(rig.kit.write(staging_, 0, band.data(), band.size(), error), error);
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.barrier(cb, texture_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageOffset = {static_cast<std::int32_t>((frame % 4) * 8), 0, 0};
            region.imageExtent = {8, 32, 1};
            rig.kit.api.vkCmdCopyBufferToImage(cb, staging_.buffer, texture_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            rig.kit.barrier(cb, texture_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        struct Push {
            float scale[4];
            float bias[4];
            std::int32_t info[4];
        } push{{1, 1, 1, 1}, {0, 0, 0, 0}, {32, 32, 0, 0}};
        rig.draw(*target_, pipeline_, nullptr, [&](VkCommandBuffer cb) {
            rig.push(cb, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, &push, sizeof push);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        }, layout_, {set_});
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Texture texture_;
    Buffer staging_;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

class TerrainScene : public RigScene {
protected:
    std::string caps(Kit& kit) override { return kit.features.tessellationShader != VK_FALSE ? std::string() : std::string("tessellationShader"); }

    void init(Rig& rig) override {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        target_ = &rig.make_target(td);
        std::copy(kClear, kClear + 4, target_->clear_color);
        std::vector<std::uint8_t> height(32 * 32 * 4);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const float h = 0.5f + 0.3f * std::sin(x * 0.4f) * std::cos(y * 0.3f) + 0.2f * std::sin((x + y) * 0.9f);
                const auto v = static_cast<std::uint8_t>(std::min(1.0f, std::max(0.0f, h)) * 255.0f);
                const std::size_t at = static_cast<std::size_t>(y * 32 + x) * 4;
                height[at] = v;
                height[at + 1] = height[at + 2] = 0;
                height[at + 3] = 255;
            }
        }
        const Texture tex = rig.sampled(32, 32, VK_FORMAT_R8G8B8A8_UNORM, height);
        const VkSampler sampler = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, nullptr}});
        const VkDescriptorPool pool = pool_for(rig, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}, 1);
        set_ = set_of(rig, pool, layout);
        write_image(rig, set_, 0, sampler, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        layout_ = rig.playout({layout}, 80, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
        std::vector<Vertex2> patches;
        for (int j = 0; j < 4; ++j) {
            for (int i = 0; i < 4; ++i) {
                const float x0 = -1.0f + 0.5f * static_cast<float>(i);
                const float y0 = -1.0f + 0.5f * static_cast<float>(j);
                patches.push_back(vertex(x0, y0, 0));
                patches.push_back(vertex(x0 + 0.5f, y0, 0));
                patches.push_back(vertex(x0 + 0.5f, y0 + 0.5f, 0));
                patches.push_back(vertex(x0, y0 + 0.5f, 0));
            }
        }
        vb_ = rig.buffer(patches, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(*target_, layout_);
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        pipe.patch_points = 4;
        pipe.extra.push_back({VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, rig.sh("terrain.tesc"), nullptr});
        pipe.extra.push_back({VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, rig.sh("terrain.tese"), nullptr});
        pipe.depth_test = true;
        pipeline_ = rig.pipe(pipe);
    }

    void step(Rig& rig, unsigned frame) override {
        const Mat4 vp = Mat4::perspective(1.0f, 1.0f, 0.3f, 20.0f, true) * Mat4::translate(0.0f, -0.4f, -2.6f + 0.25f * static_cast<float>(frame)) *
                        Mat4::rotate_x(0.7f) * Mat4::rotate_y(0.3f * static_cast<float>(frame));
        float push[20];
        std::memcpy(push, vp.m, 64);
        push[16] = 0.9f;
        push[17] = push[18] = push[19] = 0;
        rig.draw(*target_, pipeline_, &vb_, [&](VkCommandBuffer cb) {
            rig.push(cb, layout_, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, push, 80);
            rig.kit.api.vkCmdDraw(cb, 64, 1, 0, 0);
        }, layout_, {set_});
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Buffer vb_;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

struct GlyphVertex {
    float xyuv[4];
    float color[4];
};

class UiTextScene : public RigScene {
protected:
    void init(Rig& rig) override {
        target_ = &rig.make_target(TargetDesc{});
        target_->clear_color[0] = 0.12f;
        target_->clear_color[1] = 0.14f;
        target_->clear_color[2] = 0.2f;
        std::vector<std::uint8_t> atlas(64 * 8 * 4, 0);
        for (unsigned g = 0; g < 8; ++g) {
            for (unsigned y = 0; y < 8; ++y) {
                for (unsigned x = 0; x < 8; ++x) {
                    const bool on = ((x * (g + 1) + y * (7 - g)) % 5 < 2) || (x == y && g % 2 == 0) || (g == 3 && (x == 0 || y == 7));
                    const std::size_t at = (static_cast<std::size_t>(y) * 64 + g * 8 + x) * 4;
                    atlas[at] = on ? 255 : 0;
                    atlas[at + 3] = 255;
                }
            }
        }
        const Texture tex = rig.sampled(64, 8, VK_FORMAT_R8G8B8A8_UNORM, atlas);
        const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        const VkDescriptorPool pool = pool_for(rig, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}, 1);
        set_ = set_of(rig, pool, layout);
        write_image(rig, set_, 0, sampler, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        layout_ = rig.playout({layout});
        PipelineDesc pipe;
        pipe.vertex = rig.sh("ui_text.vert");
        pipe.fragment = rig.sh("ui_text.frag");
        pipe.bindings = {{0, sizeof(GlyphVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
        pipe.layout = layout_;
        pipe.pass = target_->clear_pass;
        pipe.blend = true;
        pipeline_ = rig.pipe(pipe);
        ring_ = rig.blank(4 * 6 * 64 * sizeof(GlyphVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }

    void step(Rig& rig, unsigned frame) override {
        std::vector<GlyphVertex> v;
        const auto glyph = [&](float x, float y, float w, float h, unsigned g, const float* color) {
            const float u0 = static_cast<float>(g) / 8.0f;
            const float u1 = u0 + 0.125f;
            const float c[4] = {color[0], color[1], color[2], color[3]};
            const float xs[6] = {x, x + w, x, x + w, x + w, x};
            const float ys[6] = {y, y, y + h, y, y + h, y + h};
            const float us[6] = {u0, u1, u0, u1, u1, u0};
            const float vs[6] = {0, 0, 1, 0, 1, 1};
            for (int i = 0; i < 6; ++i) {
                v.push_back({{xs[i], ys[i], us[i], vs[i]}, {c[0], c[1], c[2], c[3]}});
            }
        };
        for (unsigned line = 0; line < 4; ++line) {
            for (unsigned i = 0; i < 9; ++i) {
                const unsigned g = (frame * 3 + line * 5 + i * (line + 1)) % 8;
                const float* col = palette(line + i / 3);
                const float tint[4] = {col[0], col[1], col[2], 0.85f};
                glyph(-0.92f + 0.2f * static_cast<float>(i), -0.85f + 0.4f * static_cast<float>(line), 0.18f, 0.32f, g, tint);
            }
        }
        const float bar[4] = {0.9f, 0.9f, 0.2f, 0.6f};
        glyph(-0.95f, 0.8f, 0.2f * static_cast<float>(frame + 1), 0.12f, 3, bar);
        const VkDeviceSize slot = (frame % 4) * 6 * 64 * sizeof(GlyphVertex);
        std::string error;
        rig.must(rig.kit.write(ring_, slot, v.data(), v.size() * sizeof(GlyphVertex), error), error);
        const auto count = static_cast<std::uint32_t>(v.size());
        rig.run([&](VkCommandBuffer cb) {
            target_->begin(rig.kit, cb, true);
            rig.bind_graphics(cb, pipeline_, layout_, {set_});
            rig.bind_vertex(cb, ring_, slot);
            rig.kit.api.vkCmdDraw(cb, count, 1, 0, 0);
            target_->end(rig.kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Buffer ring_;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

const int registered = (Cases::add_class<LifeScene>(scene_spec("game_of_life_compute_pingpong", 8, {0, 3, 7})),
                        Cases::add_class<ParticleScene>(scene_spec("gpu_particles_compute", 6, {0, 2, 5})),
                        Cases::add_class<TaaScene>(scene_spec("temporal_accumulation_jitter", 6, {0, 2, 5})),
                        Cases::add_class<ShadowScene>(scene_spec("shadow_map_compare_sampler", 5, {0, 2, 4})),
                        Cases::add_class<SkinScene>(scene_spec("skinning_ssbo_bones", 6, {0, 2, 5})),
                        Cases::add_class<ForestScene>(scene_spec("instanced_forest_wind", 5, {0, 2, 4})),
                        Cases::add_class<StreamTextureScene>(scene_spec("streamed_texture_updates", 6, {0, 2, 5})),
                        Cases::add_class<TerrainScene>(scene_spec("tessellated_terrain_camera", 4, {0, 1, 3})),
                        Cases::add_class<UiTextScene>(scene_spec("ui_text_atlas_ring", 5, {0, 2, 4})), 0);

}  // namespace

}  // namespace lsw::render::vk
