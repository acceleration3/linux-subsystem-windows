#include "render/common/pattern.hpp"
#include "render/vk/quadkit.hpp"

#include <cmath>
#include <thread>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.09f, 1.0f};

CaseSpec spec_of(const std::string& name) {
    CaseSpec spec("sync", name);
    spec.tolerance(Tolerance::exact());
    return spec;
}

struct SampleConstants {
    float scale[4];
    float bias[4];
    std::int32_t info[4];
};

Image sample_to_target(Rig& rig, const Texture& source, std::uint32_t size) {
    Target& target = rig.make_target(TargetDesc{});
    const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    DescriptorWrite write;
    write.binding = 0;
    write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.image = {sampler, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorSet set = rig.dset(set_layout, {write});
    const VkPipelineLayout layout = rig.playout({set_layout}, sizeof(SampleConstants), VK_SHADER_STAGE_FRAGMENT_BIT);
    PipelineDesc pipe;
    pipe.vertex = rig.sh("full.vert");
    pipe.fragment = rig.sh("tex_f.frag");
    pipe.layout = layout;
    pipe.pass = target.clear_pass;
    const VkPipeline pipeline = rig.pipe(pipe);
    SampleConstants constants{{1, 1, 1, 1}, {0, 0, 0, 0}, {static_cast<std::int32_t>(size), static_cast<std::int32_t>(size), 0, 0}};
    return rig.draw(target, pipeline, nullptr, [&](VkCommandBuffer cb) {
        rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, &constants, sizeof constants);
        rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
    }, layout, {set});
}

struct SamplePlan {
    Target* target = nullptr;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    SampleConstants constants{};

    void record(Rig& rig, VkCommandBuffer cb) const {
        target->begin(rig.kit, cb, true);
        rig.bind_graphics(cb, pipeline, layout, {set});
        rig.push(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, &constants, sizeof constants);
        rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        target->end(rig.kit, cb);
    }
};

