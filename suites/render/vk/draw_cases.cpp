#include "render/vk/quadkit.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.09f, 1.0f};

std::vector<Vertex2> triangle_set() {
    std::vector<Vertex2> v;
    for (unsigned t = 0; t < 4; ++t) {
        v.push_back(vertex(-1.0f, -1.0f, t * 2));
        v.push_back(vertex(1.0f, -1.0f, t * 2 + 1));
        v.push_back(vertex(0.0f, 1.0f, t * 2 + 2));
    }
    return v;
}

struct DrawEnv {
    Target* target = nullptr;
    Buffer vertices;
    Buffer indices;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

DrawEnv make_env(Rig& rig, const char* vertex = "instidx.vert") {
    DrawEnv env;
    env.target = &rig.make_target(TargetDesc{});
    std::copy(kClear, kClear + 4, env.target->clear_color);
    env.vertices = rig.buffer(triangle_set(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    PipelineDesc pipe = rig.pos2c(*env.target);
    pipe.vertex = rig.sh(vertex);
    env.pipeline = rig.pipe(pipe);
    return env;
}

Image render_env(Rig& rig, DrawEnv& env, const std::function<void(VkCommandBuffer)>& commands) {
    return rig.draw(*env.target, env.pipeline, &env.vertices, commands);
}

void add_draw(const std::string& name, std::function<void(Rig&, CaptureSet&)> body, std::function<std::string(Kit&)> caps = {},
              const char* feature = nullptr, const char* extension = nullptr, unsigned minor = 0) {
    CaseSpec spec("draw", name);
    spec.tolerance(Tolerance::exact());
    if (name == "xfb_capture_then_draw" || name == "xfb_capture_indirect_byte_count") {
        spec.same_as({name == "xfb_capture_then_draw" ? "draw.xfb_capture_indirect_byte_count" : "draw.xfb_capture_then_draw"}, name == "xfb_capture_then_draw" ? "draws the captured vertices with an explicit count, the same triangles xfb_capture_indirect_byte_count draws from the byte counter; the row fails if the capture is wrong"
                                                      : "draws from the transform feedback byte counter and must reproduce the explicit count draw of xfb_capture_then_draw exactly");
    }
    if (feature != nullptr) {
        spec.feature(feature);
    }
    if (extension != nullptr) {
        spec.extension(extension);
    }
    if (minor != 0) {
        spec.min_version(1, minor);
    }
    Cases::add(std::move(spec), std::move(body), std::move(caps));
}

std::string need_fn_indirect_count(Kit& kit) {
    const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    return f.drawIndirectCount != VK_FALSE ? std::string() : std::string("drawIndirectCount");
}

void register_direct() {
    add_draw("direct_first_vertex_instance", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdDraw(cb, 3, 5, 3, 2);
            rig.kit.api.vkCmdDraw(cb, 3, 3, 6, 9);
            rig.kit.api.vkCmdDraw(cb, 6, 2, 0, 13);
        }));
    });
    add_draw("indexed_first_index_vertex_offset", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<std::uint16_t> indices = {9, 9, 9, 0, 1, 2, 3, 4, 5, 0, 1, 2, 2, 1, 0};
        env.indices = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, env.indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.kit.api.vkCmdDrawIndexed(cb, 3, 3, 3, 0, 0);
            rig.kit.api.vkCmdDrawIndexed(cb, 6, 2, 6, 6, 3);
        }));
    });
    add_draw("indexed_negative_vertex_offset", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<std::uint16_t> indices = {6, 7, 8, 9, 10, 11};
        env.indices = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, env.indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.kit.api.vkCmdDrawIndexed(cb, 3, 4, 0, -6, 0);
            rig.kit.api.vkCmdDrawIndexed(cb, 3, 4, 3, -6, 4);
        }));
    });
    add_draw("indexed_32bit_with_offset", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<std::uint32_t> indices = {0, 0, 3, 4, 5, 6, 7, 8, 0, 1, 2};
        env.indices = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, env.indices.buffer, 8, VK_INDEX_TYPE_UINT32);
            rig.kit.api.vkCmdDrawIndexed(cb, 6, 3, 0, 0, 0);
            rig.kit.api.vkCmdDrawIndexed(cb, 3, 3, 6, 0, 3);
        }));
    }, {}, "fullDrawIndexUint32");
}

