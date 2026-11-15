#include "render/vk/quadkit.hpp"

#include <cmath>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.04f, 0.05f, 0.09f, 1.0f};

CaseSpec spec_of(const std::string& name) {
    CaseSpec spec("msaa", name);
    spec.tolerance(Tolerance::exact());
    return spec;
}

std::vector<Vertex2> edge_triangles() {
    return {vertex(-0.9f, -0.75f, 0), vertex(0.35f, -0.9f, 1), vertex(-0.2f, 0.6f, 2),
            vertex(0.1f, 0.85f, 3),   vertex(0.9f, 0.35f, 4),  vertex(0.75f, -0.55f, 5),
            vertex(-0.7f, 0.3f, 6),   vertex(-0.55f, 0.9f, 7), vertex(0.0f, 0.1f, 0)};
}

std::string sample_caps(Kit& kit, VkSampleCountFlagBits samples) {
    if ((kit.properties.limits.framebufferColorSampleCounts & samples) == 0) {
        return "framebufferColorSampleCounts";
    }
    VkImageFormatProperties props{};
    if (kit.api.vkGetPhysicalDeviceImageFormatProperties(kit.physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0, &props) != VK_SUCCESS ||
        (props.sampleCounts & samples) == 0) {
        return "RGBA8 sample count";
    }
    return {};
}

void register_counts() {
    for (VkSampleCountFlagBits samples : {VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT}) {
        Cases::add(spec_of("samples_" + std::to_string(static_cast<int>(samples))), [samples](Rig& rig, CaptureSet& out) {
            TargetDesc td;
            td.samples = samples;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            const auto tris = edge_triangles();
            const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const VkPipeline pipeline = rig.pipe(rig.pos2c(target));
            out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 9, 1, 0, 0); }));
        }, [samples](Kit& kit) { return sample_caps(kit, samples); });
    }
}

void register_states() {
    Cases::add(spec_of("alpha_to_coverage"), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.samples = VK_SAMPLE_COUNT_4_BIT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.fragment = rig.sh("atoc.frag");
        pipe.alpha_to_coverage = true;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -0.9f, -0.9f, 1.8f, 0.8f, palette(0));
            quads.quad(cb, -0.9f, 0.1f, 1.8f, 0.8f, palette(2));
        }, quads.layout));
    }, [](Kit& kit) { return sample_caps(kit, VK_SAMPLE_COUNT_4_BIT); });
    CaseSpec one = spec_of("alpha_to_one");
    one.feature("alphaToOne");
    Cases::add(std::move(one), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.samples = VK_SAMPLE_COUNT_4_BIT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.fragment = rig.sh("atoc.frag");
        pipe.alpha_to_coverage = true;
        pipe.alpha_to_one = true;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -0.9f, -0.9f, 1.8f, 0.8f, palette(1));
            quads.quad(cb, -0.9f, 0.1f, 1.8f, 0.8f, palette(3));
        }, quads.layout));
    }, [](Kit& kit) { return sample_caps(kit, VK_SAMPLE_COUNT_4_BIT); });
    Cases::add(spec_of("sample_mask"), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.samples = VK_SAMPLE_COUNT_4_BIT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        static const VkSampleMask masks[3] = {0x5, 0xA, 0x3};
        std::vector<VkPipeline> pipes;
        for (const VkSampleMask& mask : masks) {
            PipelineDesc pipe = quads.desc(target);
            pipe.sample_mask = &mask;
            pipes.push_back(rig.pipe(pipe));
        }
        out.add("", rig.draw(target, pipes[0], nullptr, [&](VkCommandBuffer cb) {
            quads.quad(cb, -0.9f, -0.9f, 1.2f, 1.2f, palette(0));
            rig.bind_graphics(cb, pipes[1]);
            quads.quad(cb, -0.3f, -0.5f, 1.2f, 1.2f, palette(2));
            rig.bind_graphics(cb, pipes[2]);
            quads.quad(cb, -0.6f, 0.2f, 1.4f, 0.7f, palette(4));
        }, quads.layout));
    }, [](Kit& kit) { return sample_caps(kit, VK_SAMPLE_COUNT_4_BIT); });
    CaseSpec shading = spec_of("sample_shading");
    shading.feature("sampleRateShading");
    Cases::add(std::move(shading), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.samples = VK_SAMPLE_COUNT_4_BIT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        const auto tris = edge_triangles();
        const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        PipelineDesc pipe = rig.pos2c(target);
        pipe.fragment = rig.sh("sampleid.frag");
        pipe.sample_shading = true;
        pipe.min_sample_shading = 1.0f;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 9, 1, 0, 0); }));
    }, [](Kit& kit) { return sample_caps(kit, VK_SAMPLE_COUNT_4_BIT); });
}

Texture resolve_target(Rig& rig, VkSampleCountFlagBits samples, std::uint32_t size, VkFormat format, VkImageUsageFlags usage) {
    TextureDesc desc;
    desc.width = desc.height = size;
    desc.format = format;
    desc.samples = samples;
    desc.usage = usage;
    return rig.texture(desc);
}

