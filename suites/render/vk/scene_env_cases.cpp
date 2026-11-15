#include "render/vk/scene_kit.hpp"

namespace lsw::render::vk {

namespace {

using namespace sk;

struct CubeChain {
    Texture texture;
    std::uint32_t size = 0;
    std::uint32_t mips = 1;
    std::vector<VkFramebuffer> framebuffers;
};

CubeChain make_cube(Rig& rig, VkRenderPass pass, VkFormat format, std::uint32_t size, std::uint32_t mips) {
    CubeChain chain;
    chain.size = size;
    chain.mips = mips;
    TextureDesc desc;
    desc.width = desc.height = size;
    desc.layers = 6;
    desc.mips = mips;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.view_type = VK_IMAGE_VIEW_TYPE_CUBE;
    chain.texture = rig.texture(desc);
    for (std::uint32_t m = 0; m < mips; ++m) {
        for (unsigned f = 0; f < 6; ++f) {
            chain.framebuffers.push_back(rig.framebuffer(pass, {sub_view(rig, chain.texture, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, m, f)},
                                                         size >> m, size >> m));
        }
    }
    return chain;
}

struct Basis {
    float f[4];
    float r[4];
    float u[4];
};

Basis face_basis(unsigned face) {
    V3 f;
    V3 r;
    V3 u;
    cube_basis(face, f, r, u);
    return {{f.x, f.y, f.z, 0}, {r.x, r.y, r.z, 0}, {u.x, u.y, u.z, 0}};
}

void register_pbr() {
    Cases::add(
        scene_spec("pbr_ibl_sphere_grid", 192).tolerance(Tolerance::exact()),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            constexpr std::uint32_t kPrefilterMips = 5;
            TargetDesc td;
            td.width = 192;
            td.height = 128;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            PassDesc hdr;
            hdr.colors = {VK_FORMAT_R16G16B16A16_SFLOAT};
            hdr.clear_color = false;
            const VkRenderPass hdr_pass = make_pass(rig, hdr);
            PassDesc lut_desc;
            lut_desc.colors = {VK_FORMAT_R16G16_SFLOAT};
            lut_desc.clear_color = false;
            const VkRenderPass lut_pass = make_pass(rig, lut_desc);
            const CubeChain env = make_cube(rig, hdr_pass, VK_FORMAT_R16G16B16A16_SFLOAT, 64, 1);
            const CubeChain irradiance = make_cube(rig, hdr_pass, VK_FORMAT_R16G16B16A16_SFLOAT, 16, 1);
            const CubeChain prefilter = make_cube(rig, hdr_pass, VK_FORMAT_R16G16B16A16_SFLOAT, 32, kPrefilterMips);
            TextureDesc lut_texture;
            lut_texture.width = lut_texture.height = 32;
            lut_texture.format = VK_FORMAT_R16G16_SFLOAT;
            lut_texture.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            const Texture lut = rig.texture(lut_texture);
            const VkFramebuffer lut_fb = rig.framebuffer(lut_pass, {lut.view}, 32, 32);
            const VkSampler cube_sampler = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, static_cast<float>(kPrefilterMips - 1), VK_SAMPLER_MIPMAP_MODE_LINEAR);
            const VkSampler lut_sampler = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            const VkDescriptorSetLayout one = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorSetLayout three = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                                             binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                                             binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorSet env_set = rig.dset(one, {sampled(0, cube_sampler, env.texture.view)});
            const VkDescriptorSet pbr_set = rig.dset(three, {sampled(0, cube_sampler, irradiance.texture.view), sampled(1, cube_sampler, prefilter.texture.view),
                                                             sampled(2, lut_sampler, lut.view)});
            const VkPipelineLayout env_layout = rig.playout({}, 48, VK_SHADER_STAGE_FRAGMENT_BIT);
            const VkPipelineLayout conv_layout = rig.playout({one}, 48, VK_SHADER_STAGE_FRAGMENT_BIT);
            const VkPipelineLayout pre_layout = rig.playout({one}, 64, VK_SHADER_STAGE_FRAGMENT_BIT);
            const VkPipelineLayout lut_layout = rig.empty_layout();
            const VkPipelineLayout pbr_layout = rig.playout({three}, 112, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            const VkPipelineLayout sky_layout = rig.playout({one}, 48, VK_SHADER_STAGE_FRAGMENT_BIT);
            const auto full = [&](const char* fragment, VkPipelineLayout layout, VkRenderPass pass) {
                PipelineDesc p;
                p.vertex = rig.sh("full.vert");
                p.fragment = rig.sh(fragment);
                p.layout = layout;
                p.pass = pass;
                return p;
            };
            const VkPipeline p_env = rig.pipe(full("ibl_env.frag", env_layout, hdr_pass));
            const VkPipeline p_irr = rig.pipe(full("ibl_irradiance.frag", conv_layout, hdr_pass));
            const VkPipeline p_pre = rig.pipe(full("ibl_prefilter.frag", pre_layout, hdr_pass));
            const VkPipeline p_lut = rig.pipe(full("ibl_brdf.frag", lut_layout, lut_pass));
            PipelineDesc sky = full("skybox.frag", sky_layout, target.clear_pass);
            const VkPipeline p_sky = rig.pipe(sky);
            PipelineDesc pbr;
            pbr.vertex = rig.sh("pbr_sphere.vert");
            pbr.fragment = rig.sh("pbr.frag");
            svertex_input(pbr);
            pbr.layout = pbr_layout;
            pbr.pass = target.clear_pass;
            pbr.depth_test = true;
            const VkPipeline p_pbr = rig.pipe(pbr);
            Geometry ball;
            ball.sphere({0, 0, 0}, 1.0f, {1, 1, 1}, 24, 40);
            const Gpu sphere = upload(rig, ball);
            const float fov = 0.55f;
            const V3 eye{0.0f, 0.0f, 9.0f};
            const Mat4 vp = Mat4::perspective(fov, 1.5f, 0.5f, 30.0f, true) * look_at(eye, {0, 0, 0});
            Basis camera{{0, 0, -1, 0}, {std::tan(fov * 0.5f) * 1.5f, 0, 0, 0}, {0, std::tan(fov * 0.5f), 0, 0}};
            rig.run([&](VkCommandBuffer cb) {
                for (unsigned f = 0; f < 6; ++f) {
                    const Basis b = face_basis(f);
                    begin_pass(rig, cb, hdr_pass, env.framebuffers[f], 64, 64, {});
                    rig.bind_graphics(cb, p_env);
                    rig.push(cb, env_layout, VK_SHADER_STAGE_FRAGMENT_BIT, &b, 48);
                    kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                    kit.api.vkCmdEndRenderPass(cb);
                }
                for (unsigned f = 0; f < 6; ++f) {
                    const Basis b = face_basis(f);
                    begin_pass(rig, cb, hdr_pass, irradiance.framebuffers[f], 16, 16, {});
                    rig.bind_graphics(cb, p_irr, conv_layout, {env_set});
                    rig.push(cb, conv_layout, VK_SHADER_STAGE_FRAGMENT_BIT, &b, 48);
                    kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                    kit.api.vkCmdEndRenderPass(cb);
                }
                for (std::uint32_t m = 0; m < kPrefilterMips; ++m) {
                    for (unsigned f = 0; f < 6; ++f) {
                        const Basis b = face_basis(f);
                        float push[16];
                        std::memcpy(push, &b, 48);
                        push[12] = static_cast<float>(m) / static_cast<float>(kPrefilterMips - 1);
                        push[13] = push[14] = push[15] = 0.0f;
                        begin_pass(rig, cb, hdr_pass, prefilter.framebuffers[m * 6 + f], 32 >> m, 32 >> m, {});
                        rig.bind_graphics(cb, p_pre, pre_layout, {env_set});
                        rig.push(cb, pre_layout, VK_SHADER_STAGE_FRAGMENT_BIT, push, 64);
                        kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                        kit.api.vkCmdEndRenderPass(cb);
                    }
                }
                begin_pass(rig, cb, lut_pass, lut_fb, 32, 32, {});
                rig.bind_graphics(cb, p_lut);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                kit.api.vkCmdEndRenderPass(cb);
                target.begin(kit, cb, true);
                rig.bind_graphics(cb, p_sky, sky_layout, {env_set});
                rig.push(cb, sky_layout, VK_SHADER_STAGE_FRAGMENT_BIT, &camera, 48);
                kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
                rig.bind_graphics(cb, p_pbr, pbr_layout, {pbr_set});
                for (unsigned row = 0; row < 3; ++row) {
                    for (unsigned col = 0; col < 5; ++col) {
                        float push[28];
                        std::memcpy(push, vp.m, 64);
                        push[16] = -3.0f + 1.5f * static_cast<float>(col);
                        push[17] = 1.5f - 1.5f * static_cast<float>(row);
                        push[18] = 0.0f;
                        push[19] = 0.62f;
                        push[20] = 0.05f + 0.2375f * static_cast<float>(col);
                        push[21] = 1.0f - 0.5f * static_cast<float>(row);
                        push[22] = push[23] = 0.0f;
                        push[24] = eye.x;
                        push[25] = eye.y;
                        push[26] = eye.z;
                        push[27] = 0.0f;
                        rig.push(cb, pbr_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push, 112);
                        draw(rig, cb, sphere);
                    }
                }
                target.end(kit, cb);
            });
            out.add("", rig.color(target));
        },
        [](Kit& kit) {
            return Cases::first({Cases::need_format(kit, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
                                                    "filterable RGBA16F cube target"),
                                 Cases::need_format(kit, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
                                                    "filterable RG16F target")});
        });
}

class ReflectionScene : public RigScene {
protected:
    void init(Rig& rig) override {
        TargetDesc td;
        td.width = td.height = 160;
        td.depth = VK_FORMAT_D32_SFLOAT;
        target_ = &rig.make_target(td);
        target_->clear_color[0] = 0.03f;
        target_->clear_color[1] = 0.04f;
        target_->clear_color[2] = 0.06f;
        PassDesc face_desc;
        face_desc.colors = {VK_FORMAT_R8G8B8A8_UNORM};
        face_desc.depth = VK_FORMAT_D32_SFLOAT;
        face_desc.depth_final = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        face_pass_ = make_pass(rig, face_desc);
        TextureDesc depth_desc;
        depth_desc.width = depth_desc.height = kFace;
        depth_desc.format = VK_FORMAT_D32_SFLOAT;
        depth_desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_ = rig.texture(depth_desc);
        for (CubeChain& cube : cubes_) {
            TextureDesc desc;
            desc.width = desc.height = kFace;
            desc.layers = 6;
            desc.format = VK_FORMAT_R8G8B8A8_UNORM;
            desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            desc.view_type = VK_IMAGE_VIEW_TYPE_CUBE;
            cube.texture = rig.texture(desc);
            for (unsigned f = 0; f < 6; ++f) {
                cube.framebuffers.push_back(rig.framebuffer(
                    face_pass_, {sub_view(rig, cube.texture, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 0, f), depth_.view}, kFace, kFace));
            }
        }
        const VkSampler sampler = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        set_layout_ = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});
        sets_[0] = rig.dset(set_layout_, {sampled(0, sampler, cubes_[0].texture.view)});
        sets_[1] = rig.dset(set_layout_, {sampled(0, sampler, cubes_[1].texture.view)});
        world_layout_ = rig.playout({}, 64, VK_SHADER_STAGE_VERTEX_BIT);
        refl_layout_ = rig.playout({set_layout_}, 80, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        PipelineDesc world;
        world.vertex = rig.sh("sk_lit.vert");
        world.fragment = rig.sh("sk_lit.frag");
        svertex_input(world);
        world.layout = world_layout_;
        world.pass = target_->clear_pass;
        world.depth_test = true;
        world_pipe_ = rig.pipe(world);
        PipelineDesc refl;
        refl.vertex = rig.sh("refl.vert");
        refl.fragment = rig.sh("refl.frag");
        svertex_input(refl);
        refl.layout = refl_layout_;
        refl.pass = target_->clear_pass;
        refl.depth_test = true;
        refl_pipe_ = rig.pipe(refl);
        Geometry a;
        a.sphere(centers_[0], 0.9f, {0.9f, 0.85f, 0.8f}, 20, 32);
        Geometry b;
        b.sphere(centers_[1], 0.7f, {0.8f, 0.85f, 0.95f}, 20, 32);
        spheres_[0] = upload(rig, a);
        spheres_[1] = upload(rig, b);
        Geometry room;
        const float w = 6.0f;
        const float h = 4.0f;
        room.quad({-w, 0, -w}, {w, 0, -w}, {w, 0, w}, {-w, 0, w}, {0, 1, 0}, {0.7f, 0.68f, 0.62f});
        room.quad({-w, h, w}, {w, h, w}, {w, h, -w}, {-w, h, -w}, {0, -1, 0}, {0.8f, 0.8f, 0.85f});
        room.quad({-w, 0, -w}, {-w, h, -w}, {w, h, -w}, {w, 0, -w}, {0, 0, 1}, {0.85f, 0.35f, 0.3f});
        room.quad({w, 0, w}, {w, h, w}, {-w, h, w}, {-w, 0, w}, {0, 0, -1}, {0.3f, 0.5f, 0.85f});
        room.quad({-w, 0, w}, {-w, h, w}, {-w, h, -w}, {-w, 0, -w}, {1, 0, 0}, {0.35f, 0.8f, 0.4f});
        room.quad({w, 0, -w}, {w, h, -w}, {w, h, w}, {w, 0, w}, {-1, 0, 0}, {0.9f, 0.8f, 0.3f});
        room.box({-3.6f, 1.0f, 3.6f}, {0.4f, 1.0f, 0.4f}, {0.8f, 0.8f, 0.8f});
        room.box({3.6f, 1.0f, -3.6f}, {0.4f, 1.0f, 0.4f}, {0.8f, 0.8f, 0.8f});
        static_world_ = room;
        for (CubeChain& cube : cubes_) {
            rig.run([&](VkCommandBuffer cb) {
                for (unsigned f = 0; f < 6; ++f) {
                    begin_pass(rig, cb, face_pass_, cube.framebuffers[f], kFace, kFace, {clear_color(0.08f, 0.03f, 0.12f, 1.0f), clear_depth()});
                    rig.kit.api.vkCmdEndRenderPass(cb);
                }
            });
        }
    }

