#include "render/vk/quadkit.hpp"

#include <cmath>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.09f, 1.0f};

CaseSpec spec_of(const std::string& name) {
    CaseSpec spec("stages", name);
    spec.tolerance(Tolerance::tight(1));
    return spec;
}

struct TessPush {
    float outer[4];
    float inner[4];
};

enum class Domain { Triangle, Quad, Isoline };

std::vector<Vertex2> tess_patches(Domain domain) {
    switch (domain) {
        case Domain::Triangle:
            return {vertex(-0.85f, -0.8f, 0), vertex(0.85f, -0.7f, 3), vertex(0.0f, 0.85f, 5)};
        case Domain::Quad:
            return {vertex(-0.85f, -0.8f, 0), vertex(0.85f, -0.75f, 1), vertex(0.8f, 0.8f, 2), vertex(-0.8f, 0.85f, 4)};
        default: {
            std::vector<Vertex2> v;
            for (int i = 0; i < 4; ++i) {
                v.push_back(vertex(-0.8f, -0.55f + 0.36f * static_cast<float>(i), static_cast<unsigned>(i)));
                v.push_back(vertex(0.8f, -0.45f + 0.36f * static_cast<float>(i), static_cast<unsigned>(i + 3)));
            }
            return v;
        }
    }
}