SamplePlan plan_sampling(Rig& rig, const Texture& source, std::uint32_t size) {
    SamplePlan plan;
    plan.target = &rig.make_target(TargetDesc{});
    const VkSampler sampler = rig.sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    DescriptorWrite write;
    write.binding = 0;
    write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.image = {sampler, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    plan.set = rig.dset(set_layout, {write});
    plan.layout = rig.playout({set_layout}, sizeof(SampleConstants), VK_SHADER_STAGE_FRAGMENT_BIT);
    PipelineDesc pipe;
    pipe.vertex = rig.sh("full.vert");
    pipe.fragment = rig.sh("tex_f.frag");
    pipe.layout = plan.layout;
    pipe.pass = plan.target->clear_pass;
    plan.pipeline = rig.pipe(pipe);
    plan.constants = SampleConstants{{1, 1, 1, 1}, {0, 0, 0, 0}, {static_cast<std::int32_t>(size), static_cast<std::int32_t>(size), 0, 0}};
    return plan;
}

struct HeavyProducer {
    Texture texture;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::unique_ptr<QuadKit> quads;
    std::uint32_t size = 64;

    void record(Rig& rig, VkCommandBuffer cb) const {
        VkClearValue clear{};
        std::copy(kClear, kClear + 4, clear.color.float32);
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = pass;
        begin.framebuffer = framebuffer;
        begin.renderArea = {{0, 0}, {size, size}};
        begin.clearValueCount = 1;
        begin.pClearValues = &clear;
        rig.kit.api.vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
        rig.viewport(cb, 0, 0, static_cast<float>(size), static_cast<float>(size));
        rig.scissor(cb, 0, 0, size, size);
        rig.bind_graphics(cb, pipeline);
        for (unsigned i = 0; i < 400; ++i) {
            const float c[4] = {palette(i % 5)[0], palette(i % 5)[1], palette(i % 5)[2], 0.3f};
            const float t = static_cast<float>(i) * 0.01f;
            quads->quad(cb, -1.0f + 0.2f * std::sin(t * 3.0f), -1.0f + 0.2f * std::cos(t * 2.0f), 1.6f + 0.4f * std::sin(t), 1.6f + 0.4f * std::cos(t), c);
        }
        rig.kit.api.vkCmdEndRenderPass(cb);
    }
};

HeavyProducer make_producer(Rig& rig, std::uint32_t size) {
    HeavyProducer producer;
    producer.size = size;
    TextureDesc desc;
    desc.width = desc.height = size;
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    producer.texture = rig.texture(desc);
    VkAttachmentDescription a{};
    a.format = VK_FORMAT_R8G8B8A8_UNORM;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
    producer.pass = rig.render_pass(rp);
    producer.framebuffer = rig.framebuffer(producer.pass, {producer.texture.view}, size, size);
    TargetDesc td;
    td.width = td.height = size;
    Target& shape = rig.make_target(td);
    producer.quads = std::make_unique<QuadKit>(rig);
    PipelineDesc pipe = producer.quads->desc(shape);
    pipe.pass = producer.pass;
    pipe.blend = true;
    producer.pipeline = rig.pipe(pipe);
    return producer;
}

void register_barriers() {
    Cases::add(spec_of("barrier_attachment_to_sampling").audit_ok("legacy barrier, the same heavy scene as barrier2_attachment_to_sampling through vkCmdPipelineBarrier; a missing barrier races the producer"), [](Rig& rig, CaptureSet& out) {
        const HeavyProducer producer = make_producer(rig, 64);
        const SamplePlan plan = plan_sampling(rig, producer.texture, 64);
        rig.run([&](VkCommandBuffer cb) {
            producer.record(rig, cb);
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = producer.texture.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            rig.kit.api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                                             nullptr, 1, &b);
            plan.record(rig, cb);
        });
        out.add("", rig.color(*plan.target));
    });
    CaseSpec sync2 = spec_of("barrier2_attachment_to_sampling");
    sync2.audit_ok("synchronization2 barrier, the same heavy scene as barrier_attachment_to_sampling through vkCmdPipelineBarrier2; the subject is the API path");
    sync2.min_version(1, 3);
    Cases::add(std::move(sync2), [](Rig& rig, CaptureSet& out) {
        const HeavyProducer producer = make_producer(rig, 64);
        const SamplePlan plan = plan_sampling(rig, producer.texture, 64);
        rig.run([&](VkCommandBuffer cb) {
            producer.record(rig, cb);
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = producer.texture.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            rig.kit.api.vkCmdPipelineBarrier2(cb, &dep);
            plan.record(rig, cb);
        });
        out.add("", rig.color(*plan.target));
    });
    Cases::add(spec_of("layout_chain_clear_compute_sample"), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TextureDesc desc;
        desc.width = desc.height = 64;
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        const Texture texture = rig.texture(desc);
        const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}});
        DescriptorWrite write;
        write.binding = 0;
        write.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.image = {VK_NULL_HANDLE, texture.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorSet set = rig.dset(set_layout, {write});
        const VkPipelineLayout layout = rig.playout({set_layout}, 16, VK_SHADER_STAGE_COMPUTE_BIT);
        const VkPipeline compute = rig.compute("fill.comp", layout);
        rig.run([&](VkCommandBuffer cb) {
            kit.barrier(cb, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const VkClearColorValue clear = {{0.9f, 0.8f, 0.1f, 1.0f}};
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            kit.api.vkCmdClearColorImage(cb, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
            kit.barrier(cb, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute);
            kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
            const float rows[4] = {40.0f, 0, 0, 0};
            rig.push(cb, layout, VK_SHADER_STAGE_COMPUTE_BIT, rows, 16);
            kit.api.vkCmdDispatch(cb, 8, 8, 1);
            kit.barrier(cb, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        out.add("", sample_to_target(rig, texture, 64));
    });
}

void generate_and_draw(Rig& rig, Target& target, VkPipeline compute, VkPipelineLayout compute_layout, VkDescriptorSet set, const Buffer& verts,
                       VkPipeline graphics, float param, const std::function<void(VkCommandBuffer)>& sync) {
    rig.run([&](VkCommandBuffer cb) {
        rig.kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute);
        rig.kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout, 0, 1, &set, 0, nullptr);
        const float params[4] = {param, 0, 0, 0};
        rig.push(cb, compute_layout, VK_SHADER_STAGE_COMPUTE_BIT, params, 16);
        rig.kit.api.vkCmdDispatch(cb, 1, 1, 1);
        sync(cb);
        target.begin(rig.kit, cb, true);
        rig.bind_graphics(cb, graphics);
        rig.bind_vertex(cb, verts);
        rig.kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
        target.end(rig.kit, cb);
    });
}

struct ComputeVertices {
    Target* target = nullptr;
    Buffer verts;
    VkPipeline compute = VK_NULL_HANDLE;
    VkPipelineLayout compute_layout = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipeline graphics = VK_NULL_HANDLE;
};

ComputeVertices make_compute_vertices(Rig& rig) {
    ComputeVertices c;
    c.target = &rig.make_target(TargetDesc{});
    std::copy(kClear, kClear + 4, c.target->clear_color);
    c.verts = rig.blank(96, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false);
    const VkDescriptorSetLayout set_layout = rig.dlayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}});
    DescriptorWrite write;
    write.binding = 0;
    write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.buffer = {c.verts.buffer, 0, VK_WHOLE_SIZE};
    c.set = rig.dset(set_layout, {write});
    c.compute_layout = rig.playout({set_layout}, 16, VK_SHADER_STAGE_COMPUTE_BIT);
    c.compute = rig.compute("gen.comp", c.compute_layout);
    PipelineDesc pipe = rig.pos2c(*c.target);
    pipe.bindings = {{0, 32, VK_VERTEX_INPUT_RATE_VERTEX}};
    pipe.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
    c.graphics = rig.pipe(pipe);
    return c;
}

