#pragma once

#include "render/common/mat4.hpp"
#include "render/vk/rig.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk::sk {

struct V3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return {a.x / l, a.y / l, a.z / l};
}

inline Mat4 view_from_basis(V3 r, V3 u, V3 f, V3 eye) {
    Mat4 m;
    m.m[0] = r.x;
    m.m[4] = r.y;
    m.m[8] = r.z;
    m.m[12] = -dot(r, eye);
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[13] = -dot(u, eye);
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[14] = dot(f, eye);
    return m;
}

inline Mat4 look_at(V3 eye, V3 target, V3 up = {0, 1, 0}) {
    const V3 f = normalize(target - eye);
    const V3 r = normalize(cross(f, up));
    const V3 u = cross(r, f);
    return view_from_basis(r, u, f, eye);
}

inline void cube_basis(unsigned face, V3& f, V3& r, V3& u) {
    static const V3 fs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const V3 rs[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};
    static const V3 us[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
    f = fs[face];
    r = rs[face];
    u = us[face];
}

inline Mat4 cube_face_view(unsigned face, V3 eye) {
    V3 f;
    V3 r;
    V3 u;
    cube_basis(face, f, r, u);
    return view_from_basis(r, u, f, eye);
}

inline Mat4 ortho_vk(float l, float r, float b, float t, float n, float f) {
    Mat4 m;
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -1.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -n / (f - n);
    return m;
}

inline V3 transform_point(const Mat4& m, V3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

struct SVertex {
    float p[3];
    float n[3];
    float c[3];
    float uv[2];
};

class Geometry {
public:
    std::vector<SVertex> v;
    std::vector<std::uint16_t> i;

    void quad(V3 a, V3 b, V3 c, V3 d, V3 n, V3 color, float uv_scale = 1.0f) {
        const auto base = static_cast<std::uint16_t>(v.size());
        const V3 pts[4] = {a, b, c, d};
        const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int k = 0; k < 4; ++k) {
            v.push_back({{pts[k].x, pts[k].y, pts[k].z}, {n.x, n.y, n.z}, {color.x, color.y, color.z}, {uvs[k][0] * uv_scale, uvs[k][1] * uv_scale}});
        }
        for (std::uint16_t k : {0, 1, 2, 0, 2, 3}) {
            i.push_back(static_cast<std::uint16_t>(base + k));
        }
    }

    void box(V3 center, V3 half, V3 color, float yaw = 0.0f) {
        const float cs = std::cos(yaw);
        const float sn = std::sin(yaw);
        const auto rot = [&](V3 p) { return V3{center.x + p.x * cs + p.z * sn, center.y + p.y, center.z - p.x * sn + p.z * cs}; };
        const auto nrot = [&](V3 n) { return V3{n.x * cs + n.z * sn, n.y, -n.x * sn + n.z * cs}; };
        const V3 hx{half.x, 0, 0};
        const V3 hy{0, half.y, 0};
        const V3 hz{0, 0, half.z};
        const auto face = [&](V3 axis_a, V3 axis_b, V3 normal, V3 tint) {
            const V3 o = V3{normal.x * half.x, normal.y * half.y, normal.z * half.z};
            quad(rot(o - axis_a - axis_b), rot(o + axis_a - axis_b), rot(o + axis_a + axis_b), rot(o - axis_a + axis_b), nrot(normal),
                 {color.x * tint.x, color.y * tint.y, color.z * tint.z});
        };
        const V3 white{1, 1, 1};
        face(hy, hz, {1, 0, 0}, white);
        face(hz, hy, {-1, 0, 0}, white);
        face(hz, hx, {0, 1, 0}, white);
        face(hx, hz, {0, -1, 0}, white);
        face(hx, hy, {0, 0, 1}, white);
        face(hy, hx, {0, 0, -1}, white);
    }

    void sphere(V3 center, float radius, V3 color, unsigned stacks = 14, unsigned slices = 22) {
        const auto base = static_cast<std::uint16_t>(v.size());
        for (unsigned a = 0; a <= stacks; ++a) {
            const float phi = 3.14159265f * static_cast<float>(a) / static_cast<float>(stacks);
            for (unsigned b = 0; b <= slices; ++b) {
                const float theta = 6.2831853f * static_cast<float>(b) / static_cast<float>(slices);
                const V3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
                const V3 p = center + n * radius;
                v.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {color.x, color.y, color.z},
                             {static_cast<float>(b) / static_cast<float>(slices), static_cast<float>(a) / static_cast<float>(stacks)}});
            }
        }
        for (unsigned a = 0; a < stacks; ++a) {
            for (unsigned b = 0; b < slices; ++b) {
                const auto p0 = static_cast<std::uint16_t>(base + a * (slices + 1) + b);
                const auto p1 = static_cast<std::uint16_t>(p0 + slices + 1);
                for (std::uint16_t k : {p0, p1, static_cast<std::uint16_t>(p0 + 1), static_cast<std::uint16_t>(p0 + 1), p1, static_cast<std::uint16_t>(p1 + 1)}) {
                    i.push_back(k);
                }
            }
        }
    }

    void floor(float y, float half, V3 color, float uv_scale = 1.0f) {
        quad({-half, y, -half}, {half, y, -half}, {half, y, half}, {-half, y, half}, {0, 1, 0}, color, uv_scale);
    }
};

