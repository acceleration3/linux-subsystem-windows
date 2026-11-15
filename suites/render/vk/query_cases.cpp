#include "render/vk/quadkit.hpp"

#include <cmath>

namespace lsw::render::vk {

namespace {

const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};

CaseSpec spec_of(const std::string& name) {
    CaseSpec spec("query", name);
    spec.tolerance(Tolerance::exact());
    return spec;
}

VkQueryPool make_pool(Rig& rig, VkQueryType type, std::uint32_t count, VkQueryPipelineStatisticFlags stats = 0) {
    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = type;
    info.queryCount = count;
    info.pipelineStatistics = stats;
    VkQueryPool pool = VK_NULL_HANDLE;
    rig.vk_ok(rig.kit.api.vkCreateQueryPool(rig.kit.device, &info, nullptr, &pool), "vkCreateQueryPool");
    Kit* kit = &rig.kit;
    rig.kit.defer([kit, pool] { kit->api.vkDestroyQueryPool(kit->device, pool, nullptr); });
    return pool;
}

class OcclusionFeedback : public RigScene {
protected:
    void init(Rig& rig) override {
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        target_ = &rig.make_target(td);
        std::copy(kClear, kClear + 4, target_->clear_color);
        quads_ = std::make_unique<QuadKit>(rig);
        PipelineDesc pipe = quads_->desc(*target_);
        pipe.depth_test = true;
        pipeline_ = rig.pipe(pipe);
        pool_ = make_pool(rig, VK_QUERY_TYPE_OCCLUSION, 1);
    }