void add_tess(const std::string& name, Domain domain, const std::string& evaluation, bool point_mode, bool winding_visible) {
    CaseSpec spec = spec_of(name);
    spec.feature("tessellationShader");
    if (!point_mode) {
        spec.feature("fillModeNonSolid");
    }
    Cases::add(std::move(spec), [=](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        const std::vector<Vertex2> patch = tess_patches(domain);
        const std::uint32_t points = domain == Domain::Triangle ? 3 : (domain == Domain::Quad ? 4 : 2);
        const Buffer vb = rig.buffer(patch, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const VkPipelineLayout layout = rig.playout({}, sizeof(TessPush), VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
        const char* control = domain == Domain::Triangle ? "tessctl3.tesc" : (domain == Domain::Quad ? "tessctl4.tesc" : "tessctl2.tesc");
        PipelineDesc fill = rig.pos2c(target, layout);
        fill.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        fill.patch_points = points;
        fill.extra.push_back({VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, rig.sh(control), nullptr});
        fill.extra.push_back({VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, rig.sh(evaluation.c_str()), nullptr});
        fill.cull = winding_visible ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        PipelineDesc lines = fill;
        lines.polygon = VK_POLYGON_MODE_LINE;
        lines.cull = VK_CULL_MODE_NONE;
        const VkPipeline p_fill = rig.pipe(fill);
        const VkPipeline p_lines = point_mode ? VK_NULL_HANDLE : rig.pipe(lines);
        TessPush push{{5.3f, 4.2f, 6.7f, 3.5f}, {4.6f, 3.3f, 0, 0}};
        if (domain == Domain::Isoline) {
            push.outer[0] = 3.0f;
            push.outer[1] = 9.4f;
        }
        const auto count = static_cast<std::uint32_t>(patch.size());
        out.add("", rig.draw(target, p_fill, &vb, [&](VkCommandBuffer cb) {
            rig.push(cb, layout, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, &push, sizeof push);
            rig.kit.api.vkCmdDraw(cb, count, 1, 0, 0);
            if (!point_mode) {
                rig.bind_graphics(cb, p_lines);
                rig.kit.api.vkCmdDraw(cb, count, 1, 0, 0);
            }
        }, layout));
    });
}

void register_tessellation() {
    for (const char* spacing : {"equal", "fractional_even", "fractional_odd"}) {
        for (const char* winding : {"cw", "ccw"}) {
            add_tess(std::string("tess_tri_") + spacing + "_" + winding, Domain::Triangle, std::string("tess_tri_") + spacing + "_" + winding + ".tese", false, true);
        }
        add_tess(std::string("tess_quad_") + spacing, Domain::Quad, std::string("tess_quad_") + spacing + ".tese", false, true);
    }
    add_tess("tess_isoline_equal", Domain::Isoline, "tess_iso_equal.tese", true, false);
    add_tess("tess_isoline_fractional_odd", Domain::Isoline, "tess_iso_fractional_odd.tese", true, false);
    add_tess("tess_triangle_point_mode", Domain::Triangle, "tess_tri_point.tese", true, false);
}

void register_geometry() {
    Cases::add(spec_of("gs_amplify_triangle").feature("geometryShader").tolerance(Tolerance::tight(1)), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        const std::vector<Vertex2> tri = {vertex(-1, -1, 0), vertex(1, -1, 1), vertex(0, 1, 2)};
        const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.extra.push_back({VK_SHADER_STAGE_GEOMETRY_BIT, rig.sh("gs_amplify.geom"), nullptr});
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }));
    });
    Cases::add(spec_of("gs_invocations").feature("geometryShader"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        const std::vector<Vertex2> tri = {vertex(-1, -1, 0), vertex(1, -1, 1), vertex(0, 1, 2)};
        const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.extra.push_back({VK_SHADER_STAGE_GEOMETRY_BIT, rig.sh("gs_invocations.geom"), nullptr});
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }));
    });
    Cases::add(spec_of("gs_points_to_quads").feature("geometryShader"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        std::vector<Vertex2> pts;
        for (int i = 0; i < 25; ++i) {
            pts.push_back(vertex(-0.8f + 0.4f * static_cast<float>(i % 5), -0.8f + 0.4f * static_cast<float>(i / 5), static_cast<unsigned>(i)));
        }
        const Buffer vb = rig.buffer(pts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        pipe.extra.push_back({VK_SHADER_STAGE_GEOMETRY_BIT, rig.sh("gs_points.geom"), nullptr});
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 25, 1, 0, 0); }));
    });
    CaseSpec viewports = spec_of("gs_viewport_index");
    viewports.feature("geometryShader").feature("multiViewport");
    Cases::add(std::move(viewports), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        const std::vector<Vertex2> tri = {vertex(-0.9f, -0.9f, 0), vertex(0.9f, -0.9f, 1), vertex(0.0f, 0.9f, 2)};
        const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.extra.push_back({VK_SHADER_STAGE_GEOMETRY_BIT, rig.sh("gs_viewports.geom"), nullptr});
        pipe.viewport_count = 4;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
            const VkViewport viewports[4] = {{0, 0, 64, 64, 0, 1}, {64, 0, 64, 64, 0, 1}, {0, 64, 64, 64, 0, 1}, {64, 64, 64, 64, 0, 1}};
            const VkRect2D scissors[4] = {{{0, 0}, {64, 64}}, {{64, 0}, {64, 64}}, {{0, 64}, {64, 64}}, {{64, 64}, {64, 64}}};
            rig.kit.api.vkCmdSetViewport(cb, 0, 4, viewports);
            rig.kit.api.vkCmdSetScissor(cb, 0, 4, scissors);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        }));
    });
}

