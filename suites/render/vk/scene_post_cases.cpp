#include "render/vk/scene_kit.hpp"

#include <algorithm>

namespace lsw::render::vk {

namespace {

using namespace sk;

PipelineDesc fullscreen(Rig& rig, const char* fragment, VkPipelineLayout layout, VkRenderPass pass, std::uint32_t colors = 1) {
    PipelineDesc pipe;
    pipe.vertex = rig.sh("full.vert");
    pipe.fragment = rig.sh(fragment);
    pipe.layout = layout;
    pipe.pass = pass;
    pipe.color_attachments = colors;
    return pipe;
}

void register_ssao() {
    Cases::add(
        scene_spec("ssao_hemisphere_blur", 160),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            constexpr std::uint32_t kSize = 160;
            TargetDesc gd;
            gd.width = gd.height = kSize;
            gd.colors = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
            gd.depth = VK_FORMAT_D32_SFLOAT;
            Target& gbuf = rig.make_target(gd);
            for (float& c : gbuf.clear_color) {
                c = 0.0f;
            }
            TargetDesc fd;
            fd.width = fd.height = kSize;
            Target& ao = rig.make_target(fd);
            Target& blurred = rig.make_target(fd);
            Target& final_target = rig.make_target(fd);
            Geometry g;
            g.floor(0.0f, 4.0f, {0.75f, 0.72f, 0.68f});
            g.quad({-3, 0, -3}, {-3, 4, -3}, {-3, 4, 3}, {-3, 0, 3}, {1, 0, 0}, {0.7f, 0.75f, 0.8f});
            g.quad({3, 0, -3}, {3, 4, -3}, {-3, 4, -3}, {-3, 0, -3}, {0, 0, 1}, {0.8f, 0.72f, 0.7f});
            g.box({-1.9f, 0.5f, -1.8f}, {0.9f, 0.5f, 0.9f}, {0.85f, 0.5f, 0.35f}, 0.2f);
            g.box({-1.9f, 1.4f, -1.8f}, {0.5f, 0.4f, 0.5f}, {0.4f, 0.6f, 0.85f}, 0.7f);
            g.box({0.8f, 0.35f, -2.3f}, {0.7f, 0.35f, 0.5f}, {0.5f, 0.8f, 0.45f}, -0.2f);
            g.box({0.2f, 0.25f, -0.2f}, {0.3f, 0.25f, 0.3f}, {0.9f, 0.8f, 0.3f}, 0.4f);
            g.sphere({1.5f, 0.6f, -0.4f}, 0.6f, {0.9f, 0.4f, 0.5f});
            g.sphere({-0.4f, 0.4f, 0.9f}, 0.4f, {0.5f, 0.45f, 0.9f});
            const Gpu gpu = upload(rig, g);
            const V3 eye{3.4f, 3.2f, 5.0f};
            const float fov = 1.0f;
            const Mat4 view = look_at(eye, {-0.6f, 0.9f, -0.8f});
            const Mat4 vp = Mat4::perspective(fov, 1.0f, 0.3f, 30.0f, true) * view;
            const VkSampler nearest = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            const VkDescriptorSetLayout one = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorSetLayout three = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                                             binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                                             binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorSet set_g = rig.dset(one, {sampled(0, nearest, gbuf.color_[1].view)});
            const VkDescriptorSet set_ao = rig.dset(one, {sampled(0, nearest, ao.color_[0].view)});
            const VkDescriptorSet set_final = rig.dset(three, {sampled(0, nearest, gbuf.color_[0].view), sampled(1, nearest, blurred.color_[0].view),
                                                               sampled(2, nearest, gbuf.color_[1].view)});
            const VkPipelineLayout geo_layout = rig.playout({}, 128, VK_SHADER_STAGE_VERTEX_BIT);
            const VkPipelineLayout ssao_layout = rig.playout({one}, 32, VK_SHADER_STAGE_FRAGMENT_BIT);
            const VkPipelineLayout blur_layout = rig.playout({one}, 16, VK_SHADER_STAGE_FRAGMENT_BIT);
            const VkPipelineLayout final_layout = rig.playout({three});
            PipelineDesc geo;
            geo.vertex = rig.sh("ssao_geo.vert");
            geo.fragment = rig.sh("ssao_geo.frag");
            svertex_input(geo);
            geo.layout = geo_layout;
            geo.pass = gbuf.clear_pass;
            geo.color_attachments = 2;
            geo.depth_test = true;
            const VkPipeline p_geo = rig.pipe(geo);
            const VkPipeline p_ssao = rig.pipe(fullscreen(rig, "ssao.frag", ssao_layout, ao.clear_pass));
            const VkPipeline p_blur = rig.pipe(fullscreen(rig, "ssao_blur.frag", blur_layout, blurred.clear_pass));
            const VkPipeline p_final = rig.pipe(fullscreen(rig, "ssao_final.frag", final_layout, final_target.clear_pass));
            float geo_push[32];
            std::memcpy(geo_push, vp.m, 64);
            std::memcpy(geo_push + 16, view.m, 64);
            const float ssao_push[8] = {std::tan(fov * 0.5f), 1.0f, 0.55f, 0.03f, 0, 0, 0, 0};
            const float texel[4] = {1.0f / kSize, 1.0f / kSize, 0, 0};
            rig.run([&](VkCommandBuffer cb) {
                gbuf.begin(kit, cb, true);
                rig.bind_graphics(cb, p_geo);
                rig.push(cb, geo_layout, VK_SHADER_STAGE_VERTEX_BIT, geo_push, 128);
                draw(rig, cb, gpu);
                gbuf.end(kit, cb);
                render_target_read(rig, cb, gbuf);
                ao.begin(kit, cb, true);
                rig.bind_graphics(cb, p_ssao, ssao_layout, {set_g});
                rig.push(cb, ssao_layout, VK_SHADER_STAGE_FRAGMENT_BIT, ssao_push, 32);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                ao.end(kit, cb);
                render_target_read(rig, cb, ao);
                blurred.begin(kit, cb, true);
                rig.bind_graphics(cb, p_blur, blur_layout, {set_ao});
                rig.push(cb, blur_layout, VK_SHADER_STAGE_FRAGMENT_BIT, texel, 16);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                blurred.end(kit, cb);
                render_target_read(rig, cb, blurred);
                final_target.begin(kit, cb, true);
                rig.bind_graphics(cb, p_final, final_layout, {set_final});
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                final_target.end(kit, cb);
                kit.barrier(cb, blurred.color_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            });
            out.add("final", rig.color(final_target));
            out.add("ao", rig.color(blurred));
        },
        [](Kit& kit) {
            return Cases::need_format(kit, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
                                      "RGBA16F geometry buffer");
        });
}

void register_oit() {
    Cases::add(
        scene_spec("oit_per_pixel_linked_lists", 160).feature("fragmentStoresAndAtomics"),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            constexpr std::uint32_t kSize = 160;
            constexpr std::uint32_t kNodes = kSize * kSize * 6;
            TargetDesc td;
            td.width = td.height = kSize;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            target.clear_color[0] = 0.1f;
            target.clear_color[1] = 0.12f;
            target.clear_color[2] = 0.16f;
            Geometry opaque;
            opaque.floor(-1.0f, 6.0f, {0.55f, 0.55f, 0.6f});
            opaque.box({-1.4f, -0.4f, -1.0f}, {0.6f, 0.6f, 0.6f}, {0.85f, 0.35f, 0.3f}, 0.4f);
            opaque.sphere({1.5f, -0.2f, -2.2f}, 0.8f, {0.35f, 0.75f, 0.45f});
            const Gpu opaque_gpu = upload(rig, opaque);
            struct Layer {
                Geometry geometry;
                float color[4];
            };
            std::vector<Layer> layers(5);
            layers[0].geometry.box({0.1f, 0.0f, 0.3f}, {0.9f, 0.9f, 0.9f}, {1, 1, 1}, 0.6f);
            layers[1].geometry.sphere({-0.9f, 0.2f, 0.9f}, 0.9f, {1, 1, 1});
            layers[2].geometry.box({0.9f, -0.1f, -0.6f}, {0.8f, 0.8f, 0.3f}, {1, 1, 1}, -0.3f);
            layers[3].geometry.sphere({0.0f, 0.1f, -1.8f}, 1.1f, {1, 1, 1});
            layers[4].geometry.box({-0.2f, 0.3f, 1.8f}, {1.4f, 0.2f, 0.7f}, {1, 1, 1}, 1.1f);
            const float colors[5][4] = {{0.9f, 0.55f, 0.2f, 0.45f}, {0.2f, 0.7f, 0.95f, 0.5f}, {0.95f, 0.25f, 0.55f, 0.4f}, {0.3f, 0.95f, 0.45f, 0.35f}, {0.95f, 0.9f, 0.3f, 0.5f}};
            std::vector<Gpu> gpus;
            for (std::size_t k = 0; k < layers.size(); ++k) {
                std::memcpy(layers[k].color, colors[k], 16);
                gpus.push_back(upload(rig, layers[k].geometry));
            }
            const Mat4 vp = Mat4::perspective(0.95f, 1.0f, 0.3f, 30.0f, true) * look_at({1.2f, 2.4f, 5.2f}, {0.0f, -0.1f, -0.5f});
            const Buffer head = rig.blank(kSize * kSize * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            const Buffer nodes = rig.blank(static_cast<VkDeviceSize>(kNodes) * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            const Buffer counter = rig.blank(4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            const VkDescriptorSetLayout collect_set = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                                                   binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                                                   binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorSetLayout resolve_set = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                                                   binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorSet set_collect = rig.dset(collect_set, {uniform(0, head, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), uniform(1, nodes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                                                                       uniform(2, counter, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
            const VkDescriptorSet set_resolve = rig.dset(resolve_set, {uniform(0, head, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), uniform(1, nodes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
            const VkPipelineLayout lit_layout = rig.playout({}, 64, VK_SHADER_STAGE_VERTEX_BIT);
            const VkPipelineLayout collect_layout = rig.playout({collect_set}, 96, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            const VkPipelineLayout resolve_layout = rig.playout({resolve_set}, 16, VK_SHADER_STAGE_FRAGMENT_BIT);
            PipelineDesc lit;
            lit.vertex = rig.sh("sk_lit.vert");
            lit.fragment = rig.sh("sk_lit.frag");
            svertex_input(lit);
            lit.layout = lit_layout;
            lit.pass = target.clear_pass;
            lit.depth_test = true;
            const VkPipeline p_lit = rig.pipe(lit);
            PipelineDesc collect;
            collect.vertex = rig.sh("sk_lit.vert");
            collect.fragment = rig.sh("oit_collect.frag");
            svertex_input(collect);
            collect.layout = collect_layout;
            collect.pass = target.load_pass;
            collect.depth_test = true;
            collect.depth_write = false;
            collect.write_mask = 0;
            const VkPipeline p_collect = rig.pipe(collect);
            PipelineDesc resolve = fullscreen(rig, "oit_resolve.frag", resolve_layout, target.load_pass);
            resolve.blend = true;
            resolve.src_color = VK_BLEND_FACTOR_ONE;
            resolve.dst_color = VK_BLEND_FACTOR_SRC_ALPHA;
            resolve.src_alpha = VK_BLEND_FACTOR_ZERO;
            resolve.dst_alpha = VK_BLEND_FACTOR_ONE;
            const VkPipeline p_resolve = rig.pipe(resolve);
            const std::size_t order[5] = {2, 0, 4, 1, 3};
            rig.run([&](VkCommandBuffer cb) {
                kit.api.vkCmdFillBuffer(cb, head.buffer, 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
                VkMemoryBarrier fill{};
                fill.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                fill.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                fill.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                kit.api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &fill, 0, nullptr, 0, nullptr);
                target.begin(kit, cb, true);
                rig.bind_graphics(cb, p_lit);
                rig.push(cb, lit_layout, VK_SHADER_STAGE_VERTEX_BIT, vp.m, 64);
                draw(rig, cb, opaque_gpu);
                target.end(kit, cb);
                target.begin(kit, cb, false);
                rig.bind_graphics(cb, p_collect, collect_layout, {set_collect});
                for (std::size_t k : order) {
                    float push[24];
                    std::memcpy(push, vp.m, 64);
                    push[16] = static_cast<float>(kSize);
                    push[17] = static_cast<float>(kSize);
                    push[18] = static_cast<float>(kNodes);
                    push[19] = 0.0f;
                    std::memcpy(push + 20, layers[k].color, 16);
                    rig.push(cb, collect_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push, 96);
                    draw(rig, cb, gpus[k]);
                }
                target.end(kit, cb);
                VkMemoryBarrier collected{};
                collected.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                collected.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                collected.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                kit.api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &collected, 0, nullptr, 0, nullptr);
                target.begin(kit, cb, false);
                rig.bind_graphics(cb, p_resolve, resolve_layout, {set_resolve});
                const float params[4] = {static_cast<float>(kSize), 0, 0, 0};
                rig.push(cb, resolve_layout, VK_SHADER_STAGE_FRAGMENT_BIT, params, 16);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                target.end(kit, cb);
            });
            out.add("", rig.color(target));
        });
}

struct MotionObject {
    Gpu gpu;
};

class MotionBlurScene : public RigScene {
protected:
    std::string caps(Kit& kit) override {
        return Cases::first({Cases::need_format(kit, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                                                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
                                                "RG16F velocity target"),
                             Cases::need_format(kit, VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "D32")});
    }

    void init(Rig& rig) override {
        TargetDesc sd;
        sd.colors = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16_SFLOAT};
        sd.depth = VK_FORMAT_D32_SFLOAT;
        scene_ = &rig.make_target(sd);
        for (float& c : scene_->clear_color) {
            c = 0.0f;
        }
        final_ = &rig.make_target(TargetDesc{});
        Geometry floor;
        floor.floor(-1.0f, 14.0f, {0.55f, 0.6f, 0.5f});
        floor.quad({-14, -1, -14}, {-14, 8, -14}, {14, 8, -14}, {14, -1, -14}, {0, 0, 1}, {0.75f, 0.55f, 0.5f});
        floor.quad({14, -1, 14}, {14, 8, 14}, {-14, 8, 14}, {-14, -1, 14}, {0, 0, -1}, {0.5f, 0.65f, 0.8f});
        floor.quad({-14, -1, 14}, {-14, 8, 14}, {-14, 8, -14}, {-14, -1, -14}, {1, 0, 0}, {0.6f, 0.8f, 0.55f});
        floor.quad({14, -1, -14}, {14, 8, -14}, {14, 8, 14}, {14, -1, 14}, {-1, 0, 0}, {0.85f, 0.75f, 0.45f});
        floor.box({-3.0f, 0.0f, -3.0f}, {0.5f, 2.0f, 0.5f}, {0.7f, 0.6f, 0.5f});
        floor.box({3.2f, 0.0f, -4.0f}, {0.5f, 2.0f, 0.5f}, {0.6f, 0.6f, 0.75f});
        Geometry cube;
        cube.box({0, 0, 0}, {0.8f, 0.8f, 0.8f}, {0.9f, 0.45f, 0.3f});
        Geometry ball;
        ball.sphere({0, 0, 0}, 0.7f, {0.35f, 0.8f, 0.5f});
        Geometry slab;
        slab.box({0, 0, 0}, {1.1f, 0.2f, 0.5f}, {0.9f, 0.85f, 0.35f});
        gpu_ = {upload(rig, floor), upload(rig, cube), upload(rig, ball), upload(rig, slab)};
        const VkSampler linear = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        set_layout_ = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                   binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});
        set_ = rig.dset(set_layout_, {sampled(0, linear, scene_->color_[0].view), sampled(1, linear, scene_->color_[1].view)});
        scene_layout_ = rig.playout({}, 128, VK_SHADER_STAGE_VERTEX_BIT);
        blur_layout_ = rig.playout({set_layout_});
        PipelineDesc pipe;
        pipe.vertex = rig.sh("mb_scene.vert");
        pipe.fragment = rig.sh("mb_scene.frag");
        svertex_input(pipe);
        pipe.layout = scene_layout_;
        pipe.pass = scene_->clear_pass;
        pipe.color_attachments = 2;
        pipe.depth_test = true;
        scene_pipe_ = rig.pipe(pipe);
        blur_pipe_ = rig.pipe(fullscreen(rig, "mb_blur.frag", blur_layout_, final_->clear_pass));
        poses_ = pose(0);
        prev_poses_ = poses_;
        prev_camera_ = camera(0);
    }

    void step(Rig& rig, unsigned frame) override {
        Kit& kit = rig.kit;
        poses_ = pose(frame);
        const Mat4 cam = camera(frame);
        rig.run([&](VkCommandBuffer cb) {
            scene_->begin(kit, cb, true);
            rig.bind_graphics(cb, scene_pipe_);
            for (std::size_t k = 0; k < gpu_.size(); ++k) {
                const Mat4 cur = cam * poses_[k];
                const Mat4 prev = prev_camera_ * prev_poses_[k];
                float push[32];
                std::memcpy(push, cur.m, 64);
                std::memcpy(push + 16, prev.m, 64);
                rig.push(cb, scene_layout_, VK_SHADER_STAGE_VERTEX_BIT, push, 128);
                draw(rig, cb, gpu_[k]);
            }
            scene_->end(kit, cb);
            render_target_read(rig, cb, *scene_);
            final_->begin(kit, cb, true);
            rig.bind_graphics(cb, blur_pipe_, blur_layout_, {set_});
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            final_->end(kit, cb);
        });
        prev_poses_ = poses_;
        prev_camera_ = cam;
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*final_)); }

private:
    [[nodiscard]] static Mat4 camera(unsigned frame) {
        const float a = 0.55f + 0.085f * static_cast<float>(frame);
        const V3 eye{6.5f * std::sin(a), 2.2f, 6.5f * std::cos(a)};
        return Mat4::perspective(0.95f, 1.0f, 0.3f, 40.0f, true) * look_at(eye, {0.0f, 0.2f, 0.0f});
    }

    [[nodiscard]] static std::vector<Mat4> pose(unsigned frame) {
        const float t = static_cast<float>(frame);
        return {Mat4::identity(), Mat4::translate(-1.4f, -0.1f, 0.4f) * Mat4::rotate_y(0.45f * t) * Mat4::rotate_x(0.2f * t),
                Mat4::translate(1.2f - 0.5f * t, -0.2f + 0.6f * std::fabs(std::sin(0.8f * t)), 0.6f), Mat4::translate(0.3f, 0.1f, -1.8f + 0.35f * t) * Mat4::rotate_y(0.15f * t)};
    }

    Target* scene_ = nullptr;
    Target* final_ = nullptr;
    std::vector<Gpu> gpu_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout scene_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout blur_layout_ = VK_NULL_HANDLE;
    VkPipeline scene_pipe_ = VK_NULL_HANDLE;
    VkPipeline blur_pipe_ = VK_NULL_HANDLE;
    std::vector<Mat4> poses_;
    std::vector<Mat4> prev_poses_;
    Mat4 prev_camera_;
};

const int registered = (register_ssao(), register_oit(),
                        Cases::add_class<MotionBlurScene>(scene_spec("motion_blur_velocity_buffer", 128).frames(5, {0, 1, 4})), 0);

}  // namespace

}  // namespace lsw::render::vk