void register_resolve() {
    Cases::add(spec_of("resolve_image_command"), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        const Texture ms = resolve_target(rig, VK_SAMPLE_COUNT_4_BIT, 128, VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        TextureDesc single;
        single.width = single.height = 128;
        single.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        single.create_view = false;
        const Texture resolved = rig.texture(single);
        VkAttachmentDescription a{};
        a.format = VK_FORMAT_R8G8B8A8_UNORM;
        a.samples = VK_SAMPLE_COUNT_4_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &a;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        const VkRenderPass pass = rig.render_pass(rp);
        const VkFramebuffer fb = rig.framebuffer(pass, {ms.view}, 128, 128);
        const auto tris = edge_triangles();
        const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        TargetDesc dummy;
        dummy.samples = VK_SAMPLE_COUNT_4_BIT;
        PipelineDesc pipe = rig.pos2c(rig.make_target(dummy));
        pipe.pass = pass;
        const VkPipeline pipeline = rig.pipe(pipe);
        rig.run([&](VkCommandBuffer cb) {
            VkClearValue clear{};
            std::copy(kClear, kClear + 4, clear.color.float32);
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
            kit.api.vkCmdDraw(cb, 9, 1, 0, 0);
            kit.api.vkCmdEndRenderPass(cb);
            kit.barrier(cb, resolved.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue border{};
            border.float32[0] = 0.5f;
            border.float32[1] = 0.25f;
            border.float32[2] = 0.125f;
            border.float32[3] = 1.0f;
            const VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            kit.api.vkCmdClearColorImage(cb, resolved.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &border, 1, &whole);
            VkImageResolve region{};
            region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.srcOffset = {8, 8, 0};
            region.dstOffset = {4, 4, 0};
            region.extent = {112, 112, 1};
            kit.api.vkCmdResolveImage(cb, ms.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resolved.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            kit.barrier(cb, resolved.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        });
        out.add("", rig.read(resolved, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
    }, [](Kit& kit) { return sample_caps(kit, VK_SAMPLE_COUNT_4_BIT); });
    CaseSpec sample_locations = spec_of("sample_locations_custom");
    sample_locations.extension("VK_EXT_sample_locations");
    Cases::add(std::move(sample_locations), [](Rig& rig, CaptureSet& out) {
        TargetDesc td;
        td.samples = VK_SAMPLE_COUNT_4_BIT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        const auto tris = edge_triangles();
        const Buffer vb = rig.buffer(tris, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const VkSampleLocationEXT locations[4] = {{0.25f, 0.25f}, {0.75f, 0.25f}, {0.25f, 0.75f}, {0.875f, 0.875f}};
        VkPipelineSampleLocationsStateCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT;
        info.sampleLocationsEnable = VK_TRUE;
        info.sampleLocationsInfo.sType = VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT;
        info.sampleLocationsInfo.sampleLocationsPerPixel = VK_SAMPLE_COUNT_4_BIT;
        info.sampleLocationsInfo.sampleLocationGridSize = {1, 1};
        info.sampleLocationsInfo.sampleLocationsCount = 4;
        info.sampleLocationsInfo.pSampleLocations = locations;
        PipelineDesc pipe = rig.pos2c(target);
        pipe.multisample_next = &info;
        const VkPipeline pipeline = rig.pipe(pipe);
        out.add("", rig.draw(target, pipeline, &vb, [&](VkCommandBuffer cb) { rig.kit.api.vkCmdDraw(cb, 9, 1, 0, 0); }));
    }, [](Kit& kit) -> std::string {
        const auto p = kit.query_properties<VkPhysicalDeviceSampleLocationsPropertiesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT);
        return (p.sampleLocationSampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0 ? sample_caps(kit, VK_SAMPLE_COUNT_4_BIT) : std::string("sample locations at 4x");
    });
}

class MsaaAccumulate : public RigScene {
protected:
    std::string caps(Kit& kit) override { return sample_caps(kit, VK_SAMPLE_COUNT_4_BIT); }

    void init(Rig& rig) override {
        TargetDesc td;
        td.samples = VK_SAMPLE_COUNT_4_BIT;
        target_ = &rig.make_target(td);
        std::copy(kClear, kClear + 4, target_->clear_color);
        pipeline_ = rig.pipe(rig.pos2c(*target_));
    }

    void step(Rig& rig, unsigned frame) override {
        const float a = 0.55f * static_cast<float>(frame);
        std::vector<Vertex2> tri;
        for (int i = 0; i < 3; ++i) {
            const float t = a + 2.0944f * static_cast<float>(i);
            tri.push_back(vertex(0.35f * std::cos(a) + 0.5f * std::cos(t), 0.35f * std::sin(a) + 0.5f * std::sin(t), frame + static_cast<unsigned>(i)));
        }
        const Buffer vb = rig.buffer(tri, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        rig.run([&](VkCommandBuffer cb) {
            target_->begin(rig.kit, cb, frame == 0);
            rig.bind_graphics(cb, pipeline_);
            rig.bind_vertex(cb, vb);
            rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            target_->end(rig.kit, cb);
        });
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

const int registered = (register_counts(), register_states(), register_resolve(),
                        Cases::add_class<MsaaAccumulate>(spec_of("accumulate_resolved_frames").frames(5, {0, 2, 4})), 0);

}  // namespace

}  // namespace lsw::render::vk