void run_layered(Rig& rig, CaptureSet& out, unsigned layers, bool cube) {
    Kit& kit = rig.kit;
    TextureDesc desc;
    desc.width = desc.height = 64;
    desc.layers = layers;
    desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    desc.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const Texture color = rig.texture(desc);
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &attachment;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    const VkRenderPass pass = rig.render_pass(rp);
    const VkFramebuffer fb = rig.framebuffer(pass, {color.view}, 64, 64, layers);
    const std::vector<Vertex2> tri = {vertex(-0.8f, -0.7f, 0), vertex(0.8f, -0.6f, 2), vertex(0.0f, 0.8f, 4)};
    const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    const VkPipelineLayout layout = rig.playout({}, 16, VK_SHADER_STAGE_GEOMETRY_BIT);
    PipelineDesc pipe = rig.pos2c(rig.make_target(TargetDesc{}), layout);
    pipe.extra.push_back({VK_SHADER_STAGE_GEOMETRY_BIT, rig.sh("gs_layered.geom"), nullptr});
    pipe.pass = pass;
    const VkPipeline pipeline = rig.pipe(pipe);
    rig.run([&](VkCommandBuffer cb) {
        VkClearValue clear{};
        clear.color = {{0.08f, 0.09f, 0.14f, 1.0f}};
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = pass;
        begin.framebuffer = fb;
        begin.renderArea = {{0, 0}, {64, 64}};
        begin.clearValueCount = 1;
        begin.pClearValues = &clear;
        kit.api.vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
        rig.viewport(cb, 0, 0, 64, 64);
        rig.scissor(cb, 0, 0, 64, 64);
        rig.bind_graphics(cb, pipeline);
        rig.bind_vertex(cb, vb);
        const float active[4] = {static_cast<float>(layers), 0, 0, 0};
        rig.push(cb, layout, VK_SHADER_STAGE_GEOMETRY_BIT, active, 16);
        kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        kit.api.vkCmdEndRenderPass(cb);
    });
    for (unsigned l = 0; l < layers; ++l) {
        out.add("layer" + std::to_string(l), rig.read(color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, l));
    }
    if (cube) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = color.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view.format = VK_FORMAT_R8G8B8A8_UNORM;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
        VkImageView cube_view = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateImageView(kit.device, &view, nullptr, &cube_view), "vkCreateImageView");
        kit.defer([&kit, cube_view] { kit.api.vkDestroyImageView(kit.device, cube_view, nullptr); });
        Target& show = rig.make_target(TargetDesc{});
        const VkSampler sampler = rig.sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
        DescriptorWrite write;
        write.binding = 0;
        write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.image = {sampler, cube_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorSet set = rig.dset(set_layout, {write});
        const VkPipelineLayout show_layout = rig.playout({set_layout});
        PipelineDesc show_pipe;
        show_pipe.vertex = rig.sh("full.vert");
        show_pipe.fragment = rig.sh("tcube.frag");
        show_pipe.layout = show_layout;
        show_pipe.pass = show.clear_pass;
        const VkPipeline p_show = rig.pipe(show_pipe);
        out.add("sampled_cube", rig.draw(show, p_show, nullptr, [&](VkCommandBuffer cb) { kit.api.vkCmdDraw(cb, 3, 1, 0, 0); }, show_layout, {set}));
    }
}

void register_layered() {
    Cases::add(spec_of("layered_array_gs").feature("geometryShader"), [](Rig& rig, CaptureSet& out) { run_layered(rig, out, 4, false); });
    Cases::add(spec_of("layered_cube_faces_gs").feature("geometryShader"), [](Rig& rig, CaptureSet& out) { run_layered(rig, out, 6, true); });
}