    void step(Rig& rig, unsigned frame) override {
        Kit& kit = rig.kit;
        Geometry world = static_world_;
        for (unsigned k = 0; k < 3; ++k) {
            const float a = 0.8f * static_cast<float>(frame) + 2.0944f * static_cast<float>(k);
            world.box({3.0f * std::cos(a), 1.0f + 0.3f * static_cast<float>(k), 3.0f * std::sin(a) - 0.3f}, {0.5f, 0.5f + 0.15f * static_cast<float>(k), 0.5f},
                      {k == 0 ? 0.95f : 0.3f, k == 1 ? 0.9f : 0.4f, k == 2 ? 0.95f : 0.35f}, a);
        }
        const Gpu gpu = upload(rig, world);
        const V3 eye{0.0f, 2.2f, 5.4f};
        const Mat4 camera = Mat4::perspective(1.15f, 1.0f, 0.2f, 40.0f, true) * look_at(eye, {0.0f, 1.0f, 0.0f});
        const Mat4 projection = Mat4::perspective(1.5707964f, 1.0f, 0.1f, 40.0f, true);
        const auto world_draw = [&](VkCommandBuffer cb, const Mat4& vp) {
            rig.bind_graphics(cb, world_pipe_);
            rig.push(cb, world_layout_, VK_SHADER_STAGE_VERTEX_BIT, vp.m, 64);
            draw(rig, cb, gpu);
        };
        const auto sphere_draw = [&](VkCommandBuffer cb, const Mat4& vp, V3 from, unsigned sphere, unsigned cube) {
            float push[20];
            std::memcpy(push, vp.m, 64);
            push[16] = from.x;
            push[17] = from.y;
            push[18] = from.z;
            push[19] = 0.0f;
            rig.bind_graphics(cb, refl_pipe_, refl_layout_, {sets_[cube]});
            rig.push(cb, refl_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push, 80);
            draw(rig, cb, spheres_[sphere]);
        };
        rig.run([&](VkCommandBuffer cb) {
            for (unsigned probe = 0; probe < 2; ++probe) {
                const unsigned other = 1 - probe;
                for (unsigned f = 0; f < 6; ++f) {
                    const Mat4 vp = projection * cube_face_view(f, centers_[probe]);
                    begin_pass(rig, cb, face_pass_, cubes_[probe].framebuffers[f], kFace, kFace, {clear_color(0.08f, 0.03f, 0.12f, 1.0f), clear_depth()});
                    world_draw(cb, vp);
                    sphere_draw(cb, vp, centers_[probe], other, other);
                    kit.api.vkCmdEndRenderPass(cb);
                }
            }
            target_->begin(kit, cb, true);
            world_draw(cb, camera);
            sphere_draw(cb, camera, eye, 0, 0);
            sphere_draw(cb, camera, eye, 1, 1);
            target_->end(kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    static constexpr std::uint32_t kFace = 96;
    Target* target_ = nullptr;
    VkRenderPass face_pass_ = VK_NULL_HANDLE;
    Texture depth_;
    CubeChain cubes_[2];
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet sets_[2] = {};
    VkPipelineLayout world_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout refl_layout_ = VK_NULL_HANDLE;
    VkPipeline world_pipe_ = VK_NULL_HANDLE;
    VkPipeline refl_pipe_ = VK_NULL_HANDLE;
    Gpu spheres_[2];
    Geometry static_world_;
    const V3 centers_[2] = {{-1.7f, 1.0f, -0.6f}, {1.6f, 0.8f, 0.5f}};
};

const int registered = (register_pbr(), Cases::add_class<ReflectionScene>(scene_spec("dynamic_cube_reflection_feedback", 160).frames(4, {0, 1, 3})), 0);

}  // namespace

}  // namespace lsw::render::vk