void register_indirect() {
    add_draw("indirect_single", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<VkDrawIndirectCommand> commands = {{3, 1, 0, 0}, {3, 6, 3, 1}, {3, 4, 6, 8}};
        const Buffer ib = rig.buffer(commands, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdDrawIndirect(cb, ib.buffer, sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
            rig.kit.api.vkCmdDrawIndirect(cb, ib.buffer, 0, 1, 0);
        }));
    });
    add_draw("indirect_multi_stride", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        struct Padded {
            VkDrawIndirectCommand command;
            std::uint32_t pad[3];
        };
        const std::vector<Padded> commands = {{{3, 2, 0, 0}, {}}, {{3, 3, 3, 2}, {}}, {{3, 4, 6, 5}, {}}, {{3, 3, 9 - 3, 9}, {}}};
        const Buffer ib = rig.buffer(commands, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDrawIndirect(cb, ib.buffer, 0, 4, sizeof(Padded)); }));
    }, {}, "multiDrawIndirect");
    add_draw("indexed_indirect_multi", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<std::uint16_t> indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        env.indices = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        const std::vector<VkDrawIndexedIndirectCommand> commands = {{3, 2, 0, 0, 0}, {3, 3, 3, 0, 2}, {3, 4, 6, -3, 5}, {3, 3, 9, 0, 9}};
        const Buffer ib = rig.buffer(commands, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, env.indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.kit.api.vkCmdDrawIndexedIndirect(cb, ib.buffer, 0, 4, sizeof(VkDrawIndexedIndirectCommand));
        }));
    }, {}, "multiDrawIndirect");
    add_draw("indirect_first_instance", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<VkDrawIndirectCommand> commands = {{3, 3, 0, 3}, {3, 4, 3, 7}, {3, 2, 6, 12}};
        const Buffer ib = rig.buffer(commands, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 3; ++i) {
                rig.kit.api.vkCmdDrawIndirect(cb, ib.buffer, i * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
            }
        }));
    }, {}, "drawIndirectFirstInstance");
    add_draw("indirect_count", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<VkDrawIndirectCommand> commands = {{3, 2, 0, 0}, {3, 3, 3, 2}, {3, 4, 6, 5}, {3, 3, 9, 9}, {3, 1, 0, 14}, {3, 1, 3, 15}};
        const Buffer ib = rig.buffer(commands, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        const std::vector<std::uint32_t> counts = {0, 3, 9};
        const Buffer cb_buf = rig.buffer(counts, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdDrawIndirectCount(cb, ib.buffer, 0, cb_buf.buffer, 4, 6, sizeof(VkDrawIndirectCommand));
        }));
    }, need_fn_indirect_count, "multiDrawIndirect", nullptr, 2);
    add_draw("indexed_indirect_count", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<std::uint16_t> indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        env.indices = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        const std::vector<VkDrawIndexedIndirectCommand> commands = {{3, 2, 0, 0, 0}, {3, 3, 3, 0, 2}, {3, 4, 6, 0, 5}, {3, 3, 9, 0, 9}};
        const Buffer ib = rig.buffer(commands, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        const std::vector<std::uint32_t> counts = {2};
        const Buffer cb_buf = rig.buffer(counts, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, env.indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.kit.api.vkCmdDrawIndexedIndirectCount(cb, ib.buffer, 0, cb_buf.buffer, 0, 4, sizeof(VkDrawIndexedIndirectCommand));
        }));
    }, need_fn_indirect_count, "multiDrawIndirect", nullptr, 2);
}

void register_multi_draw() {
    auto caps = [](Kit& kit) -> std::string {
        const auto f = kit.query_features<VkPhysicalDeviceMultiDrawFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT);
        return f.multiDraw != VK_FALSE ? std::string() : std::string("multiDraw");
    };
    add_draw("multi_draw_ext", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const VkMultiDrawInfoEXT infos[3] = {{0, 3}, {3, 3}, {6, 6}};
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdDrawMultiEXT(cb, 3, infos, 4, 0, sizeof(VkMultiDrawInfoEXT));
            rig.kit.api.vkCmdDrawMultiEXT(cb, 2, infos, 3, 4, sizeof(VkMultiDrawInfoEXT));
        }));
    }, caps, nullptr, "VK_EXT_multi_draw");
    add_draw("multi_draw_indexed_ext", [](Rig& rig, CaptureSet& out) {
        DrawEnv env = make_env(rig);
        const std::vector<std::uint16_t> indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        env.indices = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        const VkMultiDrawIndexedInfoEXT infos[3] = {{0, 3, 0}, {3, 3, 0}, {6, 6, -3}};
        out.add("", render_env(rig, env, [&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindIndexBuffer(cb, env.indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            rig.kit.api.vkCmdDrawMultiIndexedEXT(cb, 3, infos, 3, 0, sizeof(VkMultiDrawIndexedInfoEXT), nullptr);
        }));
    }, caps, nullptr, "VK_EXT_multi_draw");
}

