#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/common/pattern.hpp"
#include "render/vk/target.hpp"
#include "render/vk/vk.hpp"

namespace lsw::render::vk {

namespace {

class MeshBuffers {
public:
    [[nodiscard]] bool create(Kit& kit, std::string& error) {
        const std::vector<ColoredVertex> vertices = Mesh::cube_vertices();
        const std::vector<std::uint16_t> indices = Mesh::cube_indices();
        index_count = static_cast<std::uint32_t>(indices.size());
        return kit.make_buffer_with(vertex, vertices.data(), vertices.size() * sizeof(ColoredVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, error) &&
               kit.make_buffer_with(index, indices.data(), indices.size() * sizeof(std::uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, error);
    }

    void bind(Kit& kit, VkCommandBuffer cb) const {
        const VkDeviceSize offset = 0;
        kit.api.vkCmdBindVertexBuffers(cb, 0, 1, &vertex.buffer, &offset);
        kit.api.vkCmdBindIndexBuffer(cb, index.buffer, 0, VK_INDEX_TYPE_UINT16);
    }

    Buffer vertex;
    Buffer index;
    std::uint32_t index_count = 0;
};

void mesh_input(PipelineDesc& desc) {
    desc.bindings = {{0, sizeof(ColoredVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    desc.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
}

class IndexedCubeDepth : public VkSingleScene {
public:
    bool draw(Kit& kit, CaptureSet& out, std::string& error) override {
        if (!kit.format_supports(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            error = "D32_SFLOAT depth attachment unsupported";
            return false;
        }
        Target target;
        TargetDesc desc;
        desc.depth = VK_FORMAT_D32_SFLOAT;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        target.clear_color[0] = 0.1f;
        target.clear_color[1] = 0.1f;
        target.clear_color[2] = 0.15f;
        MeshBuffers mesh;
        if (!mesh.create(kit, error)) {
            return false;
        }
        const VkPipelineLayout layout = VkCases::push_layout(kit, 64, VK_SHADER_STAGE_VERTEX_BIT, {}, error);
        PipelineDesc pipe;
        pipe.vertex = kit.shader("mesh.vert", error);
        pipe.fragment = kit.shader("mesh.frag", error);
        if (layout == VK_NULL_HANDLE || pipe.vertex == VK_NULL_HANDLE || pipe.fragment == VK_NULL_HANDLE) {
            return false;
        }
        mesh_input(pipe);
        pipe.depth_test = true;
        pipe.cull = VK_CULL_MODE_BACK_BIT;
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (!kit.make_pipeline(pipeline, pipe, error)) {
            return false;
        }
        const Mat4 projection = Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, true);
        const Mat4 view = Mat4::translate(0, 0, -6.0f);
        const Mat4 first = projection * view * Mat4::translate(-0.6f, 0.0f, 0.0f) * Mat4::rotate_y(0.6f) * Mat4::rotate_x(0.4f);
        const Mat4 second = projection * view * Mat4::translate(0.7f, 0.2f, 0.4f) * Mat4::rotate_y(-0.9f) * Mat4::rotate_z(0.5f) *
                            Mat4::scale(0.9f, 0.9f, 0.9f);
        const bool ok = kit.submit(
            [&](VkCommandBuffer cb) {
                target.begin(kit, cb, true);
                kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                mesh.bind(kit, cb);
                kit.api.vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, first.m);
                kit.api.vkCmdDrawIndexed(cb, mesh.index_count, 1, 0, 0, 0);
                kit.api.vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, second.m);
                kit.api.vkCmdDrawIndexed(cb, mesh.index_count, 1, 0, 0, 0);
                target.end(kit, cb);
            },
            error);
        Image color;
        if (!ok || !target.read_color(kit, 0, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const VkRegister<IndexedCubeDepth> kIndexedCubeDepth{CaseSpec("geometry", "indexed_cube_depth").tolerance(Tolerance::tight(1))};

class MrtDepthCapture : public VkSingleScene {
public:
    std::string unsupported(Kit& kit) override {
        if (!kit.format_supports(VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            return "RGBA16F colour attachment unsupported";
        }
        if (!kit.format_supports(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            return "D32_SFLOAT depth attachment unsupported";
        }
        if (kit.properties.limits.maxColorAttachments < 2) {
            return "fewer than two colour attachments";
        }
        return {};
    }

    bool draw(Kit& kit, CaptureSet& out, std::string& error) override {
        Target target;
        TargetDesc desc;
        desc.colors = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
        desc.depth = VK_FORMAT_D32_SFLOAT;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        MeshBuffers mesh;
        if (!mesh.create(kit, error)) {
            return false;
        }
        const VkPipelineLayout layout = VkCases::push_layout(kit, 64, VK_SHADER_STAGE_VERTEX_BIT, {}, error);
        PipelineDesc pipe;
        pipe.vertex = kit.shader("mesh.vert", error);
        pipe.fragment = kit.shader("mrt.frag", error);
        if (layout == VK_NULL_HANDLE || pipe.vertex == VK_NULL_HANDLE || pipe.fragment == VK_NULL_HANDLE) {
            return false;
        }
        mesh_input(pipe);
        pipe.depth_test = true;
        pipe.cull = VK_CULL_MODE_BACK_BIT;
        pipe.color_attachments = 2;
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (!kit.make_pipeline(pipeline, pipe, error)) {
            return false;
        }
        const Mat4 transform = Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, true) * Mat4::translate(0, 0, -5.0f) *
                               Mat4::rotate_y(0.7f) * Mat4::rotate_x(-0.5f);
        const bool ok = kit.submit(
            [&](VkCommandBuffer cb) {
                target.begin(kit, cb, true);
                kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                mesh.bind(kit, cb);
                kit.api.vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, transform.m);
                kit.api.vkCmdDrawIndexed(cb, mesh.index_count, 1, 0, 0, 0);
                target.end(kit, cb);
            },
            error);
        Image c0;
        Image c1;
        Image depth;
        if (!ok || !target.read_color(kit, 0, c0, error) || !target.read_color(kit, 1, c1, error) ||
            !target.read_depth(kit, depth, error)) {
            return false;
        }
        out.add("color0", std::move(c0));
        out.add("color1", std::move(c1));
        out.add("depth", std::move(depth));
        return true;
    }
};

const VkRegister<MrtDepthCapture> kMrtDepthCapture{CaseSpec("targets", "mrt_depth_capture").tolerance(Tolerance::tight(1))};

}  // namespace

}  // namespace lsw::render::vk