void register_shader_builtins() {
    CaseSpec params = spec_of("draw_parameters");
    params.feature("multiDrawIndirect").feature("drawIndirectFirstInstance").min_version(1, 1);
    Cases::add(
        std::move(params),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            std::vector<Vertex2> tris;
            for (unsigned t = 0; t < 4; ++t) {
                tris.push_back(vertex(-1.0f, -1.0f, t * 2));
                tris.push_back(vertex(1.0f, -1.0f, t * 2 + 1));
                tris.push_back(vertex(0.0f, 1.0f, t * 2 + 2));
            }
            const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const std::vector<std::uint16_t> indices = {0, 1, 2};
            const Buffer ib = rig.buffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            const std::vector<VkDrawIndexedIndirectCommand> commands = {{3, 2, 0, 0, 0}, {3, 2, 0, 3, 1}, {3, 3, 0, 6, 2}};
            const Buffer cb_buf = rig.buffer(commands, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
            PipelineDesc pipe = rig.pos2c(target);
            pipe.vertex = rig.sh("drawparams.vert");
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                rig.kit.api.vkCmdBindIndexBuffer(cb, ib.buffer, 0, VK_INDEX_TYPE_UINT16);
                rig.kit.api.vkCmdDrawIndexedIndirect(cb, cb_buf.buffer, 0, 3, sizeof(VkDrawIndexedIndirectCommand));
            }));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
            return f.shaderDrawParameters != VK_FALSE ? std::string() : std::string("shaderDrawParameters");
        });
    Cases::add(spec_of("primitive_id").feature("geometryShader"), [](Rig& rig, CaptureSet& out) {
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        std::vector<Vertex2> v;
        for (int i = 0; i < 6; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 6.0f;
            v.push_back(vertex(0.0f, 0.0f, 7));
            v.push_back(vertex(0.85f * std::cos(a), 0.85f * std::sin(a), static_cast<unsigned>(i)));
            v.push_back(vertex(0.85f * std::cos(a + 1.0f), 0.85f * std::sin(a + 1.0f), static_cast<unsigned>(i + 1)));
        }
        const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.fragment = rig.sh("primid.frag");
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 18, 1, 0, 0); }));
    });
    CaseSpec bary = spec_of("fragment_barycentrics");
    bary.extension("VK_KHR_fragment_shader_barycentric").min_version(1, 2);
    Cases::add(
        std::move(bary),
        [](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            const std::vector<Vertex2> tris = {vertex(-0.9f, -0.8f, 0), vertex(0.1f, -0.7f, 1), vertex(-0.4f, 0.1f, 2),
                                               vertex(0.2f, 0.0f, 3),   vertex(0.9f, 0.1f, 4),  vertex(0.5f, 0.9f, 5)};
            const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            PipelineDesc pipe = rig.pos2c(target);
            pipe.fragment = rig.sh("bary.v12.frag");
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0); }));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR);
            return f.fragmentShaderBarycentric != VK_FALSE ? std::string() : std::string("fragmentShaderBarycentric");
        });
}

void register_shading_rate() {
    auto caps = [](Kit& kit) -> std::string {
        const auto f = kit.query_features<VkPhysicalDeviceFragmentShadingRateFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR);
        return f.pipelineFragmentShadingRate != VK_FALSE ? std::string() : std::string("pipelineFragmentShadingRate");
    };
    for (int dynamic = 0; dynamic < 2; ++dynamic) {
        CaseSpec spec = spec_of(dynamic != 0 ? "shading_rate_dynamic" : "shading_rate_pipeline");
        spec.extension("VK_KHR_fragment_shading_rate").min_version(1, 2);
        Cases::add(std::move(spec), [dynamic](Rig& rig, CaptureSet& out) {
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            const std::vector<Vertex2> quad = {vertex(-0.95f, -0.95f, 0), vertex(0.95f, -0.95f, 1), vertex(-0.95f, 0.95f, 2),
                                               vertex(0.95f, -0.95f, 1),  vertex(0.95f, 0.95f, 3),  vertex(-0.95f, 0.95f, 2)};
            const Buffer vb = rig.buffer(quad, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            VkPipelineFragmentShadingRateStateCreateInfoKHR rate{};
            rate.sType = VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR;
            rate.fragmentSize = {2, 2};
            rate.combinerOps[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
            rate.combinerOps[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
            PipelineDesc pipe = rig.pos2c(target);
            pipe.fragment = rig.sh("shadingrate.v12.frag");
            pipe.next = &rate;
            if (dynamic != 0) {
                pipe.dynamic = {VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR};
            }
            const VkPipeline pipeline = rig.pipe(pipe);
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) {
                if (dynamic == 0) {
                    rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                    return;
                }
                const VkExtent2D sizes[4] = {{1, 1}, {1, 2}, {2, 1}, {2, 2}};
                const VkFragmentShadingRateCombinerOpKHR ops[2] = {VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR};
                for (unsigned i = 0; i < 4; ++i) {
                    rig.viewport(cb, static_cast<float>(i % 2) * 64.0f, static_cast<float>(i / 2) * 64.0f, 64, 64);
                    rig.kit.api.vkCmdSetFragmentShadingRateKHR(cb, &sizes[i], ops);
                    rig.kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                }
            }));
        }, caps);
    }
}

const int registered = (register_tessellation(), register_geometry(), register_layered(), register_shader_builtins(), register_shading_rate(), 0);

}  // namespace

}  // namespace lsw::render::vk
