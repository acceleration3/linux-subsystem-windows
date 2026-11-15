#include "render/vk/quadkit.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::vk {

namespace {

CaseSpec plain(const char* name) {
    CaseSpec spec("pipe", name);
    spec.tolerance(Tolerance::exact());
    return spec;
}

struct SpecData {
    std::int32_t mode;
    float scale;
    VkBool32 invert;
    std::int32_t count;
};

void register_specialization() {
    struct Variant {
        const char* name;
        const char* shader;
        SpecData data;
    };
    const std::vector<Variant> variants = {
        {"spec_mode_gradient", "spec.frag", {0, 1.0f, VK_FALSE, 4}},
        {"spec_mode_grid_inverted", "spec.frag", {1, 1.5f, VK_TRUE, 4}},
        {"spec_mode_radial_scaled", "spec.frag", {2, 2.5f, VK_FALSE, 4}},
        {"spec_array_size_3", "spec_arr.frag", {0, 1.0f, VK_FALSE, 3}},
        {"spec_array_size_9", "spec_arr.frag", {0, 1.0f, VK_FALSE, 9}},
    };
    for (const Variant& variant : variants) {
        Cases::add(plain(variant.name), [variant](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            const VkSpecializationMapEntry entries[4] = {{0, offsetof(SpecData, mode), 4},
                                                         {1, offsetof(SpecData, scale), 4},
                                                         {2, offsetof(SpecData, invert), 4},
                                                         {3, offsetof(SpecData, count), 4}};
            VkSpecializationInfo info{4, entries, sizeof variant.data, &variant.data};
            PipelineDesc pipe;
            pipe.vertex = rig.sh("full.vert");
            pipe.fragment = VK_NULL_HANDLE;
            pipe.extra.push_back({VK_SHADER_STAGE_FRAGMENT_BIT, rig.sh(variant.shader), &info});
            pipe.layout = rig.empty_layout();
            pipe.pass = target.clear_pass;
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }));
        });
    }
}

void check_cache_header(Rig& rig, VkPipelineCache cache) {
    std::size_t size = 0;
    rig.vk_ok(rig.kit.api.vkGetPipelineCacheData(rig.kit.device, cache, &size, nullptr), "vkGetPipelineCacheData");
    std::vector<std::uint8_t> data(size);
    rig.vk_ok(rig.kit.api.vkGetPipelineCacheData(rig.kit.device, cache, &size, data.data()), "vkGetPipelineCacheData");
    rig.must(size >= 32, "pipeline cache data is smaller than its header");
    std::uint32_t header[4] = {};
    std::memcpy(header, data.data(), sizeof header);
    rig.must(header[0] >= 32, "pipeline cache headerSize is below 32");
    rig.must(header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE, "pipeline cache header version is not ONE");
    rig.must(header[2] == rig.kit.properties.vendorID, "pipeline cache header vendorID differs from the device properties");
    rig.must(header[3] == rig.kit.properties.deviceID, "pipeline cache header deviceID differs from the device properties");
    rig.must(std::memcmp(data.data() + 16, rig.kit.properties.pipelineCacheUUID, VK_UUID_SIZE) == 0,
             "pipeline cache header UUID differs from the device properties");
}