struct Gpu {
    Buffer vb;
    Buffer ib;
    std::uint32_t count = 0;
};

inline Gpu upload(Rig& rig, const Geometry& g, VkBufferUsageFlags extra = 0) {
    Gpu out;
    out.vb = rig.buffer(g.v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | extra);
    out.ib = rig.buffer(g.i, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | extra);
    out.count = static_cast<std::uint32_t>(g.i.size());
    return out;
}

inline void draw(Rig& rig, VkCommandBuffer cb, const Gpu& gpu, std::uint32_t instances = 1) {
    rig.bind_vertex(cb, gpu.vb);
    rig.kit.api.vkCmdBindIndexBuffer(cb, gpu.ib.buffer, 0, VK_INDEX_TYPE_UINT16);
    rig.kit.api.vkCmdDrawIndexed(cb, gpu.count, instances, 0, 0, 0);
}

inline void svertex_input(PipelineDesc& pipe) {
    pipe.bindings = {{0, sizeof(SVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    pipe.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24},
                       {3, 0, VK_FORMAT_R32G32_SFLOAT, 36}};
}

struct PassDesc {
    std::vector<VkFormat> colors;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkImageLayout color_final = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout depth_final = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    bool clear_color = true;
    bool clear_depth = true;
};

inline VkRenderPass make_pass(Rig& rig, const PassDesc& desc) {
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;
    for (VkFormat format : desc.colors) {
        VkAttachmentDescription a{};
        a.format = format;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = desc.clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = desc.color_final;
        color_refs.push_back({static_cast<std::uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        attachments.push_back(a);
    }
    VkAttachmentReference depth_ref{};
    if (desc.depth != VK_FORMAT_UNDEFINED) {
        VkAttachmentDescription a{};
        a.format = desc.depth;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = desc.clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = desc.depth_final;
        depth_ref = {static_cast<std::uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        attachments.push_back(a);
    }
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<std::uint32_t>(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    subpass.pDepthStencilAttachment = desc.depth != VK_FORMAT_UNDEFINED ? &depth_ref : nullptr;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    return rig.render_pass(info);
}

inline VkClearValue clear_color(float r, float g, float b, float a) {
    VkClearValue v{};
    v.color.float32[0] = r;
    v.color.float32[1] = g;
    v.color.float32[2] = b;
    v.color.float32[3] = a;
    return v;
}

inline VkClearValue clear_depth(float depth = 1.0f) {
    VkClearValue v{};
    v.depthStencil = {depth, 0};
    return v;
}

inline void begin_pass(Rig& rig, VkCommandBuffer cb, VkRenderPass pass, VkFramebuffer fb, std::uint32_t w, std::uint32_t h, const std::vector<VkClearValue>& clears) {
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = pass;
    info.framebuffer = fb;
    info.renderArea = {{0, 0}, {w, h}};
    info.clearValueCount = static_cast<std::uint32_t>(clears.size());
    info.pClearValues = clears.data();
    rig.kit.api.vkCmdBeginRenderPass(cb, &info, VK_SUBPASS_CONTENTS_INLINE);
    rig.viewport(cb, 0, 0, static_cast<float>(w), static_cast<float>(h));
    rig.scissor(cb, 0, 0, w, h);
}

inline VkImageView sub_view(Rig& rig, const Texture& t, VkImageViewType type, VkImageAspectFlags aspect, std::uint32_t mip, std::uint32_t layer,
                            std::uint32_t layers = 1, std::uint32_t mips = 1) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = t.image;
    info.viewType = type;
    info.format = t.format;
    info.subresourceRange = {aspect, mip, mips, layer, layers};
    VkImageView view = VK_NULL_HANDLE;
    rig.vk_ok(rig.kit.api.vkCreateImageView(rig.kit.device, &info, nullptr, &view), "vkCreateImageView");
    Kit* kit = &rig.kit;
    rig.kit.defer([kit, view] { kit->api.vkDestroyImageView(kit->device, view, nullptr); });
    return view;
}

inline DescriptorWrite sampled(std::uint32_t binding, VkSampler sampler, VkImageView view,
                               VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    DescriptorWrite w;
    w.binding = binding;
    w.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.image = {sampler, view, layout};
    return w;
}

inline DescriptorWrite uniform(std::uint32_t binding, const Buffer& buffer, VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
    DescriptorWrite w;
    w.binding = binding;
    w.type = type;
    w.buffer = {buffer.buffer, 0, VK_WHOLE_SIZE};
    return w;
}

inline VkDescriptorSetLayoutBinding binding(std::uint32_t index, VkDescriptorType type, VkShaderStageFlags stages, std::uint32_t count = 1) {
    return {index, type, count, stages, nullptr};
}

inline void to_read(Rig& rig, VkCommandBuffer cb, const Texture& t, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    rig.kit.barrier(cb, t.image, aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, t.mips, t.layers);
}

inline void render_target_read(Rig& rig, VkCommandBuffer cb, Target& target) {
    for (const Texture& t : target.color_) {
        to_read(rig, cb, t);
    }
}

inline CaseSpec scene_spec(const std::string& name, unsigned size = 128) {
    CaseSpec spec("scene", name);
    spec.size(size, size).tolerance(Tolerance::exact()).timeout(120);
    return spec;
}

}  // namespace lsw::render::vk::sk