    void step(Rig& rig, unsigned frame) override {
        Kit& kit = rig.kit;
        const float shift = 0.5f * static_cast<float>(frame);
        const float bar_width = 1.8f * static_cast<float>(last_) / 1600.0f;
        rig.run([&](VkCommandBuffer cb) {
            kit.api.vkCmdResetQueryPool(cb, pool_, 0, 1);
            target_->begin(kit, cb, true);
            rig.bind_graphics(cb, pipeline_);
            quads_->quad(cb, -0.9f + shift, -0.9f, 0.7f, 1.2f, palette(3), 0.3f);
            kit.api.vkCmdBeginQuery(cb, pool_, 0, kit.features.occlusionQueryPrecise != VK_FALSE ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
            quads_->quad(cb, -0.8f, -0.6f, 1.6f, 0.8f, palette(2), 0.5f);
            kit.api.vkCmdEndQuery(cb, pool_, 0);
            quads_->quad(cb, -0.9f, 0.5f, std::max(0.02f, bar_width), 0.3f, last_ > 800 ? palette(1) : palette(0), 0.1f);
            target_->end(kit, cb);
        });
        std::uint64_t samples = 0;
        rig.vk_ok(kit.api.vkGetQueryPoolResults(kit.device, pool_, 0, 1, sizeof samples, &samples, sizeof samples, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                  "vkGetQueryPoolResults");
        last_ = samples;
        if (samples == 0) {
            rig.fail("the occlusion query counted no samples for a visible quad");
        }
    }

    void grab(Rig& rig, unsigned, CaptureSet& out) override { out.add("", rig.color(*target_)); }

private:
    Target* target_ = nullptr;
    std::unique_ptr<QuadKit> quads_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    std::uint64_t last_ = 400;
};

void register_queries() {
    Cases::add_class<OcclusionFeedback>(spec_of("occlusion_result_feeds_next_frame").frames(4, {0, 1, 2, 3}));
    CaseSpec cond = spec_of("occlusion_copy_to_conditional_rendering");
    cond.extension("VK_EXT_conditional_rendering");
    Cases::add(
        std::move(cond),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            TargetDesc td;
            td.depth = VK_FORMAT_D32_SFLOAT;
            Target& target = rig.make_target(td);
            std::copy(kClear, kClear + 4, target.clear_color);
            QuadKit quads(rig);
            PipelineDesc pipe = quads.desc(target);
            pipe.depth_test = true;
            const VkPipeline pipeline = rig.pipe(pipe);
            const VkQueryPool pool = make_pool(rig, VK_QUERY_TYPE_OCCLUSION, 2);
            const Buffer results = rig.blank(8, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT, false);
            rig.run([&](VkCommandBuffer cb) {
                kit.api.vkCmdResetQueryPool(cb, pool, 0, 2);
                target.begin(kit, cb, true);
                rig.bind_graphics(cb, pipeline);
                quads.quad(cb, -1, -1, 2, 0.8f, palette(7), 0.2f);
                kit.api.vkCmdBeginQuery(cb, pool, 0, 0);
                quads.quad(cb, -0.8f, -0.9f, 0.6f, 0.5f, palette(0), 0.6f);
                kit.api.vkCmdEndQuery(cb, pool, 0);
                kit.api.vkCmdBeginQuery(cb, pool, 1, 0);
                quads.quad(cb, 0.1f, 0.1f, 0.6f, 0.5f, palette(1), 0.6f);
                kit.api.vkCmdEndQuery(cb, pool, 1);
                target.end(kit, cb);
                kit.api.vkCmdCopyQueryPoolResults(cb, pool, 0, 2, results.buffer, 0, 4, VK_QUERY_RESULT_WAIT_BIT);
                VkBufferMemoryBarrier b{};
                b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                b.dstAccessMask = VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;
                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.buffer = results.buffer;
                b.size = VK_WHOLE_SIZE;
                kit.api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT, 0, 0, nullptr, 1, &b, 0, nullptr);
                target.begin(kit, cb, false);
                rig.bind_graphics(cb, pipeline);
                for (std::uint32_t i = 0; i < 2; ++i) {
                    VkConditionalRenderingBeginInfoEXT begin{};
                    begin.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
                    begin.buffer = results.buffer;
                    begin.offset = i * 4;
                    kit.api.vkCmdBeginConditionalRenderingEXT(cb, &begin);
                    quads.quad(cb, -0.8f + 1.0f * static_cast<float>(i), 0.6f, 0.5f, 0.35f, palette(3 + i), 0.1f);
                    kit.api.vkCmdEndConditionalRenderingEXT(cb);
                }
                target.end(kit, cb);
            });
            out.add("", rig.color(target));
        },
        [](Kit& kit) -> std::string {
            const auto f = kit.query_features<VkPhysicalDeviceConditionalRenderingFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT);
            return f.conditionalRendering != VK_FALSE ? std::string() : std::string("conditionalRendering");
        });
    CaseSpec precise = spec_of("occlusion_precise_vs_boolean");
    precise.feature("occlusionQueryPrecise");
    Cases::add(std::move(precise), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        TargetDesc td;
        td.depth = VK_FORMAT_D32_SFLOAT;
        Target& target = rig.make_target(td);
        std::copy(kClear, kClear + 4, target.clear_color);
        QuadKit quads(rig);
        PipelineDesc pipe = quads.desc(target);
        pipe.depth_test = true;
        const VkPipeline pipeline = rig.pipe(pipe);
        const VkQueryPool pool = make_pool(rig, VK_QUERY_TYPE_OCCLUSION, 4);
        rig.run([&](VkCommandBuffer cb) {
            kit.api.vkCmdResetQueryPool(cb, pool, 0, 4);
            target.begin(kit, cb, true);
            rig.bind_graphics(cb, pipeline);
            quads.quad(cb, -1.0f, -0.2f, 2.0f, 0.6f, palette(7), 0.2f);
            kit.api.vkCmdBeginQuery(cb, pool, 0, VK_QUERY_CONTROL_PRECISE_BIT);
            quads.quad(cb, -0.8f, -0.9f, 0.7f, 0.5f, palette(0), 0.6f);
            kit.api.vkCmdEndQuery(cb, pool, 0);
            kit.api.vkCmdBeginQuery(cb, pool, 1, VK_QUERY_CONTROL_PRECISE_BIT);
            quads.quad(cb, -0.5f, -0.1f, 0.5f, 0.3f, palette(1), 0.6f);
            kit.api.vkCmdEndQuery(cb, pool, 1);
            kit.api.vkCmdBeginQuery(cb, pool, 2, 0);
            quads.quad(cb, 0.1f, 0.5f, 0.8f, 0.4f, palette(2), 0.6f);
            kit.api.vkCmdEndQuery(cb, pool, 2);
            kit.api.vkCmdBeginQuery(cb, pool, 3, VK_QUERY_CONTROL_PRECISE_BIT);
            quads.quad(cb, 0.2f, -0.9f, 0.6f, 0.35f, palette(3), 0.6f);
            quads.quad(cb, 0.4f, -0.8f, 0.5f, 0.3f, palette(4), 0.7f);
            kit.api.vkCmdEndQuery(cb, pool, 3);
            target.end(kit, cb);
        });
        std::uint64_t counts[4] = {};
        rig.vk_ok(kit.api.vkGetQueryPoolResults(kit.device, pool, 0, 4, sizeof counts, counts, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                  "vkGetQueryPoolResults");
        rig.must(counts[0] > 0 && counts[2] > 0, "a visible quad counted no samples");
        rig.must(counts[1] == 0, "a quad hidden by nearer geometry counted samples");
        Image result(4, 1, PixelKind::Rgba32Uint);
        result.set_channel(0, 0, 0, static_cast<double>(counts[0]));
        result.set_channel(1, 0, 0, static_cast<double>(counts[1]));
        result.set_channel(2, 0, 0, counts[2] != 0 ? 1.0 : 0.0);
        result.set_channel(3, 0, 0, static_cast<double>(counts[3]));
        out.add("counts", std::move(result));
        out.add("image", rig.color(target));
    });
    CaseSpec counters = spec_of("pipeline_statistics_counters");
    counters.feature("pipelineStatisticsQuery").same_as({"query.timestamps_and_statistics_leave_image"}, "the image is the geometry of the timestamp row, the counters capture is the subject");
    Cases::add(std::move(counters), [](Rig& rig, CaptureSet& out) {
        Kit& kit = rig.kit;
        Target& target = rig.make_target(TargetDesc{});
        std::copy(kClear, kClear + 4, target.clear_color);
        const std::vector<Vertex2> v = {vertex(-0.8f, -0.7f, 0), vertex(0.7f, -0.6f, 1), vertex(0.0f, 0.8f, 2),
                                        vertex(-0.3f, -0.2f, 3), vertex(0.9f, 0.4f, 4),  vertex(0.1f, 0.9f, 5)};
        const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const VkPipeline pipeline = rig.pipe(rig.pos2c(target));
        const VkQueryPipelineStatisticFlags bits = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
                                                   VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
                                                   VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT;
        const VkQueryPool pool = make_pool(rig, VK_QUERY_TYPE_PIPELINE_STATISTICS, 2, bits);
        rig.run([&](VkCommandBuffer cb) {
            kit.api.vkCmdResetQueryPool(cb, pool, 0, 2);
            target.begin(kit, cb, true);
            rig.bind_graphics(cb, pipeline);
            rig.bind_vertex(cb, vb);
            kit.api.vkCmdBeginQuery(cb, pool, 0, 0);
            kit.api.vkCmdDraw(cb, 3, 1, 0, 0);
            kit.api.vkCmdEndQuery(cb, pool, 0);
            kit.api.vkCmdBeginQuery(cb, pool, 1, 0);
            kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
            kit.api.vkCmdEndQuery(cb, pool, 1);
            target.end(kit, cb);
        });
        std::uint64_t values[2][5] = {};
        rig.vk_ok(kit.api.vkGetQueryPoolResults(kit.device, pool, 0, 2, sizeof values, values, sizeof values[0], VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                  "vkGetQueryPoolResults");
        Image result(5, 2, PixelKind::Rgba32Uint);
        for (std::uint32_t q = 0; q < 2; ++q) {
            for (std::uint32_t c = 0; c < 5; ++c) {
                result.set_channel(c, q, 0, static_cast<double>(values[q][c]));
            }
        }
        out.add("counters", std::move(result));
        out.add("image", rig.color(target));
    });
    CaseSpec stats = spec_of("timestamps_and_statistics_leave_image");
    stats.feature("pipelineStatisticsQuery").same_as({"query.pipeline_statistics_counters"}, "the image must equal the plain draw, which is the geometry of the counters row; the queries must not alter it");
    Cases::add(
        std::move(stats),
        [](Rig& rig, CaptureSet& out) {
            Kit& kit = rig.kit;
            Target& target = rig.make_target(TargetDesc{});
            std::copy(kClear, kClear + 4, target.clear_color);
            const std::vector<Vertex2> v = {vertex(-0.8f, -0.7f, 0), vertex(0.7f, -0.6f, 1), vertex(0.0f, 0.8f, 2),
                                            vertex(-0.3f, -0.2f, 3), vertex(0.9f, 0.4f, 4),  vertex(0.1f, 0.9f, 5)};
            const Buffer vb = rig.buffer(v, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            const VkPipeline pipeline = rig.pipe(rig.pos2c(target));
            const VkQueryPool stats_pool = make_pool(rig, VK_QUERY_TYPE_PIPELINE_STATISTICS, 1,
                                                     VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT);
            const VkQueryPool time_pool = make_pool(rig, VK_QUERY_TYPE_TIMESTAMP, 2);
            rig.run([&](VkCommandBuffer cb) {
                kit.api.vkCmdResetQueryPool(cb, stats_pool, 0, 1);
                kit.api.vkCmdResetQueryPool(cb, time_pool, 0, 2);
                kit.api.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, time_pool, 0);
                target.begin(kit, cb, true);
                rig.bind_graphics(cb, pipeline);
                rig.bind_vertex(cb, vb);
                kit.api.vkCmdBeginQuery(cb, stats_pool, 0, 0);
                kit.api.vkCmdDraw(cb, 6, 1, 0, 0);
                kit.api.vkCmdEndQuery(cb, stats_pool, 0);
                target.end(kit, cb);
                kit.api.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, time_pool, 1);
            });
            std::uint64_t counters[2] = {};
            rig.vk_ok(kit.api.vkGetQueryPoolResults(kit.device, stats_pool, 0, 1, sizeof counters, counters, sizeof counters, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                      "vkGetQueryPoolResults");
            std::uint64_t times[2] = {};
            rig.vk_ok(kit.api.vkGetQueryPoolResults(kit.device, time_pool, 0, 2, sizeof times, times, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                      "vkGetQueryPoolResults");
            rig.must(counters[0] == 6, "the vertex shader invocation statistic is not the six vertices drawn");
            rig.must(times[1] >= times[0], "timestamps went backwards");
            out.add("", rig.color(target));
        });
}

const int registered = (register_queries(), 0);

}  // namespace

}  // namespace lsw::render::vk