void register_cache_and_derivatives() {
    Cases::add(plain("obj_pipeline_cache_roundtrip"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        QuadKit quads(rig);
        VkPipelineCacheCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VkPipelineCache first = VK_NULL_HANDLE;
        rig.vk_ok(rig.kit.api.vkCreatePipelineCache(rig.kit.device, &info, nullptr, &first), "vkCreatePipelineCache");
        rig.kit.defer([&kit = rig.kit, first] { kit.api.vkDestroyPipelineCache(kit.device, first, nullptr); });
        PipelineDesc a = quads.desc(target);
        a.cache = first;
        const VkPipeline p_a = rig.pipe(a);
        std::size_t size = 0;
        rig.vk_ok(rig.kit.api.vkGetPipelineCacheData(rig.kit.device, first, &size, nullptr), "vkGetPipelineCacheData");
        std::vector<std::uint8_t> data(size);
        rig.vk_ok(rig.kit.api.vkGetPipelineCacheData(rig.kit.device, first, &size, data.data()), "vkGetPipelineCacheData");
        rig.must(size >= 32, "pipeline cache data is smaller than its header");
        check_cache_header(rig, first);
        info.initialDataSize = size;
        info.pInitialData = data.data();
        VkPipelineCache second = VK_NULL_HANDLE;
        rig.vk_ok(rig.kit.api.vkCreatePipelineCache(rig.kit.device, &info, nullptr, &second), "vkCreatePipelineCache");
        check_cache_header(rig, second);
        rig.kit.defer([&kit = rig.kit, second] { kit.api.vkDestroyPipelineCache(kit.device, second, nullptr); });
        PipelineDesc b = quads.desc(target);
        b.cache = second;
        b.blend = true;
        const VkPipeline p_b = rig.pipe(b);
        out.add("", rig.draw(target, p_a, nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -0.9f, -0.9f, 1.2f, 1.2f, palette(0));
            rig.bind_graphics(cb, p_b);
            const float soft[4] = {0.2f, 0.6f, 1.0f, 0.5f};
            quads.quad(cb, -0.3f, -0.3f, 1.2f, 1.2f, soft);
        }, quads.layout));
    });
    Cases::add(plain("obj_pipeline_cache_merge"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        QuadKit quads(rig);
        VkPipelineCacheCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VkPipelineCache caches[3] = {};
        for (VkPipelineCache& cache : caches) {
            rig.vk_ok(rig.kit.api.vkCreatePipelineCache(rig.kit.device, &info, nullptr, &cache), "vkCreatePipelineCache");
            const VkPipelineCache owned = cache;
            rig.kit.defer([&kit = rig.kit, owned] { kit.api.vkDestroyPipelineCache(kit.device, owned, nullptr); });
        }
        PipelineDesc a = quads.desc(target);
        a.cache = caches[0];
        PipelineDesc b = quads.desc(target);
        b.cache = caches[1];
        b.polygon = VK_POLYGON_MODE_FILL;
        b.depth_write = false;
        const VkPipeline p_a = rig.pipe(a);
        const VkPipeline p_b = rig.pipe(b);
        rig.vk_ok(rig.kit.api.vkMergePipelineCaches(rig.kit.device, caches[2], 2, caches), "vkMergePipelineCaches");
        check_cache_header(rig, caches[2]);
        PipelineDesc c = quads.desc(target);
        c.cache = caches[2];
        c.blend = true;
        c.dst_color = VK_BLEND_FACTOR_ONE;
        c.src_color = VK_BLEND_FACTOR_SRC_ALPHA;
        const VkPipeline p_c = rig.pipe(c);
        out.add("", rig.draw(target, p_a, nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -0.9f, -0.9f, 0.9f, 0.9f, palette(1));
            rig.bind_graphics(cb, p_b);
            quads.quad(cb, 0.0f, -0.9f, 0.9f, 0.9f, palette(2));
            rig.bind_graphics(cb, p_c);
            const float glow[4] = {0.6f, 0.3f, 0.1f, 0.7f};
            quads.quad(cb, -0.5f, -0.5f, 1.0f, 1.0f, glow);
        }, quads.layout));
    });
    Cases::add(plain("obj_derivative_pipelines"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        QuadKit quads(rig);
        PipelineDesc base = quads.desc(target);
        base.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
        const VkPipeline p_base = rig.pipe(base);
        PipelineDesc add = quads.desc(target);
        add.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
        add.base = p_base;
        add.blend = true;
        add.src_color = VK_BLEND_FACTOR_SRC_ALPHA;
        add.dst_color = VK_BLEND_FACTOR_ONE;
        PipelineDesc lines = quads.desc(target);
        lines.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
        lines.base = p_base;
        lines.polygon = VK_POLYGON_MODE_FILL;
        lines.write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT;
        const VkPipeline p_add = rig.pipe(add);
        const VkPipeline p_mask = rig.pipe(lines);
        out.add("", rig.draw(target, p_base, nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -0.9f, -0.9f, 1.1f, 1.1f, palette(3));
            rig.bind_graphics(cb, p_add);
            const float glow[4] = {0.9f, 0.2f, 0.4f, 0.6f};
            quads.quad(cb, -0.2f, -0.2f, 1.1f, 1.1f, glow);
            rig.bind_graphics(cb, p_mask);
            quads.quad(cb, -0.6f, 0.2f, 1.5f, 0.7f, palette(5));
        }, quads.layout));
    });
}

