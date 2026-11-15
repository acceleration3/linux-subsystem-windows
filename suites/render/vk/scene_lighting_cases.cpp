#include "render/common/mat4.hpp"
#include "render/common/pattern.hpp"
#include "render/vk/quadkit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

CaseSpec scene_spec(const std::string& name, unsigned frames, std::vector<unsigned> captured) {
    CaseSpec spec("scene", name);
    spec.size(128, 128).frames(frames, std::move(captured)).tolerance(Tolerance::exact()).timeout(120);
    return spec;
}

class BloomScene : public RigScene {
protected:
    std::string caps(Kit& kit) override {
        return Cases::need_format(kit, VK_FORMAT_R16G16B16A16_SFLOAT,
                                  VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
                                  "linear-filtered RGBA16F render target");
    }

    void init(Rig& rig) override {
        TargetDesc hdr;
        hdr.colors = {VK_FORMAT_R16G16B16A16_SFLOAT};
        for (Target*& t : fp_) {
            t = &rig.make_target(hdr);
        }
        final_ = &rig.make_target(TargetDesc{});
        sampler_ = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        layout_set_ = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                   {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        pl_ = rig.playout({layout_set_}, 16, VK_SHADER_STAGE_FRAGMENT_BIT);
        scene_ = rig.pipe(rig.pos2c(*fp_[kScene]));
        bright_ = fx(rig, "bloom_bright.frag", *fp_[kScene]);
        blur_ = fx(rig, "bloom_blur.frag", *fp_[kScene]);
        accum_ = fx(rig, "bloom_accum.frag", *fp_[kScene]);
        tone_ = fx(rig, "tonemap.frag", *final_);
        s_scene_ = pair(rig, kScene, kScene);
        s_bright_ = pair(rig, kBright, kBright);
        s_blur1_ = pair(rig, kBlur1, kBlur1);
        for (unsigned i = 0; i < 2; ++i) {
            s_accum_[i] = pair(rig, kBlur2, kHist0 + i);
            s_tone_[i] = pair(rig, kScene, kHist0 + i);
        }
        for (unsigned i = 0; i < 2; ++i) {
            rig.draw(*fp_[kHist0 + i], scene_, nullptr, [](VkCommandBuffer) {});
        }
    }

    void step(Rig& rig, unsigned frame) override {
        Kit& kit = rig.kit;
        std::vector<Vertex2> v;
        const float soft[4] = {0.35f, 0.4f, 0.55f, 1.0f};
        const float warm[4] = {0.9f, 0.6f, 0.3f, 1.0f};
        const auto add = [&](const std::vector<Vertex2>& more) { v.insert(v.end(), more.begin(), more.end()); };
        add(make_disc(-0.4f, -0.35f, 0.5f, 24, soft));
        add(make_disc(0.5f, 0.4f, 0.35f, 20, warm));
        for (unsigned k = 0; k < 4; ++k) {
            const float a = 0.8f * static_cast<float>(frame) + 1.57f * static_cast<float>(k);
            const float emissive[4] = {5.0f - static_cast<float>(k), 2.5f + static_cast<float>(k), 1.0f + 2.0f * static_cast<float>(k), 1.0f};
            add(make_disc(0.6f * std::cos(a), 0.55f * std::sin(a * 0.7f + static_cast<float>(k)), 0.12f, 12, emissive));
        }
        const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const unsigned prev = frame % 2;
        const unsigned next = 1 - prev;
        const float texel = 1.0f / 128.0f;
        const float horizontal[4] = {texel, 0, 0, 0};
        const float vertical[4] = {0, texel, 0, 0};
        const float exposure[4] = {1.0f + 0.2f * static_cast<float>(frame), 0, 0, 0};
        rig.run([&](VkCommandBuffer cb) {
            const auto to_read = [&](unsigned index) {
                kit.barrier(cb, fp_[index]->color_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            };
            const auto pass = [&](Target& target, VkPipeline pipeline, VkDescriptorSet set, const float* push) {
                target.begin(kit, cb, true);
                rig.bind_graphics(cb, pipeline, pl_, {set});
                rig.push(cb, pl_, VK_SHADER_STAGE_FRAGMENT_BIT, push, 16);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                target.end(kit, cb);
            };
            fp_[kScene]->begin(kit, cb, true);
            rig.bind_graphics(cb, scene_);
            rig.bind_vertex(cb, vb);
            kit.api.vkCmdDraw(cb, static_cast<std::uint32_t>(v.size()), 1, 0, 0);
            fp_[kScene]->end(kit, cb);
            to_read(kScene);
            pass(*fp_[kBright], bright_, s_scene_, horizontal);
            to_read(kBright);
            pass(*fp_[kBlur1], blur_, s_bright_, horizontal);
            to_read(kBlur1);
            pass(*fp_[kBlur2], blur_, s_blur1_, vertical);
            to_read(kBlur2);
            to_read(kHist0 + prev);
            pass(*fp_[kHist0 + next], accum_, s_accum_[prev], horizontal);
            to_read(kHist0 + next);
            pass(*final_, tone_, s_tone_[next], exposure);
            kit.barrier(cb, fp_[kHist0 + prev]->color_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            kit.barrier(cb, fp_[kHist0 + next]->color_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*final_)); }

private:
    enum : unsigned { kScene = 0, kBright = 1, kBlur1 = 2, kBlur2 = 3, kHist0 = 4 };

    VkPipeline fx(Rig& rig, const char* fragment, Target& target) {
        PipelineDesc pipe;
        pipe.vertex = rig.sh("full.vert");
        pipe.fragment = rig.sh(fragment);
        pipe.layout = pl_;
        pipe.pass = target.clear_pass;
        return rig.pipe(pipe);
    }

    VkDescriptorSet pair(Rig& rig, unsigned first, unsigned second) {
        DescriptorWrite a;
        a.binding = 0;
        a.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        a.image = {sampler_, fp_[first]->color_[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        DescriptorWrite b = a;
        b.binding = 1;
        b.image.imageView = fp_[second]->color_[0].view;
        return rig.dset(layout_set_, {a, b});
    }

    Target* fp_[6] = {};
    Target* final_ = nullptr;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_set_ = VK_NULL_HANDLE;
    VkPipelineLayout pl_ = VK_NULL_HANDLE;
    VkPipeline scene_ = VK_NULL_HANDLE;
    VkPipeline bright_ = VK_NULL_HANDLE;
    VkPipeline blur_ = VK_NULL_HANDLE;
    VkPipeline accum_ = VK_NULL_HANDLE;
    VkPipeline tone_ = VK_NULL_HANDLE;
    VkDescriptorSet s_scene_ = VK_NULL_HANDLE;
    VkDescriptorSet s_bright_ = VK_NULL_HANDLE;
    VkDescriptorSet s_blur1_ = VK_NULL_HANDLE;
    VkDescriptorSet s_accum_[2] = {};
    VkDescriptorSet s_tone_[2] = {};
};

struct GVertex {
    float pos[3];
    float normal[3];
    float color[3];
};

void sphere_mesh(std::vector<GVertex>& v, std::vector<std::uint16_t>& idx, float cx, float cy, float cz, float r, const float* color) {
    const int rings = 10;
    const int segs = 14;
    const std::size_t base = v.size();
    for (int i = 0; i <= rings; ++i) {
        const float phi = 3.14159265f * static_cast<float>(i) / static_cast<float>(rings);
        for (int j = 0; j <= segs; ++j) {
            const float theta = 6.2831853f * static_cast<float>(j) / static_cast<float>(segs);
            const float nx = std::sin(phi) * std::cos(theta);
            const float ny = std::cos(phi);
            const float nz = std::sin(phi) * std::sin(theta);
            v.push_back({{cx + r * nx, cy + r * ny, cz + r * nz}, {nx, ny, nz}, {color[0], color[1], color[2]}});
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segs; ++j) {
            const auto a = static_cast<std::uint16_t>(base + static_cast<std::size_t>(i * (segs + 1) + j));
            const auto b = static_cast<std::uint16_t>(a + segs + 1);
            idx.insert(idx.end(), {a, b, static_cast<std::uint16_t>(a + 1), static_cast<std::uint16_t>(a + 1), b, static_cast<std::uint16_t>(b + 1)});
        }
    }
}

class DeferredScene : public RigScene {
protected:
    void init(Rig& rig) override {
        Kit& kit = rig.kit;
        TextureDesc color;
        color.width = color.height = 128;
        color.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        albedo_ = rig.texture(color);
        normal_ = rig.texture(color);
        color.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        final_ = rig.texture(color);
        TextureDesc depth;
        depth.width = depth.height = 128;
        depth.format = VK_FORMAT_D32_SFLOAT;
        depth.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_ = rig.texture(depth);
        for (unsigned variant = 0; variant < 2; ++variant) {
            VkAttachmentDescription a[4]{};
            for (unsigned i = 0; i < 4; ++i) {
                a[i].format = i == 2 ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
                a[i].samples = VK_SAMPLE_COUNT_1_BIT;
                a[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                a[i].storeOp = i == 3 ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
                a[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                a[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                a[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                a[i].finalLayout = i == 3 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : (i == 2 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
            if (variant == 1) {
                a[3].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                a[3].initialLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            }
            const VkAttachmentReference gcolors[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
            const VkAttachmentReference gdepth{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            const VkAttachmentReference inputs[2] = {{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
            const VkAttachmentReference lcolor{3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subs[2]{};
            subs[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subs[0].colorAttachmentCount = 2;
            subs[0].pColorAttachments = gcolors;
            subs[0].pDepthStencilAttachment = &gdepth;
            subs[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subs[1].inputAttachmentCount = 2;
            subs[1].pInputAttachments = inputs;
            subs[1].colorAttachmentCount = 1;
            subs[1].pColorAttachments = &lcolor;
            VkSubpassDependency deps[3]{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            deps[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = 1;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
            deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            deps[2].srcSubpass = 1;
            deps[2].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[2].srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
            deps[2].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            deps[2].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            deps[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            VkRenderPassCreateInfo rp{};
            rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rp.attachmentCount = 4;
            rp.pAttachments = a;
            rp.subpassCount = 2;
            rp.pSubpasses = subs;
            rp.dependencyCount = 3;
            rp.pDependencies = deps;
            pass_[variant] = rig.render_pass(rp);
        }
        fb_ = rig.framebuffer(pass_[0], {albedo_.view, normal_.view, depth_.view, final_.view}, 128, 128);
        std::vector<GVertex> verts;
        std::vector<std::uint16_t> idx;
        const float red[3] = {0.9f, 0.3f, 0.25f};
        const float green[3] = {0.3f, 0.85f, 0.4f};
        const float blue[3] = {0.3f, 0.45f, 0.95f};
        sphere_mesh(verts, idx, -0.9f, 0.0f, 0.0f, 0.8f, red);
        sphere_mesh(verts, idx, 0.8f, -0.2f, -0.6f, 0.9f, green);
        sphere_mesh(verts, idx, 0.1f, 0.9f, 0.5f, 0.55f, blue);
        count_ = static_cast<std::uint32_t>(idx.size());
        vb_ = rig.buffer(verts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        ib_ = rig.buffer(idx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                              {1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        DescriptorWrite w0;
        w0.binding = 0;
        w0.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        w0.image = {VK_NULL_HANDLE, albedo_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        DescriptorWrite w1 = w0;
        w1.binding = 1;
        w1.image.imageView = normal_.view;
        set_ = rig.dset(set_layout, {w0, w1});
        light_layout_ = rig.playout({set_layout}, 128, VK_SHADER_STAGE_FRAGMENT_BIT);
        gbuf_layout_ = rig.playout({}, 64, VK_SHADER_STAGE_VERTEX_BIT);
        PipelineDesc g;
        g.vertex = rig.sh("gbuf.vert");
        g.fragment = rig.sh("gbuf.frag");
        g.bindings = {{0, sizeof(GVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
        g.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24}};
        g.layout = gbuf_layout_;
        g.pass = pass_[0];
        g.subpass = 0;
        g.color_attachments = 2;
        g.depth_test = true;
        gbuf_ = rig.pipe(g);
        PipelineDesc l;
        l.vertex = rig.sh("full.vert");
        l.fragment = rig.sh("deferred_light.frag");
        l.layout = light_layout_;
        l.pass = pass_[0];
        l.subpass = 1;
        l.blend = true;
        l.src_color = VK_BLEND_FACTOR_SRC_ALPHA;
        l.dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        l.src_alpha = VK_BLEND_FACTOR_ONE;
        l.dst_alpha = VK_BLEND_FACTOR_ZERO;
        light_ = rig.pipe(l);
        (void)kit;
    }

    void step(Rig& rig, unsigned frame) override {
        Kit& kit = rig.kit;
        const Mat4 vp = Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, true) * Mat4::translate(0, 0, -4.5f) * Mat4::rotate_y(0.25f * static_cast<float>(frame)) * Mat4::rotate_x(-0.35f);
        float push[32] = {};
        for (unsigned i = 0; i < 4; ++i) {
            const float a = 0.9f * static_cast<float>(frame) + 1.5708f * static_cast<float>(i);
            push[i * 4 + 0] = std::cos(a);
            push[i * 4 + 1] = 0.4f + 0.3f * std::sin(a * 1.3f);
            push[i * 4 + 2] = std::sin(a);
            const float tint[4][3] = {{1.0f, 0.8f, 0.6f}, {0.4f, 0.6f, 1.0f}, {0.8f, 1.0f, 0.5f}, {1.0f, 0.5f, 0.8f}};
            for (unsigned c = 0; c < 3; ++c) {
                push[16 + i * 4 + c] = 0.55f * tint[i][c];
            }
        }
        rig.run([&](VkCommandBuffer cb) {
            VkClearValue clears[4]{};
            clears[2].depthStencil = {1.0f, 0};
            clears[3].color = {{0.04f, 0.05f, 0.08f, 1.0f}};
            VkRenderPassBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            begin.renderPass = pass_[frame == 0 ? 0 : 1];
            begin.framebuffer = fb_;
            begin.renderArea = {{0, 0}, {128, 128}};
            begin.clearValueCount = 4;
            begin.pClearValues = clears;
            kit.api.vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
            rig.viewport(cb, 0, 0, 128, 128);
            rig.scissor(cb, 0, 0, 128, 128);
            rig.bind_graphics(cb, gbuf_);
            rig.bind_vertex(cb, vb_);
            kit.api.vkCmdBindIndexBuffer(cb, ib_.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.push(cb, gbuf_layout_, VK_SHADER_STAGE_VERTEX_BIT, vp.m, 64);
            kit.api.vkCmdDrawIndexed(cb, count_, 1, 0, 0, 0);
            kit.api.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
            rig.bind_graphics(cb, light_, light_layout_, {set_});
            rig.push(cb, light_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, push, 128);
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            kit.api.vkCmdEndRenderPass(cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.read(final_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)); }

private:
    Texture albedo_;
    Texture normal_;
    Texture final_;
    Texture depth_;
    VkRenderPass pass_[2] = {};
    VkFramebuffer fb_ = VK_NULL_HANDLE;
    Buffer vb_;
    Buffer ib_;
    std::uint32_t count_ = 0;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout light_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout gbuf_layout_ = VK_NULL_HANDLE;
    VkPipeline gbuf_ = VK_NULL_HANDLE;
    VkPipeline light_ = VK_NULL_HANDLE;
};

class ForwardPlusScene : public RigScene {
protected:
    void init(Rig& rig) override {
        target_ = &rig.make_target(TargetDesc{});
        lights_ = rig.blank(64 * 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        tiles_ = rig.blank(64 * 2 * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
        const VkDescriptorSetLayout cull_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                                               {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}});
        const VkDescriptorSetLayout shade_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                                {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        DescriptorWrite a;
        a.binding = 0;
        a.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        a.buffer = {lights_.buffer, 0, VK_WHOLE_SIZE};
        DescriptorWrite b = a;
        b.binding = 1;
        b.buffer = {tiles_.buffer, 0, VK_WHOLE_SIZE};
        cull_set_ = rig.dset(cull_layout, {a, b});
        shade_set_ = rig.dset(shade_layout, {a, b});
        cull_pl_ = rig.playout({cull_layout});
        shade_pl_ = rig.playout({shade_layout});
        cull_ = rig.compute("fplus_cull.comp", cull_pl_);
        PipelineDesc p;
        p.vertex = rig.sh("full.vert");
        p.fragment = rig.sh("fplus_shade.frag");
        p.layout = shade_pl_;
        p.pass = target_->clear_pass;
        shade_ = rig.pipe(p);
        std::uint32_t state = 777;
        for (unsigned i = 0; i < 64; ++i) {
            pos_[i][0] = 128.0f * Pattern::unit_random(state);
            pos_[i][1] = 128.0f * Pattern::unit_random(state);
            vel_[i][0] = 6.0f * (Pattern::unit_random(state) - 0.5f);
            vel_[i][1] = 6.0f * (Pattern::unit_random(state) - 0.5f);
            radius_[i] = 8.0f + 16.0f * Pattern::unit_random(state);
            for (unsigned c = 0; c < 3; ++c) {
                color_[i][c] = 0.3f + 0.7f * Pattern::unit_random(state);
            }
        }
    }

    void step(Rig& rig, unsigned) override {
        Kit& kit = rig.kit;
        float data[64 * 8];
        for (unsigned i = 0; i < 64; ++i) {
            for (unsigned c = 0; c < 2; ++c) {
                pos_[i][c] += vel_[i][c];
                if (pos_[i][c] < 0.0f || pos_[i][c] > 128.0f) {
                    vel_[i][c] = -vel_[i][c];
                    pos_[i][c] = std::clamp(pos_[i][c], 0.0f, 128.0f);
                }
            }
            float* d = data + i * 8;
            d[0] = pos_[i][0];
            d[1] = pos_[i][1];
            d[2] = radius_[i];
            d[3] = 0;
            d[4] = color_[i][0];
            d[5] = color_[i][1];
            d[6] = color_[i][2];
            d[7] = 0.9f;
        }
        std::string error;
        rig.must(kit.write(lights_, 0, data, sizeof data, error), error);
        rig.run([&](VkCommandBuffer cb) {
            kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cull_);
            kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cull_pl_, 0, 1, &cull_set_, 0, nullptr);
            kit.api.vkCmdDispatch(cb, 8, 8, 1);
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            kit.api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            target_->begin(kit, cb, true);
            rig.bind_graphics(cb, shade_, shade_pl_, {shade_set_});
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            target_->end(kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    Buffer lights_;
    Buffer tiles_;
    VkDescriptorSet cull_set_ = VK_NULL_HANDLE;
    VkDescriptorSet shade_set_ = VK_NULL_HANDLE;
    VkPipelineLayout cull_pl_ = VK_NULL_HANDLE;
    VkPipelineLayout shade_pl_ = VK_NULL_HANDLE;
    VkPipeline cull_ = VK_NULL_HANDLE;
    VkPipeline shade_ = VK_NULL_HANDLE;
    float pos_[64][2] = {};
    float vel_[64][2] = {};
    float radius_[64] = {};
    float color_[64][3] = {};
};

const int registered = (Cases::add_class<BloomScene>(scene_spec("hdr_bloom_tonemap_history", 4, {0, 1, 3})),
                        Cases::add_class<DeferredScene>(scene_spec("deferred_subpass_input_attachments", 5, {0, 2, 4})),
                        Cases::add_class<ForwardPlusScene>(scene_spec("forward_plus_compute_culling", 5, {0, 2, 4})), 0);

}  // namespace

}  // namespace lsw::render::vk