void register_gpu_generated() {
    add_draw("indirect_from_compute_cull", [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        std::vector<float> items;
        for (int i = 0; i < 32; ++i) {
            items.push_back(-0.85f + 0.24f * static_cast<float>(i % 8));
            items.push_back(-0.8f + 0.5f * static_cast<float>(i / 8));
            items.push_back(0.09f);
            items.push_back(static_cast<float>(i) * 0.137f + 0.01f);
        }
        const Buffer src = rig.buffer(items.data(), items.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const Buffer dst = rig.blank(items.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const VkDrawIndirectCommand initial{3, 0, 0, 0};
        const Buffer cmd = rig.buffer(&initial, sizeof initial, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                                              {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}});
        std::vector<DescriptorWrite> writes(3);
        const Buffer* buffers[3] = {&src, &dst, &cmd};
        for (unsigned i = 0; i < 3; ++i) {
            writes[i].binding = i;
            writes[i].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].buffer = {buffers[i]->buffer, 0, VK_WHOLE_SIZE};
        }
        const VkDescriptorSet set = rig.dset(set_layout, writes);
        const VkPipelineLayout compute_layout = rig.playout({set_layout});
        const VkPipeline compute = rig.compute("cull.comp", compute_layout);
        const std::vector<Vertex2> tri = {vertex(-1, -1, 0), vertex(1, -1, 1), vertex(0, 1, 2)};
        const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.vertex = rig.sh("inst4.vert");
        pipe.bindings = {{0, sizeof(Vertex2), VK_VERTEX_INPUT_RATE_VERTEX}, {1, 16, VK_VERTEX_INPUT_RATE_INSTANCE}};
        pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0}};
        const VkPipeline pipeline = rig.pipe(pipe);
        rig.run([&](VkCommandBuffer cb) {
            rig.kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute);
            rig.kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout, 0, 1, &set, 0, nullptr);
            rig.kit.api.vkCmdDispatch(cb, 2, 1, 1);
            rig.barrier_all(cb);
        });
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            rig.bind_vertex(cb, dst, 0, 1);
            rig.kit.api.vkCmdDrawIndirect(cb, cmd.buffer, 0, 1, sizeof(VkDrawIndirectCommand));
        }));
    });
}