void register_events() {
    Cases::add(spec_of("event_set_wait_compute_to_vertex"), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        ComputeVertices c = make_compute_vertices(rig);
        VkEventCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
        VkEvent event = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateEvent(kit.device, &info, nullptr, &event), "vkCreateEvent");
        kit.defer([&kit, event] { kit.api.vkDestroyEvent(kit.device, event, nullptr); });
        generate_and_draw(rig, *c.target, c.compute, c.compute_layout, c.set, c.verts, c.graphics, 1.0f, [&](VkCommandBuffer cb) {
            kit.api.vkCmdSetEvent(cb, event, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            kit.api.vkCmdWaitEvents(cb, 1, &event, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 1, &b, 0, nullptr, 0, nullptr);
        });
        out.add("", rig.color(*c.target));
    });
    CaseSpec events2 = spec_of("event2_set_wait_compute_to_vertex");
    events2.min_version(1, 3);
    Cases::add(std::move(events2), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        ComputeVertices c = make_compute_vertices(rig);
        VkEventCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
        VkEvent event = VK_NULL_HANDLE;
        rig.vk_ok(kit.api.vkCreateEvent(kit.device, &info, nullptr, &event), "vkCreateEvent");
        kit.defer([&kit, event] { kit.api.vkDestroyEvent(kit.device, event, nullptr); });
        generate_and_draw(rig, *c.target, c.compute, c.compute_layout, c.set, c.verts, c.graphics, 2.0f, [&](VkCommandBuffer cb) {
            VkMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
            b.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &b;
            kit.api.vkCmdSetEvent2(cb, event, &dep);
            kit.api.vkCmdWaitEvents2(cb, 1, &event, &dep);
        });
        out.add("", rig.color(*c.target));
    });
}

class SemaphoreScene : public RigScene {
public:
    explicit SemaphoreScene(bool timeline) : timeline_(timeline) {}

protected:
    std::string caps(Kit& kit) override {
        if (kit.compute_family == ~0u) {
            return "no compute-only queue family";
        }
        if (timeline_) {
            const auto f = kit.query_features<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            return f.timelineSemaphore != VK_FALSE ? std::string() : std::string("timelineSemaphore");
        }
        return {};
    }

