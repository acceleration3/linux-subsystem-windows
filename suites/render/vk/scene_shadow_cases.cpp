#include "render/vk/scene_kit.hpp"

#include <algorithm>

namespace lsw::render::vk {

namespace {

using namespace sk;

void register_csm() {
    Cases::add(
        scene_spec("csm_three_cascades", 160),
        [](Rig& rig, CaptureSet& out) {
            constexpr std::uint32_t kMap = 256;
            Target& target = rig.make_target([] {
                TargetDesc td;
                td.width = td.height = 160;
                td.depth = VK_FORMAT_D32_SFLOAT;
                return td;
            }());
            target.clear_color[0] = 0.55f;
            target.clear_color[1] = 0.65f;
            target.clear_color[2] = 0.8f;
            Geometry g;
            g.floor(0.0f, 90.0f, {0.45f, 0.55f, 0.4f}, 1.0f);
            const float zs[] = {3.0f, -1.0f, -4.5f, -8.0f, -12.0f, -17.0f, -23.0f, -30.0f, -38.0f, -48.0f};
            for (unsigned k = 0; k < 10; ++k) {
                const float x = (k % 2 == 0 ? -1.0f : 1.0f) * (2.0f + 0.9f * static_cast<float>(k % 4));
                const float s = 0.9f + 0.18f * static_cast<float>(k);
                const V3 color = {0.9f - 0.07f * static_cast<float>(k), 0.45f + 0.04f * static_cast<float>(k), 0.3f + 0.05f * static_cast<float>(k)};
                if (k % 3 == 2) {
                    g.sphere({x, s, zs[k]}, s, color);
                } else {
                    g.box({x, s * 1.5f, zs[k]}, {s, s * 1.5f, s}, color, 0.4f * static_cast<float>(k));
                }
                g.box({-x * 0.3f, 0.5f * s, zs[k] - 2.0f}, {0.5f * s, 0.5f * s, 0.5f * s}, {0.8f, 0.8f, 0.85f}, 0.9f);
            }
            const Gpu gpu = upload(rig, g);
            const V3 eye{0.0f, 4.2f, 9.0f};
            const V3 forward = normalize({0.0f, -0.3f, -1.0f});
            const V3 right = normalize(cross(forward, {0, 1, 0}));
            const V3 up = cross(right, forward);
            const float fov = 1.05f;
            const Mat4 camera = Mat4::perspective(fov, 1.0f, 0.5f, 90.0f, true) * look_at(eye, eye + forward);
            const float splits[4] = {0.5f, 5.0f, 16.0f, 70.0f};
            const V3 light_dir = normalize({0.6f, -0.9f, 0.5f});
            const V3 lr = normalize(cross(light_dir, {0, 1, 0}));
            const V3 lu = cross(lr, light_dir);
            const Mat4 light_view = view_from_basis(lr, lu, light_dir, {0, 0, 0});
            struct Cascades {
                float vp[3][16];
                float splits[4];
                float light[4];
            } cascades{};
            Mat4 cascade_vp[3];
            const float tan_half = std::tan(fov * 0.5f);
            for (unsigned c = 0; c < 3; ++c) {
                float lo[3] = {1e9f, 1e9f, 1e9f};
                float hi[3] = {-1e9f, -1e9f, -1e9f};
                for (float d : {splits[c], splits[c + 1]}) {
                    for (float sx : {-1.0f, 1.0f}) {
                        for (float sy : {-1.0f, 1.0f}) {
                            const V3 corner = eye + forward * d + right * (sx * d * tan_half) + up * (sy * d * tan_half);
                            const V3 l = transform_point(light_view, corner);
                            lo[0] = std::min(lo[0], l.x);
                            lo[1] = std::min(lo[1], l.y);
                            lo[2] = std::min(lo[2], l.z);
                            hi[0] = std::max(hi[0], l.x);
                            hi[1] = std::max(hi[1], l.y);
                            hi[2] = std::max(hi[2], l.z);
                        }
                    }
                }
                cascade_vp[c] = ortho_vk(lo[0], hi[0], lo[1], hi[1], -hi[2] - 70.0f, -lo[2] + 1.0f) * light_view;
                std::memcpy(cascades.vp[c], cascade_vp[c].m, 64);
            }
            cascades.splits[0] = splits[1];
            cascades.splits[1] = splits[2];
            cascades.splits[2] = splits[3];
            cascades.light[0] = light_dir.x;
            cascades.light[1] = light_dir.y;
            cascades.light[2] = light_dir.z;
            const Buffer ubo = rig.buffer(&cascades, sizeof cascades, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            TextureDesc map;
            map.width = map.height = kMap;
            map.layers = 3;
            map.format = VK_FORMAT_D32_SFLOAT;
            map.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            map.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            map.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            const Texture shadow = rig.texture(map);
            PassDesc pass_desc;
            pass_desc.depth = VK_FORMAT_D32_SFLOAT;
            const VkRenderPass shadow_pass = make_pass(rig, pass_desc);
            VkFramebuffer fbs[3];
            for (unsigned c = 0; c < 3; ++c) {
                fbs[c] = rig.framebuffer(shadow_pass, {sub_view(rig, shadow, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, 0, c)}, kMap, kMap);
            }
            VkSamplerCreateInfo sci{};
            sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
            sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            sci.compareEnable = VK_TRUE;
            sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            sci.maxAnisotropy = 1.0f;
            const VkSampler sampler = rig.sampler(sci);
            const VkDescriptorSetLayout set_layout = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
                                                                  binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorSet set = rig.dset(set_layout, {uniform(0, ubo), sampled(1, sampler, shadow.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)});
            const VkPipelineLayout lit_layout = rig.playout({set_layout}, 64, VK_SHADER_STAGE_VERTEX_BIT);
            const VkPipelineLayout depth_layout = rig.playout({}, 64, VK_SHADER_STAGE_VERTEX_BIT);
            PipelineDesc depth_pipe;
            depth_pipe.vertex = rig.sh("sk_depth.vert");
            svertex_input(depth_pipe);
            depth_pipe.layout = depth_layout;
            depth_pipe.pass = shadow_pass;
            depth_pipe.color_attachments = 0;
            depth_pipe.depth_test = true;
            depth_pipe.depth_bias = true;
            depth_pipe.bias_constant = 1.5f;
            depth_pipe.bias_slope = 2.0f;
            const VkPipeline depth_pipeline = rig.pipe(depth_pipe);
            PipelineDesc lit;
            lit.vertex = rig.sh("csm_lit.vert");
            lit.fragment = rig.sh("csm_lit.frag");
            svertex_input(lit);
            lit.layout = lit_layout;
            lit.pass = target.clear_pass;
            lit.depth_test = true;
            const VkPipeline lit_pipeline = rig.pipe(lit);
            rig.run([&](VkCommandBuffer cb) {
                for (unsigned c = 0; c < 3; ++c) {
                    begin_pass(rig, cb, shadow_pass, fbs[c], kMap, kMap, {clear_depth()});
                    rig.bind_graphics(cb, depth_pipeline);
                    rig.push(cb, depth_layout, VK_SHADER_STAGE_VERTEX_BIT, cascade_vp[c].m, 64);
                    draw(rig, cb, gpu);
                    rig.kit.api.vkCmdEndRenderPass(cb);
                }
                target.begin(rig.kit, cb, true);
                rig.bind_graphics(cb, lit_pipeline, lit_layout, {set});
                rig.push(cb, lit_layout, VK_SHADER_STAGE_VERTEX_BIT, camera.m, 64);
                draw(rig, cb, gpu);
                target.end(rig.kit, cb);
            });
            out.add("", rig.color(target));
        },
        [](Kit& kit) {
            return Cases::need_format(kit, VK_FORMAT_D32_SFLOAT,
                                      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
                                      "sampled linear-filtered D32");
        });
}

void register_omni() {
    Cases::add(
        scene_spec("omni_shadow_cube", 160),
        [](Rig& rig, CaptureSet& out) {
            constexpr std::uint32_t kFace = 128;
            constexpr float kFar = 24.0f;
            Target& target = rig.make_target([] {
                TargetDesc td;
                td.width = td.height = 160;
                td.depth = VK_FORMAT_D32_SFLOAT;
                return td;
            }());
            target.clear_color[0] = target.clear_color[1] = target.clear_color[2] = 0.02f;
            Geometry g;
            const float h = 4.0f;
            const float w = 7.0f;
            g.quad({-w, 0, -w}, {w, 0, -w}, {w, 0, w}, {-w, 0, w}, {0, 1, 0}, {0.6f, 0.55f, 0.5f});
            g.quad({-w, h, w}, {w, h, w}, {w, h, -w}, {-w, h, -w}, {0, -1, 0}, {0.8f, 0.8f, 0.8f});
            g.quad({-w, 0, -w}, {-w, h, -w}, {w, h, -w}, {w, 0, -w}, {0, 0, 1}, {0.75f, 0.45f, 0.4f});
            g.quad({w, 0, w}, {w, h, w}, {-w, h, w}, {-w, 0, w}, {0, 0, -1}, {0.4f, 0.6f, 0.75f});
            g.quad({-w, 0, w}, {-w, h, w}, {-w, h, -w}, {-w, 0, -w}, {1, 0, 0}, {0.5f, 0.75f, 0.5f});
            g.quad({w, 0, -w}, {w, h, -w}, {w, h, w}, {w, 0, w}, {-1, 0, 0}, {0.75f, 0.7f, 0.4f});
            g.box({-2.8f, 0.9f, -1.5f}, {0.9f, 0.9f, 0.9f}, {0.9f, 0.5f, 0.3f}, 0.5f);
            g.box({2.4f, 1.3f, 0.8f}, {0.6f, 1.3f, 0.6f}, {0.4f, 0.5f, 0.9f}, -0.3f);
            g.box({0.2f, 0.4f, -4.2f}, {1.4f, 0.4f, 0.7f}, {0.9f, 0.85f, 0.3f}, 0.1f);
            g.sphere({-0.9f, 1.4f, 2.4f}, 0.8f, {0.9f, 0.4f, 0.7f});
            g.box({0.8f, 2.4f, 0.2f}, {0.35f, 0.35f, 0.35f}, {0.95f, 0.95f, 0.95f}, 0.7f);
            const Gpu gpu = upload(rig, g);
            const V3 light{0.3f, 2.9f, -0.4f};
            TextureDesc cube;
            cube.width = cube.height = kFace;
            cube.layers = 6;
            cube.format = VK_FORMAT_R32_SFLOAT;
            cube.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            cube.view_type = VK_IMAGE_VIEW_TYPE_CUBE;
            const Texture distances = rig.texture(cube);
            TextureDesc depth_desc;
            depth_desc.width = depth_desc.height = kFace;
            depth_desc.format = VK_FORMAT_D32_SFLOAT;
            depth_desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            const Texture depth = rig.texture(depth_desc);
            PassDesc pass_desc;
            pass_desc.colors = {VK_FORMAT_R32_SFLOAT};
            pass_desc.depth = VK_FORMAT_D32_SFLOAT;
            pass_desc.depth_final = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            const VkRenderPass face_pass = make_pass(rig, pass_desc);
            VkFramebuffer fbs[6];
            for (unsigned f = 0; f < 6; ++f) {
                fbs[f] = rig.framebuffer(face_pass, {sub_view(rig, distances, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 0, f), depth.view}, kFace, kFace);
            }
            const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            const VkDescriptorSetLayout set_layout = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});
            const VkDescriptorSet set = rig.dset(set_layout, {sampled(0, sampler, distances.view)});
            const VkPipelineLayout face_layout = rig.playout({}, 80, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            const VkPipelineLayout lit_layout = rig.playout({set_layout}, 96, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
            PipelineDesc face;
            face.vertex = rig.sh("omni_depth.vert");
            face.fragment = rig.sh("omni_depth.frag");
            svertex_input(face);
            face.layout = face_layout;
            face.pass = face_pass;
            face.depth_test = true;
            const VkPipeline face_pipeline = rig.pipe(face);
            PipelineDesc lit;
            lit.vertex = rig.sh("omni_lit.vert");
            lit.fragment = rig.sh("omni_lit.frag");
            svertex_input(lit);
            lit.layout = lit_layout;
            lit.pass = target.clear_pass;
            lit.depth_test = true;
            const VkPipeline lit_pipeline = rig.pipe(lit);
            const Mat4 projection = Mat4::perspective(1.5707964f, 1.0f, 0.1f, kFar, true);
            const V3 eye{5.2f, 2.3f, 6.2f};
            const Mat4 camera = Mat4::perspective(1.25f, 1.0f, 0.2f, 40.0f, true) * look_at(eye, {-0.4f, 1.1f, -0.8f});
            rig.run([&](VkCommandBuffer cb) {
                for (unsigned f = 0; f < 6; ++f) {
                    const Mat4 vp = projection * cube_face_view(f, light);
                    float push[20];
                    std::memcpy(push, vp.m, 64);
                    push[16] = light.x;
                    push[17] = light.y;
                    push[18] = light.z;
                    push[19] = kFar;
                    begin_pass(rig, cb, face_pass, fbs[f], kFace, kFace, {clear_color(1, 1, 1, 1), clear_depth()});
                    rig.bind_graphics(cb, face_pipeline);
                    rig.push(cb, face_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push, 80);
                    draw(rig, cb, gpu);
                    rig.kit.api.vkCmdEndRenderPass(cb);
                }
                float push[24];
                std::memcpy(push, camera.m, 64);
                push[16] = light.x;
                push[17] = light.y;
                push[18] = light.z;
                push[19] = kFar;
                push[20] = eye.x;
                push[21] = eye.y;
                push[22] = eye.z;
                push[23] = 0.0f;
                target.begin(rig.kit, cb, true);
                rig.bind_graphics(cb, lit_pipeline, lit_layout, {set});
                rig.push(cb, lit_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push, 96);
                draw(rig, cb, gpu);
                target.end(rig.kit, cb);
            });
            out.add("", rig.color(target));
        },
        [](Kit& kit) {
            return Cases::first({Cases::need_format(kit, VK_FORMAT_R32_SFLOAT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "R32F cube target"),
                                 Cases::need_format(kit, VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "D32")});
        });
}

const int registered = (register_csm(), register_omni(), 0);

}  // namespace

}  // namespace lsw::render::vk
