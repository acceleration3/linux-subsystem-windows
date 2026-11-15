#pragma once

#include "render/vk/rig.hpp"

namespace lsw::render::vk {

struct QuadZ {
    float rect[4];
    float color[4];
    float depth[4];
};

class QuadKit {
public:
    explicit QuadKit(Rig& rig) : rig_(rig) {
        layout = rig.playout({}, sizeof(QuadZ), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    PipelineDesc desc(Target& target) {
        PipelineDesc p;
        p.vertex = rig_.sh("quadz.vert");
        p.fragment = rig_.sh("quadz.frag");
        p.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        p.layout = layout;
        p.pass = target.clear_pass;
        p.color_attachments = static_cast<std::uint32_t>(target.desc.colors.size());
        p.samples = target.desc.samples;
        return p;
    }

    void quad(VkCommandBuffer cb, float x, float y, float w, float h, const float* color, float z = 0.5f, float dz = 0.0f) const {
        QuadZ q{{x, y, w, h}, {color[0], color[1], color[2], color[3]}, {z, dz, 0, 0}};
        rig_.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &q, sizeof q);
        rig_.kit.api.vkCmdDraw(cb, 4, 1, 0, 0);
    }

    void tri_strip_quad(VkCommandBuffer cb, float x0, float y0, float x1, float y1, const float* color, float z = 0.5f) const {
        quad(cb, x0, y0, x1 - x0, y1 - y0, color, z);
    }

    VkPipelineLayout layout = VK_NULL_HANDLE;

private:
    Rig& rig_;
};

}  // namespace lsw::render::vk