    void init(Rig& rig) override {
        Kit& kit = rig.kit;
        kit.share_families = {kit.family, kit.compute_family};
        c_ = make_compute_vertices(rig);
        kit.share_families.clear();
        VkSemaphoreTypeCreateInfo type{};
        type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        info.pNext = timeline_ ? &type : nullptr;
        rig.vk_ok(kit.api.vkCreateSemaphore(kit.device, &info, nullptr, &sem_), "vkCreateSemaphore");
        Kit* kit_ptr = &kit;
        const VkSemaphore sem = sem_;
        kit.defer([kit_ptr, sem] { kit_ptr->api.vkDestroySemaphore(kit_ptr->device, sem, nullptr); });
        std::string error;
        compute_cb_ = kit.allocate_command(VK_COMMAND_BUFFER_LEVEL_PRIMARY, error, true);
        rig.must(compute_cb_ != VK_NULL_HANDLE, error);
        graphics_cb_ = kit.allocate_command(VK_COMMAND_BUFFER_LEVEL_PRIMARY, error);
        rig.must(graphics_cb_ != VK_NULL_HANDLE, error);
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        rig.vk_ok(kit.api.vkCreateFence(kit.device, &fence_info, nullptr, &fence_), "vkCreateFence");
        const VkFence fence = fence_;
        kit.defer([kit_ptr, fence] { kit_ptr->api.vkDestroyFence(kit_ptr->device, fence, nullptr); });
    }

    void step(Rig& rig, unsigned frame) override {
        Kit& kit = rig.kit;
        const float param = static_cast<float>(frame);
        const auto record = [&](VkCommandBuffer cb, const std::function<void(VkCommandBuffer)>& fn) {
            rig.vk_ok(kit.api.vkResetCommandBuffer(cb, 0), "vkResetCommandBuffer");
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            rig.vk_ok(kit.api.vkBeginCommandBuffer(cb, &begin), "vkBeginCommandBuffer");
            fn(cb);
            rig.vk_ok(kit.api.vkEndCommandBuffer(cb), "vkEndCommandBuffer");
        };
        record(compute_cb_, [&](VkCommandBuffer cb) {
            kit.api.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, c_.compute);
            kit.api.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, c_.compute_layout, 0, 1, &c_.set, 0, nullptr);
            const float params[4] = {param, 0, 0, 0};
            rig.push(cb, c_.compute_layout, VK_SHADER_STAGE_COMPUTE_BIT, params, 16);
            kit.api.vkCmdDispatch(cb, 1, 1, 1);
        });
        record(graphics_cb_, [&](VkCommandBuffer cb) {
            c_.target->begin(kit, cb, frame == 0);
            rig.bind_graphics(cb, c_.graphics);
            rig.bind_vertex(cb, c_.verts);
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            c_.target->end(kit, cb);
        });
        const std::uint64_t base = static_cast<std::uint64_t>(frame) * 2;
        std::uint64_t compute_signal = base + 1;
        std::uint64_t graphics_wait = base + 1;
        std::uint64_t graphics_signal = base + 2;
        VkTimelineSemaphoreSubmitInfo compute_timeline{};
        compute_timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        compute_timeline.signalSemaphoreValueCount = 1;
        compute_timeline.pSignalSemaphoreValues = &compute_signal;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.pNext = timeline_ ? &compute_timeline : nullptr;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &compute_cb_;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &sem_;
        rig.vk_ok(kit.api.vkQueueSubmit(kit.compute_queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
        VkTimelineSemaphoreSubmitInfo graphics_timeline{};
        graphics_timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        graphics_timeline.waitSemaphoreValueCount = 1;
        graphics_timeline.pWaitSemaphoreValues = &graphics_wait;
        graphics_timeline.signalSemaphoreValueCount = 1;
        graphics_timeline.pSignalSemaphoreValues = &graphics_signal;
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        VkSubmitInfo gfx{};
        gfx.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        gfx.pNext = timeline_ ? &graphics_timeline : nullptr;
        gfx.waitSemaphoreCount = 1;
        gfx.pWaitSemaphores = &sem_;
        gfx.pWaitDstStageMask = &stage;
        gfx.commandBufferCount = 1;
        gfx.pCommandBuffers = &graphics_cb_;
        if (timeline_) {
            gfx.signalSemaphoreCount = 1;
            gfx.pSignalSemaphores = &sem_;
        }
        rig.vk_ok(kit.api.vkQueueSubmit(kit.queue, 1, &gfx, fence_), "vkQueueSubmit");
        if (timeline_) {
            VkSemaphoreWaitInfo wait{};
            wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wait.semaphoreCount = 1;
            wait.pSemaphores = &sem_;
            wait.pValues = &graphics_signal;
            rig.vk_ok(kit.api.vkWaitSemaphores(kit.device, &wait, 30000000000ull), "vkWaitSemaphores");
        }
        rig.vk_ok(kit.api.vkWaitForFences(kit.device, 1, &fence_, VK_TRUE, 30000000000ull), "vkWaitForFences");
        rig.vk_ok(kit.api.vkResetFences(kit.device, 1, &fence_), "vkResetFences");
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*c_.target)); }