void register_push_constants() {
    Cases::add(plain("obj_push_stage_ranges"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const VkPipelineLayout layout = rig.playout({}, {{VK_SHADER_STAGE_VERTEX_BIT, 0, 16}, {VK_SHADER_STAGE_FRAGMENT_BIT, 16, 16}});
        PipelineDesc pipe;
        pipe.vertex = rig.sh("pushsplit.vert");
        pipe.fragment = rig.sh("pushsplit.frag");
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        pipe.layout = layout;
        pipe.pass = target.clear_pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            for (unsigned i = 0; i < 6; ++i) {
                const float rect[4] = {-0.9f + 0.6f * static_cast<float>(i % 3), -0.9f + 0.95f * static_cast<float>(i / 3), 0.55f, 0.85f};
                rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, rect, 16, 0);
                rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, palette(i), 16, 16);
                rig.kit.api.vkCmdDraw(cb, 4, 1, 0, 0);
            }
        }, layout));
    });
    Cases::add(plain("obj_push_partial_updates"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const VkPipelineLayout layout = rig.playout({}, {{VK_SHADER_STAGE_VERTEX_BIT, 0, 64}, {VK_SHADER_STAGE_FRAGMENT_BIT, 64, 32}});
        PipelineDesc pipe = rig.pos2c(target, layout);
        pipe.vertex = rig.sh("pushmat.vert");
        pipe.fragment = rig.sh("pushmat.frag");
        const std::vector<Vertex2> tri = {vertex(-0.4f, -0.4f, 0), vertex(0.4f, -0.4f, 1), vertex(0.0f, 0.45f, 2)};
        const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            float m[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1, 0, -0.5f, -0.5f, 0, 1};
            const float tint[4] = {1, 1, 1, 1};
            const float add[4] = {0, 0, 0, 0};
            rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, m, 64, 0);
            rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, tint, 16, 64);
            rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, add, 16, 80);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            m[12] = 0.5f;
            rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, &m[12], 8, 48);
            const float tint2[4] = {0.4f, 1.0f, 0.6f, 1.0f};
            rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, tint2, 16, 64);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            m[13] = 0.5f;
            rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, &m[13], 4, 52);
            const float add2[4] = {0.2f, 0.0f, 0.3f, 0.0f};
            rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, add2, 16, 80);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            m[12] = -0.5f;
            rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, &m[12], 4, 48);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        }, layout));
    });
    Cases::add(plain("obj_push_max_block"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        const std::uint32_t bytes = 128;
        const VkPipelineLayout layout = rig.playout({}, bytes, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        PipelineDesc pipe = rig.pos2c(target, layout);
        pipe.vertex = rig.sh("push128.vert");
        pipe.fragment = rig.sh("push128.frag");
        const std::vector<Vertex2> tri = {vertex(-0.9f, -0.9f, 0), vertex(0.9f, -0.9f, 1), vertex(0.0f, 0.9f, 2)};
        const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            float data[32];
            for (int i = 0; i < 32; ++i) {
                data[i] = 0.05f + 0.012f * static_cast<float>(i);
            }
            rig.push(cb, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, data, bytes, 0);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        }, layout));
    });
}