void register_feedback_and_conditional() {
    auto xfb_caps = [](Kit& kit) -> std::string {
        const auto f = kit.query_features<VkPhysicalDeviceTransformFeedbackFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT);
        return f.transformFeedback != VK_FALSE ? std::string() : std::string("transformFeedback");
    };
    for (int indirect = 0; indirect < 2; ++indirect) {
        add_draw(indirect != 0 ? "xfb_capture_indirect_byte_count" : "xfb_capture_then_draw", [indirect](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            const std::vector<Vertex2> source = {vertex(-0.7f, -0.7f, 0), vertex(0.2f, -0.6f, 1), vertex(-0.2f, 0.4f, 2),
                                                 vertex(0.0f, 0.1f, 3),   vertex(0.9f, 0.0f, 4),  vertex(0.6f, 0.9f, 5)};
            const Buffer vb = rig.buffer(source, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const Buffer captured = rig.blank(24 * 6, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const Buffer counter = rig.blank(16, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
            PipelineDesc capture = rig.pos2c(target);
            capture.vertex = rig.sh("xfb.vert");
            capture.fragment = VK_NULL_HANDLE;
            capture.rasterizer_discard = true;
            const VkPipeline p_capture = rig.pipe(capture);
            const VkPipeline p_show = rig.pipe(rig.pos2c(target));
            rig.run([&](VkCommandBuffer cb) {
                target.begin(rig.kit, cb, true);
                rig.bind_graphics(cb, p_capture);
                rig.bind_vertex(cb, vb);
                const VkDeviceSize zero = 0;
                const VkDeviceSize size = 24 * 6;
                rig.kit.api.vkCmdBindTransformFeedbackBuffersEXT(cb, 0, 1, &captured.buffer, &zero, &size);
                rig.kit.api.vkCmdBeginTransformFeedbackEXT(cb, 0, 1, &counter.buffer, &zero);
                rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                rig.kit.api.vkCmdEndTransformFeedbackEXT(cb, 0, 1, &counter.buffer, &zero);
                target.end(rig.kit, cb);
                rig.barrier_all(cb);
            });
            out.add("", rig.draw(target, p_show, nullptr, [&](VkCommandBuffer cb) {
                rig.bind_vertex(cb, captured);
                if (indirect != 0) {
                    rig.kit.api.vkCmdDrawIndirectByteCountEXT(cb, 1, 0, counter.buffer, 0, 0, 24);
                } else {
                    rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                }
            }));
        }, xfb_caps, nullptr, "VK_EXT_transform_feedback");
    }
    for (int inverted = 0; inverted < 2; ++inverted) {
        add_draw(inverted != 0 ? "conditional_inverted" : "conditional_predicate", [inverted](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            QuadKit quads(rig);
            const std::vector<std::uint32_t> predicates = {1, 0, 7, 0};
            const Buffer pb = rig.buffer(predicates, VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT);
            const VkPipeline pipeline = rig.pipe(quads.desc(target));
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                for (unsigned i = 0; i < 4; ++i) {
                    VkConditionalRenderingBeginInfoEXT begin{};
                    begin.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
                    begin.buffer = pb.buffer;
                    begin.offset = i * 4;
                    begin.flags = inverted != 0 ? VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT : 0;
                    rig.kit.api.vkCmdBeginConditionalRenderingEXT(cb, &begin);
                    quads.quad(cb, -0.95f + 0.5f * static_cast<float>(i), -0.8f, 0.44f, 1.6f, palette(i));
                    rig.kit.api.vkCmdEndConditionalRenderingEXT(cb);
                }
            }, quads.layout));
        }, [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceConditionalRenderingFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT);
            return f.conditionalRendering != VK_FALSE ? std::string() : std::string("conditionalRendering");
        }, nullptr, "VK_EXT_conditional_rendering");
    }
}

void register_mesh() {
    struct MeshCase {
        const char* name;
        bool task;
        bool indirect;
    };
    for (const MeshCase& entry : {MeshCase{"mesh_shader_direct", false, false}, MeshCase{"mesh_shader_task_direct", true, false},
                                  MeshCase{"mesh_shader_task_indirect", true, true}}) {
        add_draw(entry.name, [entry](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            PipelineDesc pipe;
            pipe.fragment = rig.sh("col4.frag");
            if (entry.task) {
                pipe.extra.push_back({VK_SHADER_STAGE_TASK_BIT_EXT, rig.sh("meshtask.v12.task"), nullptr});
                pipe.extra.push_back({VK_SHADER_STAGE_MESH_BIT_EXT, rig.sh("meshtask.v12.mesh"), nullptr});
            } else {
                pipe.extra.push_back({VK_SHADER_STAGE_MESH_BIT_EXT, rig.sh("meshlet.v12.mesh"), nullptr});
            }
            pipe.layout = rig.empty_layout();
            pipe.pass = target.clear_pass;
            const VkPipeline pipeline = rig.pipe(pipe);
            const VkDrawMeshTasksIndirectCommandEXT command{4, 1, 1};
            const Buffer ib = rig.buffer(&command, sizeof command, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
                if (entry.indirect) {
                    rig.kit.api.vkCmdDrawMeshTasksIndirectEXT(cb, ib.buffer, 0, 1, sizeof command);
                } else if (entry.task) {
                    rig.kit.api.vkCmdDrawMeshTasksEXT(cb, 5, 1, 1);
                } else {
                    rig.kit.api.vkCmdDrawMeshTasksEXT(cb, 10, 1, 1);
                }
            }));
        }, [entry](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceMeshShaderFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT);
            if (f.meshShader == VK_FALSE) {
                return "meshShader";
            }
            return entry.task && f.taskShader == VK_FALSE ? std::string("taskShader") : std::string();
        }, nullptr, "VK_EXT_mesh_shader", 2);
    }
}

const int registered = (register_direct(), register_indirect(), register_multi_draw(), register_gpu_generated(), register_feedback_and_conditional(),
                        register_mesh(), 0);

}  // namespace

}  // namespace lsw::render::vk