private:
    bool timeline_;
    ComputeVertices c_;
    VkSemaphore sem_ = VK_NULL_HANDLE;
    VkCommandBuffer compute_cb_ = VK_NULL_HANDLE;
    VkCommandBuffer graphics_cb_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};

class RingFlightScene : public RigScene {
protected:
    void init(Rig& rig) override {
        target_ = &rig.make_target(TargetDesc{});
        target_->clear_color[0] = 0.05f;
        target_->clear_color[1] = 0.05f;
        target_->clear_color[2] = 0.08f;
        ring_ = rig.blank(kSlots * kSlotBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        pipeline_ = rig.pipe(rig.pos2c(*target_));
        for (unsigned i = 0; i < kSlots; ++i) {
            std::string error;
            cbs_[i] = rig.kit.allocate_command(VK_COMMAND_BUFFER_LEVEL_PRIMARY, error);
            rig.must(cbs_[i] != VK_NULL_HANDLE, error);
            VkFenceCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            rig.vk_ok(rig.kit.api.vkCreateFence(rig.kit.device, &info, nullptr, &fences_[i]), "vkCreateFence");
            Kit* kit = &rig.kit;
            const VkFence fence = fences_[i];
            rig.kit.defer([kit, fence] { kit->api.vkDestroyFence(kit->device, fence, nullptr); });
        }
    }

    void step(Rig& rig, unsigned frame) override {
        Kit& kit = rig.kit;
        const unsigned slot = frame % kSlots;
        rig.vk_ok(kit.api.vkWaitForFences(kit.device, 1, &fences_[slot], VK_TRUE, 30000000000ull), "vkWaitForFences");
        rig.vk_ok(kit.api.vkResetFences(kit.device, 1, &fences_[slot]), "vkResetFences");
        Vertex2 v[6];
        const float angle = static_cast<float>(frame) * 0.7854f;
        for (unsigned t = 0; t < 2; ++t) {
            const float a = angle + static_cast<float>(t) * 3.1416f;
            const float cx = 0.55f * std::cos(a);
            const float cy = 0.55f * std::sin(a);
            v[t * 3] = vertex(cx, cy + 0.2f, frame + t);
            v[t * 3 + 1] = vertex(cx - 0.17f, cy - 0.12f, frame + t + 2);
            v[t * 3 + 2] = vertex(cx + 0.17f, cy - 0.12f, frame + t + 4);
        }
        std::string error;
        rig.must(kit.write(ring_, slot * kSlotBytes, v, sizeof v, error), error);
        const VkCommandBuffer cb = cbs_[slot];
        rig.vk_ok(kit.api.vkResetCommandBuffer(cb, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        rig.vk_ok(kit.api.vkBeginCommandBuffer(cb, &begin), "vkBeginCommandBuffer");
        target_->begin(kit, cb, frame == 0);
        rig.bind_graphics(cb, pipeline_);
        rig.bind_vertex(cb, ring_, slot * kSlotBytes);
        kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
        target_->end(kit, cb);
        rig.vk_ok(kit.api.vkEndCommandBuffer(cb), "vkEndCommandBuffer");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cbs_[slot];
        rig.vk_ok(kit.api.vkQueueSubmit(kit.queue, 1, &submit, fences_[slot]), "vkQueueSubmit");
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    static constexpr unsigned kSlots = 3;
    static constexpr VkDeviceSize kSlotBytes = 6 * sizeof(Vertex2);
    Target* target_ = nullptr;
    Buffer ring_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandBuffer cbs_[kSlots] = {};
    VkFence fences_[kSlots] = {};
};

void register_submission() {
    Cases::add_scene(spec_of("binary_semaphore_two_submits").audit_ok("compute queue triangle ordered by a binary semaphore, the same generated triangle as the timeline row; the subject is the semaphore type").frames(4, {0, 1, 3}), [] { return std::make_unique<SemaphoreScene>(false); });
    Cases::add_scene(spec_of("timeline_semaphore_compute_queue").audit_ok("compute queue triangle ordered by a timeline semaphore, the same generated triangle as the binary row; the subject is the semaphore type").frames(5, {0, 2, 4}).min_version(1, 2), [] { return std::make_unique<SemaphoreScene>(true); });
    Cases::add_class<RingFlightScene>(spec_of("frames_in_flight_ring").frames(8, {0, 3, 7}));
    Cases::add(spec_of("secondary_command_buffers_threads"), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        const VkPipeline pipeline = rig.pipe(quads.desc(target));
        constexpr unsigned kThreads = 4;
        VkCommandPool pools[kThreads] = {};
        VkCommandBuffer secondaries[kThreads] = {};
        for (unsigned i = 0; i < kThreads; ++i) {
            VkCommandPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_info.queueFamilyIndex = kit.family;
            rig.vk_ok(kit.api.vkCreateCommandPool(kit.device, &pool_info, nullptr, &pools[i]), "vkCreateCommandPool");
            const VkCommandPool pool = pools[i];
            kit.defer([&kit, pool] { kit.api.vkDestroyCommandPool(kit.device, pool, nullptr); });
            VkCommandBufferAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc.commandPool = pools[i];
            alloc.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            alloc.commandBufferCount = 1;
            rig.vk_ok(kit.api.vkAllocateCommandBuffers(kit.device, &alloc, &secondaries[i]), "vkAllocateCommandBuffers");
        }
        std::vector<std::thread> threads;
        std::vector<VkResult> results(kThreads, VK_SUCCESS);
        for (unsigned i = 0; i < kThreads; ++i) {
            threads.emplace_back([&, i] {
                VkCommandBufferInheritanceInfo inherit{};
                inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
                inherit.renderPass = target.clear_pass;
                inherit.framebuffer = target.framebuffer;
                VkCommandBufferBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                begin.pInheritanceInfo = &inherit;
                results[i] = kit.api.vkBeginCommandBuffer(secondaries[i], &begin);
                if (results[i] != VK_SUCCESS) {
                    return;
                }
                rig.viewport(secondaries[i], 0, 0, 128, 128);
                rig.scissor(secondaries[i], 0, 0, 128, 128);
                kit.api.vkCmdBindPipeline(secondaries[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                for (unsigned q = 0; q < 6; ++q) {
                    quads.quad(secondaries[i], -0.95f + 0.47f * static_cast<float>(i), -0.9f + 0.3f * static_cast<float>(q), 0.42f, 0.26f, palette(i + q));
                }
                results[i] = kit.api.vkEndCommandBuffer(secondaries[i]);
            });
        }
        for (std::thread& t : threads) {
            t.join();
        }
        for (VkResult r : results) {
            rig.vk_ok(r, "secondary command buffer recording");
        }
        rig.run([&](VkCommandBuffer cb) {
            VkClearValue clear{};
            std::copy(kClear, kClear + 4, clear.color.float32);
            VkRenderPassBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            begin.renderPass = target.clear_pass;
            begin.framebuffer = target.framebuffer;
            begin.renderArea = {{0, 0}, {128, 128}};
            begin.clearValueCount = 1;
            begin.pClearValues = &clear;
            kit.api.vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
            kit.api.vkCmdExecuteCommands(cb, kThreads, secondaries);
            kit.api.vkCmdEndRenderPass(cb);
        });
        out.add("", rig.color(target));
    });
}

const int registered = (register_barriers(), register_events(), register_submission(), 0);

}  // namespace

}  // namespace lsw::render::vk