void register_renderpass_variants() {
    CaseSpec dynamic = plain("obj_dynamic_rendering");
    dynamic.min_version(1, 3);
    Cases::add(std::move(dynamic), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TextureDesc color_desc;
        color_desc.width = color_desc.height = 128;
        color_desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        const Texture color = rig.texture(color_desc);
        TextureDesc depth_desc;
        depth_desc.width = depth_desc.height = 128;
        depth_desc.format = Cases::depth_only(kit);
        depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        const Texture depth = rig.texture(depth_desc);
        const VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineRenderingCreateInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &color_format;
        rendering.depthAttachmentFormat = depth_desc.format;
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(rig.make_target(TargetDesc{}));
        pipe.pass = VK_NULL_HANDLE;
        pipe.next = &rendering;
        pipe.depth_test = true;
        const VkPipeline pipeline = rig.pipe(pipe);
        rig.run([&](VkCommandBuffer cb) {
            kit.barrier(cb, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            kit.barrier(cb, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VkRenderingAttachmentInfo ca{};
            ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            ca.imageView = color.view;
            ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            ca.clearValue.color = {{0.05f, 0.08f, 0.15f, 1.0f}};
            VkRenderingAttachmentInfo da{};
            da.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            da.imageView = depth.view;
            da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            da.clearValue.depthStencil = {1.0f, 0};
            VkRenderingInfo info{};
            info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            info.renderArea = {{0, 0}, {128, 128}};
            info.layerCount = 1;
            info.colorAttachmentCount = 1;
            info.pColorAttachments = &ca;
            info.pDepthAttachment = &da;
            kit.api.vkCmdBeginRendering(cb, &info);
            rig.bind_graphics(cb, pipeline);
            rig.viewport(cb, 0, 0, 128, 128);
            rig.scissor(cb, 0, 0, 128, 128);
            quads.quad(cb, -0.8f, -0.8f, 1.2f, 1.2f, palette(0), 0.6f);
            quads.quad(cb, -0.3f, -0.4f, 1.2f, 1.2f, palette(2), 0.3f, 0.2f);
            quads.quad(cb, -0.5f, 0.1f, 1.3f, 0.7f, palette(3), 0.45f);
            kit.api.vkCmdEndRendering(cb);
        });
        out.add("", rig.read(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
    });
    CaseSpec subpass = plain("obj_subpass_input_attachment");
    Cases::add(std::move(subpass), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TextureDesc inter;
        inter.width = inter.height = 128;
        inter.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        const Texture mid = rig.texture(inter);
        TextureDesc final_desc;
        final_desc.width = final_desc.height = 128;
        final_desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        const Texture fin = rig.texture(final_desc);
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        const VkAttachmentReference write0{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference read0{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkAttachmentReference write1{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpasses[2]{};
        subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpasses[0].colorAttachmentCount = 1;
        subpasses[0].pColorAttachments = &write0;
        subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpasses[1].inputAttachmentCount = 1;
        subpasses[1].pInputAttachments = &read0;
        subpasses[1].colorAttachmentCount = 1;
        subpasses[1].pColorAttachments = &write1;
        VkSubpassDependency dep{};
        dep.srcSubpass = 0;
        dep.dstSubpass = 1;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 2;
        rp.pAttachments = attachments;
        rp.subpassCount = 2;
        rp.pSubpasses = subpasses;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;
        const VkRenderPass pass = rig.render_pass(rp);
        const VkFramebuffer fb = rig.framebuffer(pass, {mid.view, fin.view}, 128, 128);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        DescriptorWrite write;
        write.binding = 0;
        write.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        write.image = {VK_NULL_HANDLE, mid.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorSet set = rig.dset(set_layout, {write});
        const VkPipelineLayout read_layout = rig.playout({set_layout});
        QuadKit quads(rig);
        PipelineDesc scene = quads.desc(rig.make_target(TargetDesc{}));
        scene.pass = pass;
        scene.subpass = 0;
        PipelineDesc compose;
        compose.vertex = rig.sh("full.vert");
        compose.fragment = rig.sh("sp_read.frag");
        compose.layout = read_layout;
        compose.pass = pass;
        compose.subpass = 1;
        const VkPipeline p_scene = rig.pipe(scene);
        const VkPipeline p_compose = rig.pipe(compose);
        rig.run([&](VkCommandBuffer cb) {
            VkClearValue clears[2]{};
            clears[0].color = {{0.1f, 0.2f, 0.3f, 1.0f}};
            VkRenderPassBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            begin.renderPass = pass;
            begin.framebuffer = fb;
            begin.renderArea = {{0, 0}, {128, 128}};
            begin.clearValueCount = 2;
            begin.pClearValues = clears;
            kit.api.vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
            rig.viewport(cb, 0, 0, 128, 128);
            rig.scissor(cb, 0, 0, 128, 128);
            rig.bind_graphics(cb, p_scene);
            quads.quad(cb, -0.8f, -0.8f, 1.1f, 1.1f, palette(0));
            quads.quad(cb, -0.2f, -0.3f, 1.0f, 1.0f, palette(1));
            quads.quad(cb, -0.5f, 0.2f, 1.2f, 0.6f, palette(2));
            kit.api.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
            rig.bind_graphics(cb, p_compose, read_layout, {set});
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            kit.api.vkCmdEndRenderPass(cb);
        });
        out.add("", rig.read(fin, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
    });
}

void register_multiview() {
    for (unsigned views : {2u, 4u}) {
        CaseSpec spec = plain(views == 2 ? "obj_multiview_2" : "obj_multiview_4");
        spec.same_as({views == 2 ? "pipe.obj_multiview_4" : "pipe.obj_multiview_2"}, views == 2 ? "the two views render the first two views of the four view row of the same scene" : "views 0 and 1 render the same scene as the two view row, views 2 and 3 are new");
        Cases::add(
            std::move(spec),
            [views](Rig& rig, CaptureSet& out) {
                Kit& kit = rig.kit;
                TextureDesc desc;
                desc.width = desc.height = 128;
                desc.layers = views;
                desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                const Texture color = rig.texture(desc);
                VkAttachmentDescription attachment{};
                attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
                attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                VkSubpassDescription subpass{};
                subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                subpass.colorAttachmentCount = 1;
                subpass.pColorAttachments = &ref;
                const std::uint32_t mask = (1u << views) - 1u;
                VkRenderPassMultiviewCreateInfo multiview{};
                multiview.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
                multiview.subpassCount = 1;
                multiview.pViewMasks = &mask;
                multiview.correlationMaskCount = 1;
                multiview.pCorrelationMasks = &mask;
                VkRenderPassCreateInfo rp{};
                rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                rp.pNext = &multiview;
                rp.attachmentCount = 1;
                rp.pAttachments = &attachment;
                rp.subpassCount = 1;
                rp.pSubpasses = &subpass;
                const VkRenderPass pass = rig.render_pass(rp);
                const VkFramebuffer fb = rig.framebuffer(pass, {color.view}, 128, 128, 1);
                std::vector<Vertex2> tri = {vertex(-0.6f, -0.6f, 0), vertex(0.6f, -0.6f, 1), vertex(0.0f, 0.7f, 2),
                                            vertex(-0.2f, -0.1f, 3), vertex(0.8f, 0.1f, 4),  vertex(0.3f, 0.8f, 5)};
                const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                PipelineDesc pipe = rig.pos2c(rig.make_target(TargetDesc{}));
                pipe.vertex = rig.sh("mv.vert");
                pipe.pass = pass;
                const VkPipeline pipeline = rig.pipe(pipe);
                rig.run([&](VkCommandBuffer cb) {
                    VkClearValue clear{};
                    clear.color = {{0.06f, 0.06f, 0.1f, 1.0f}};
                    VkRenderPassBeginInfo begin{};
                    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    begin.renderPass = pass;
                    begin.framebuffer = fb;
                    begin.renderArea = {{0, 0}, {128, 128}};
                    begin.clearValueCount = 1;
                    begin.pClearValues = &clear;
                    kit.api.vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
                    rig.viewport(cb, 0, 0, 128, 128);
                    rig.scissor(cb, 0, 0, 128, 128);
                    rig.bind_graphics(cb, pipeline);
                    rig.bind_vertex(cb, vb);
                    kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                    kit.api.vkCmdEndRenderPass(cb);
                });
                for (unsigned v = 0; v < views; ++v) {
                    out.add("view" + std::to_string(v), rig.read(color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, v));
                }
            },
            [views](Kit& kit) -> std::string {
                const auto f = kit.query_features<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
                if (f.multiview == VK_FALSE) {
                    return "multiview";
                }
                const auto p = kit.query_properties<VkPhysicalDeviceVulkan11Properties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);
                return p.maxMultiviewViewCount < views ? std::string("maxMultiviewViewCount") : std::string();
            });
    }
}

const int registered = (register_specialization(), register_cache_and_derivatives(), register_push_constants(),
                        register_renderpass_variants(), register_multiview(), 0);

}  // namespace

}  // namespace lsw::render::vk
