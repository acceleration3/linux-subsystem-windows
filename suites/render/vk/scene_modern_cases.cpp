#include "render/common/pattern.hpp"
#include "render/vk/scene_kit.hpp"

#include <algorithm>
#include <map>

namespace lsw::render::vk {

namespace {

using namespace sk;

class BindlessScene : public RigScene {
protected:
    std::string caps(Kit& kit) override {
        const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
        if (f.runtimeDescriptorArray == VK_FALSE || f.descriptorBindingPartiallyBound == VK_FALSE || f.descriptorBindingSampledImageUpdateAfterBind == VK_FALSE) {
            return "descriptor indexing runtime arrays with update after bind";
        }
        return f.shaderSampledImageArrayNonUniformIndexing != VK_FALSE ? std::string() : std::string("shaderSampledImageArrayNonUniformIndexing");
    }

    void init(Rig& rig) override {
        TargetDesc td;
        td.width = td.height = 160;
        td.depth = VK_FORMAT_D32_SFLOAT;
        target_ = &rig.make_target(td);
        target_->clear_color[0] = 0.08f;
        target_->clear_color[1] = 0.09f;
        target_->clear_color[2] = 0.12f;
        const auto tint = [](unsigned r, unsigned g, unsigned b, unsigned r2, unsigned g2, unsigned b2, unsigned cell) {
            return Pattern::checker(32, 32, cell, Pattern::pack(r, g, b), Pattern::pack(r2, g2, b2));
        };
        std::vector<std::vector<std::uint8_t>> bank;
        bank.push_back(tint(230, 90, 60, 250, 210, 120, 4));
        bank.push_back(tint(60, 120, 230, 190, 230, 250, 8));
        bank.push_back(tint(60, 200, 110, 20, 90, 50, 2));
        bank.push_back(tint(220, 60, 190, 90, 20, 80, 16));
        bank.push_back(Pattern::gradient(32, 32));
        bank.push_back(Pattern::noise(32, 32, 11));
        bank.push_back(tint(250, 250, 90, 120, 100, 20, 1));
        bank.push_back(tint(90, 240, 230, 10, 70, 80, 4));
        bank.push_back(Pattern::noise(32, 32, 99));
        bank.push_back(tint(255, 255, 255, 30, 30, 30, 8));
        bank.push_back(tint(250, 150, 40, 40, 40, 200, 4));
        bank.push_back(tint(160, 250, 160, 90, 40, 120, 2));
        for (const auto& bytes : bank) {
            textures_.push_back(rig.sampled(32, 32, VK_FORMAT_R8G8B8A8_UNORM, bytes));
        }
        sampler_ = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        const VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo flag_info{};
        flag_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flag_info.bindingCount = 1;
        flag_info.pBindingFlags = &flags;
        set_layout_ = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, kSlots)},
                                  VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT, &flag_info);
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSlots};
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &size;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        rig.vk_ok(rig.kit.api.vkCreateDescriptorPool(rig.kit.device, &pool_info, nullptr, &pool), "vkCreateDescriptorPool");
        Kit* kit = &rig.kit;
        rig.kit.defer([kit, pool] { kit->api.vkDestroyDescriptorPool(kit->device, pool, nullptr); });
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &set_layout_;
        rig.vk_ok(rig.kit.api.vkAllocateDescriptorSets(rig.kit.device, &alloc, &set_), "vkAllocateDescriptorSets");
        for (unsigned slot = 0; slot < 6; ++slot) {
            write_slot(rig, slot, slot);
        }
        layout_ = rig.playout({set_layout_}, 64, VK_SHADER_STAGE_VERTEX_BIT);
        PipelineDesc pipe;
        pipe.vertex = rig.sh("bindless.vert");
        pipe.fragment = rig.sh("bindless.v12.frag");
        svertex_input(pipe);
        pipe.bindings.push_back({1, 20, VK_VERTEX_INPUT_RATE_INSTANCE});
        pipe.attributes.push_back({4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0});
        pipe.attributes.push_back({5, 1, VK_FORMAT_R32_UINT, 16});
        pipe.layout = layout_;
        pipe.pass = target_->clear_pass;
        pipe.depth_test = true;
        pipe_ = rig.pipe(pipe);
        Geometry cube;
        cube.box({0, 0, 0}, {1, 1, 1}, {1, 1, 1});
        cube_ = upload(rig, cube);
    }

    void step(Rig& rig, unsigned frame) override {
        Kit& kit = rig.kit;
        const unsigned usable = frame == 0 ? 6 : 10;
        struct Instance {
            float data[4];
            std::uint32_t material;
        };
        std::vector<Instance> instances;
        for (unsigned k = 0; k < 12; ++k) {
            const float x = -2.4f + 1.6f * static_cast<float>(k % 4);
            const float y = 1.5f - 1.5f * static_cast<float>(k / 4);
            instances.push_back({{x, y, 0.0f, 0.62f}, k % usable});
        }
        const Buffer inst = rig.buffer(instances.data(), instances.size() * sizeof(Instance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const float a = 0.25f + 0.12f * static_cast<float>(frame);
        const Mat4 vp = Mat4::perspective(0.9f, 1.0f, 0.5f, 30.0f, true) * look_at({7.0f * std::sin(a) * 0.4f, 0.4f, 7.2f}, {0, 0, 0});
        rig.run([&](VkCommandBuffer cb) {
            target_->begin(kit, cb, true);
            rig.bind_graphics(cb, pipe_, layout_, {set_});
            rig.push(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, vp.m, 64);
            if (frame == 1) {
                for (unsigned slot = 6; slot < 10; ++slot) {
                    write_slot(rig, slot, slot);
                }
            } else if (frame == 2) {
                write_slot(rig, 1, 10);
                write_slot(rig, 2, 11);
                write_slot(rig, 5, 8);
            }
            rig.bind_vertex(cb, cube_.vb);
            rig.bind_vertex(cb, inst, 0, 1);
            kit.api.vkCmdBindIndexBuffer(cb, cube_.ib.buffer, 0, VK_INDEX_TYPE_UINT16);
            kit.api.vkCmdDrawIndexed(cb, cube_.count, static_cast<std::uint32_t>(instances.size()), 0, 0, 0);
            target_->end(kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    static constexpr std::uint32_t kSlots = 16;

    void write_slot(Rig& rig, unsigned slot, unsigned texture) {
        const VkDescriptorImageInfo info{sampler_, textures_[texture].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set_;
        w.dstBinding = 0;
        w.dstArrayElement = slot;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &info;
        rig.kit.api.vkUpdateDescriptorSets(rig.kit.device, 1, &w, 0, nullptr);
    }

    Target* target_ = nullptr;
    std::vector<Texture> textures_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipe_ = VK_NULL_HANDLE;
    Gpu cube_;
};

struct MeshletData {
    std::vector<float> vertices;
    std::vector<std::uint32_t> info;
    std::vector<float> bounds;
    std::vector<std::uint32_t> meshlet_vertices;
    std::vector<std::uint32_t> triangles;
    std::uint32_t count = 0;
};

MeshletData build_torus_meshlets() {
    constexpr unsigned nu = 32;
    constexpr unsigned nv = 16;
    constexpr unsigned bu = 4;
    constexpr unsigned bv = 4;
    constexpr float big = 1.0f;
    constexpr float small = 0.42f;
    MeshletData out;
    std::vector<V3> positions;
    std::vector<V3> normals;
    for (unsigned i = 0; i < nu; ++i) {
        for (unsigned j = 0; j < nv; ++j) {
            const float u = 6.2831853f * static_cast<float>(i) / nu;
            const float v = 6.2831853f * static_cast<float>(j) / nv;
            const V3 n{std::cos(u) * std::cos(v), std::sin(v), std::sin(u) * std::cos(v)};
            const V3 c{big * std::cos(u), 0.0f, big * std::sin(u)};
            const V3 p = c + n * small;
            positions.push_back(p);
            normals.push_back(n);
            out.vertices.insert(out.vertices.end(), {p.x, p.y, p.z, 0.0f, n.x, n.y, n.z, 0.0f});
        }
    }
    const auto global = [&](unsigned i, unsigned j) { return (i % nu) * nv + (j % nv); };
    for (unsigned a = 0; a < nu / bu; ++a) {
        for (unsigned b = 0; b < nv / bv; ++b) {
            const auto voff = static_cast<std::uint32_t>(out.meshlet_vertices.size());
            const auto toff = static_cast<std::uint32_t>(out.triangles.size());
            V3 center{};
            for (unsigned i = 0; i <= bu; ++i) {
                for (unsigned j = 0; j <= bv; ++j) {
                    const unsigned g = global(a * bu + i, b * bv + j);
                    out.meshlet_vertices.push_back(g);
                    center = center + positions[g];
                }
            }
            center = center * (1.0f / static_cast<float>((bu + 1) * (bv + 1)));
            float radius = 0.0f;
            V3 axis{};
            for (unsigned i = 0; i <= bu; ++i) {
                for (unsigned j = 0; j <= bv; ++j) {
                    const unsigned g = global(a * bu + i, b * bv + j);
                    const V3 d = positions[g] - center;
                    radius = std::max(radius, std::sqrt(dot(d, d)));
                    axis = axis + normals[g];
                }
            }
            axis = normalize(axis);
            float min_dot = 1.0f;
            for (unsigned i = 0; i <= bu; ++i) {
                for (unsigned j = 0; j <= bv; ++j) {
                    min_dot = std::min(min_dot, dot(axis, normals[global(a * bu + i, b * bv + j)]));
                }
            }
            const float cutoff = min_dot > 0.05f ? std::sqrt(1.0f - min_dot * min_dot) : 1.0f;
            std::uint32_t tris = 0;
            for (unsigned i = 0; i < bu; ++i) {
                for (unsigned j = 0; j < bv; ++j) {
                    const std::uint32_t p00 = i * (bv + 1) + j;
                    const std::uint32_t p01 = p00 + 1;
                    const std::uint32_t p10 = p00 + (bv + 1);
                    const std::uint32_t p11 = p10 + 1;
                    out.triangles.push_back(p00 | (p10 << 8) | (p01 << 16));
                    out.triangles.push_back(p01 | (p10 << 8) | (p11 << 16));
                    tris += 2;
                }
            }
            out.info.insert(out.info.end(), {voff, (bu + 1) * (bv + 1), toff, tris});
            out.bounds.insert(out.bounds.end(), {center.x, center.y, center.z, radius, axis.x, axis.y, axis.z, cutoff});
            ++out.count;
        }
    }
    return out;
}

void register_meshlet() {
    CaseSpec spec = scene_spec("meshlet_torus_task_mesh", 160);
    spec.extension("VK_EXT_mesh_shader").min_version(1, 2);
    Cases::add(
        std::move(spec),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            TargetDesc td;
            td.width = td.height = 160;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            target.clear_color[0] = 0.07f;
            target.clear_color[1] = 0.08f;
            target.clear_color[2] = 0.12f;
            const MeshletData data = build_torus_meshlets();
            struct Packed {
                std::uint32_t info[4];
                float sphere[4];
                float cone[4];
            };
            std::vector<Packed> meshlets(data.count);
            for (std::uint32_t k = 0; k < data.count; ++k) {
                std::memcpy(meshlets[k].info, &data.info[k * 4], 16);
                std::memcpy(meshlets[k].sphere, &data.bounds[k * 8], 16);
                std::memcpy(meshlets[k].cone, &data.bounds[k * 8 + 4], 16);
            }
            const Buffer vertices = rig.buffer(data.vertices, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            const Buffer infos = rig.buffer(meshlets, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            const Buffer indices = rig.buffer(data.meshlet_vertices, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            const Buffer tris = rig.buffer(data.triangles, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            const VkShaderStageFlags stages = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;
            const VkDescriptorSetLayout set_layout = rig.dlayout({binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT),
                                                                  binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages),
                                                                  binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT),
                                                                  binding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT)});
            const VkDescriptorSet set = rig.dset(set_layout, {uniform(0, vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), uniform(1, infos, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
                                                              uniform(2, indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), uniform(3, tris, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
            const VkPipelineLayout layout = rig.playout({set_layout}, 96, stages);
            PipelineDesc pipe;
            pipe.fragment = rig.sh("meshlet_lit.frag");
            pipe.extra.push_back({VK_SHADER_STAGE_TASK_BIT_EXT, rig.sh("meshlet_scene.v12.task"), nullptr});
            pipe.extra.push_back({VK_SHADER_STAGE_MESH_BIT_EXT, rig.sh("meshlet_scene.v12.mesh"), nullptr});
            pipe.layout = layout;
            pipe.pass = target.clear_pass;
            pipe.depth_test = true;
            const VkPipeline pipeline = rig.pipe(pipe);
            const V3 eye{1.6f, 2.2f, 3.0f};
            const Mat4 vp = Mat4::perspective(0.95f, 1.0f, 0.3f, 20.0f, true) * look_at(eye, {0.1f, 0.0f, 0.0f});
            float push[24];
            std::memcpy(push, vp.m, 64);
            push[16] = eye.x;
            push[17] = eye.y;
            push[18] = eye.z;
            push[19] = static_cast<float>(data.count);
            push[20] = 1.0f;
            push[21] = 0.0f;
            push[22] = 0.0f;
            push[23] = -0.35f;
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                rig.push(cb, layout, stages, push, 96);
                kit.api.vkCmdDrawMeshTasksEXT(cb, (data.count + 31) / 32, 1, 1);
            }, layout, {set}));
        },
        [](Kit& kit) {
            const auto f = kit.query_features<VkPhysicalDeviceMeshShaderFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT);
            if (f.meshShader == VK_FALSE) {
                return std::string("meshShader");
            }
            return f.taskShader == VK_FALSE ? std::string("taskShader") : std::string();
        });
}

const int registered = (register_meshlet(),
                        Cases::add_class<BindlessScene>(scene_spec("bindless_materials_update_after_bind", 160).min_version(1, 2).frames(3, {0, 1, 2})), 0);

}  // namespace

}  // namespace lsw::render::vk
