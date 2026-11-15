#include "sanity/gl/sync_checks.hpp"

#include "sanity/gl/kit.hpp"
#include "support/gl_program.hpp"
#include "support/gl_scene.hpp"
#include "support/gl_target.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace lsw::suites::sanity::gl {

namespace {

using Clock = std::chrono::steady_clock;

long long micros(Clock::time_point from) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - from).count();
}

class Workload {
public:
    [[nodiscard]] bool open(const GlApi& api, int side, std::string& error) {
        api_ = &api;
        side_ = side;
        const std::string vertex = "#version 330 core\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
        const std::string fragment = "#version 330 core\nout vec4 color;\nvoid main() { color = vec4(1.0 / 255.0); }\n";
        if (!program_.build(api, vertex, fragment, error) || !target_.create(api, side, side, glc::RGBA8, false, error)) {
            return false;
        }
        api.glGenVertexArrays(1, &vao_);
        return true;
    }

    void run(int layers) {
        target_.bind();
        api_->glViewport(0, 0, side_, side_);
        api_->glDisable(glc::DEPTH_TEST);
        api_->glDisable(glc::SCISSOR_TEST);
        api_->glColorMask(1, 1, 1, 1);
        api_->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        api_->glClear(glc::COLOR_BUFFER_BIT);
        api_->glEnable(glc::BLEND);
        api_->glBlendFunc(glc::ONE, glc::ONE);
        api_->glBindVertexArray(vao_);
        program_.use();
        int remaining = layers;
        while (remaining > 0) {
            const int batch = std::min(remaining, 255);
            api_->glDrawArraysInstanced(glc::TRIANGLES, 0, 3, batch);
            remaining -= batch;
        }
        api_->glUseProgram(0);
        api_->glBindVertexArray(0);
        api_->glDisable(glc::BLEND);
    }

    [[nodiscard]] int expected(int layers) const { return std::min(layers, 255); }

    [[nodiscard]] int pixel() {
        std::uint8_t value[4] = {};
        api_->glBindFramebuffer(glc::READ_FRAMEBUFFER, target_.fbo());
        api_->glPixelStorei(glc::PACK_ALIGNMENT, 1);
        api_->glReadPixels(side_ / 2, side_ / 2, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, value);
        return value[0] == value[1] && value[1] == value[2] ? value[0] : -1;
    }

    [[nodiscard]] int side() const { return side_; }

    void close() {
        if (api_ != nullptr) {
            api_->glDeleteVertexArrays(1, &vao_);
        }
        target_.destroy();
        program_.destroy();
        api_ = nullptr;
    }

private:
    const GlApi* api_ = nullptr;
    GlProgram program_;
    GlTarget target_;
    unsigned vao_ = 0;
    int side_ = 0;
};

}  // namespace

Outcome SyncChecks::fence_ordering(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 2, "GL_ARB_sync") || api.glFenceSync == nullptr) {
        return Kit::not_advertised("OpenGL 3.2 or GL_ARB_sync");
    }
    Workload work;
    if (!work.open(api, 512, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (const int layers : {10, 50, 120, 200, 250}) {
        work.run(layers);
        void* fence = api.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (fence == nullptr || api.glIsSync(fence) == 0) {
            problems.add("glFenceSync returned " + std::string(fence == nullptr ? "NULL" : "a handle that glIsSync rejects"));
            break;
        }
        const auto begin = Clock::now();
        const unsigned waited = api.glClientWaitSync(fence, glc::SYNC_FLUSH_COMMANDS_BIT, 10'000'000'000ull);
        if (waited != glc::ALREADY_SIGNALED && waited != glc::CONDITION_SATISFIED) {
            problems.add(std::to_string(layers) + " layers: glClientWaitSync(FLUSH_COMMANDS, 10 s) returned " + GlTools::wait_name(waited) + " after " + std::to_string(micros(begin) / 1000) + " ms");
            api.glDeleteSync(fence);
            break;
        }
        int status = 0;
        api.glGetSynciv(fence, glc::SYNC_STATUS, 1, nullptr, &status);
        if (static_cast<unsigned>(status) != glc::SIGNALED) {
            problems.add(std::to_string(layers) + " layers: SYNC_STATUS is 0x" + Text::hex(static_cast<unsigned>(status), 4) + " after the wait succeeded");
        }
        const int value = work.pixel();
        if (value != work.expected(layers)) {
            problems.add(std::to_string(layers) + " layers: the result read after the fence signalled is " + std::to_string(value) + ", expected " + std::to_string(work.expected(layers)));
        }
        api.glDeleteSync(fence);
        if (api.glIsSync(fence) != 0) {
            problems.add("glIsSync still accepts a deleted fence");
        }
    }
    GlTools::expect_clean(problems, api, "the fence rounds");
    work.close();
    return problems.outcome();
}

Outcome SyncChecks::timeout_semantics(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 2, "GL_ARB_sync") || api.glFenceSync == nullptr) {
        return Kit::not_advertised("OpenGL 3.2 or GL_ARB_sync");
    }
    Workload work;
    if (!work.open(api, 512, error)) {
        return Outcome::fail(error);
    }
    int layers = 64;
    long long run_us = 0;
    for (;;) {
        const auto begin = Clock::now();
        work.run(layers);
        api.glFinish();
        run_us = micros(begin);
        if (run_us >= 40'000 || layers >= 1024 || run_us >= 150'000) {
            break;
        }
        layers *= 2;
    }
    context.measure("layers", layers);
    context.measure("workload_us", run_us);
    Problems problems;
    int expired = 0;
    int unsignalled_wait = 0;
    for (int attempt = 0; attempt < 8 && problems.empty(); ++attempt) {
        work.run(layers);
        void* fence = api.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
        auto begin = Clock::now();
        const unsigned immediate = api.glClientWaitSync(fence, 0, 0);
        const long long immediate_us = micros(begin);
        if (immediate == glc::TIMEOUT_EXPIRED) {
            ++expired;
            if (immediate_us > 50'000) {
                problems.add("glClientWaitSync with timeout 0 took " + std::to_string(immediate_us / 1000) + " ms to report TIMEOUT_EXPIRED");
            }
            api.glFlush();
            begin = Clock::now();
            const unsigned timed = api.glClientWaitSync(fence, 0, 20'000'000ull);
            const long long timed_us = micros(begin);
            if (timed == glc::TIMEOUT_EXPIRED) {
                ++unsignalled_wait;
                if (timed_us < 16'000) {
                    problems.add("a 20 ms glClientWaitSync reported TIMEOUT_EXPIRED after only " + std::to_string(timed_us) + " us");
                }
                if (timed_us > 1'000'000) {
                    problems.add("a 20 ms glClientWaitSync took " + std::to_string(timed_us / 1000) + " ms");
                }
            } else if (timed != glc::ALREADY_SIGNALED && timed != glc::CONDITION_SATISFIED) {
                problems.add("a 20 ms glClientWaitSync returned " + GlTools::wait_name(timed));
            }
        } else if (immediate != glc::ALREADY_SIGNALED && immediate != glc::CONDITION_SATISFIED) {
            problems.add("glClientWaitSync with timeout 0 returned " + GlTools::wait_name(immediate));
        }
        api.glFinish();
        api.glDeleteSync(fence);
    }
    int blocked = 0;
    int measured = 0;
    for (int attempt = 0; attempt < 5 && problems.empty(); ++attempt) {
        work.run(layers);
        void* fence = api.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
        const auto begin = Clock::now();
        api.glWaitSync(fence, 0, glc::TIMEOUT_IGNORED);
        const long long call_us = micros(begin);
        api.glFinish();
        const long long total_us = micros(begin);
        if (total_us >= 60'000) {
            ++measured;
            if (call_us > std::max<long long>(30'000, total_us / 2)) {
                ++blocked;
            }
        }
        api.glDeleteSync(fence);
    }
    context.measure("timeout_zero_expired_of_8", expired);
    context.measure("timed_wait_expired_of_8", unsignalled_wait);
    context.measure("wait_sync_blocking_of_measurable", blocked);
    if (measured >= 2 && blocked * 2 > measured) {
        problems.add("glWaitSync blocked the CPU for most of the GPU work in " + std::to_string(blocked) + " of " + std::to_string(measured) + " measurable rounds");
    }
    if (expired == 0) {
        context.note("every fence was already signalled at the first timeout-0 wait, so the unsignalled path was not exercised (workload " + std::to_string(run_us) + " us)");
    }
    GlTools::expect_clean(problems, api, "the sync rounds");
    work.close();
    return problems.outcome();
}

Outcome SyncChecks::finish_flush(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 2, "GL_ARB_sync") || api.glFenceSync == nullptr) {
        return Kit::not_advertised("OpenGL 3.2 or GL_ARB_sync");
    }
    Workload work;
    if (!work.open(api, 512, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (const int layers : {40, 200}) {
        work.run(layers);
        void* fence = api.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
        api.glFinish();
        const unsigned after = api.glClientWaitSync(fence, 0, 0);
        if (after != glc::ALREADY_SIGNALED) {
            problems.add(std::to_string(layers) + " layers: a fence created before glFinish is " + GlTools::wait_name(after) + " when glFinish returned");
        }
        const int value = work.pixel();
        if (value != work.expected(layers)) {
            problems.add(std::to_string(layers) + " layers: the result after glFinish is " + std::to_string(value) + ", expected " + std::to_string(work.expected(layers)));
        }
        api.glDeleteSync(fence);
    }
    int polled = 0;
    for (const int layers : {40, 200}) {
        work.run(layers);
        void* fence = api.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
        const auto begin = Clock::now();
        api.glFlush();
        const long long flush_us = micros(begin);
        if (flush_us > 500'000) {
            problems.add("glFlush took " + std::to_string(flush_us / 1000) + " ms");
        }
        bool signalled = false;
        while (micros(begin) < 10'000'000) {
            int status = 0;
            api.glGetSynciv(fence, glc::SYNC_STATUS, 1, nullptr, &status);
            if (static_cast<unsigned>(status) == glc::SIGNALED) {
                signalled = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++polled;
        }
        if (!signalled) {
            problems.add(std::to_string(layers) + " layers: the fence did not signal within 10 s after glFlush alone");
        } else if (work.pixel() != work.expected(layers)) {
            problems.add(std::to_string(layers) + " layers: the result after the flushed fence signalled is not final");
        }
        api.glDeleteSync(fence);
    }
    context.measure("flush_polls", polled);
    GlTools::expect_clean(problems, api, "finish and flush");
    work.close();
    return problems.outcome();
}

Outcome SyncChecks::timer_samples_primitives(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.at_least(3, 3) || api.glGetQueryObjectui64v == nullptr) {
        return Kit::not_advertised("OpenGL 3.3 queries");
    }
    Problems problems;
    constexpr int kSide = 64;
    GlTarget target;
    if (!target.create(api, kSide, kSide, glc::RGBA8, true, error)) {
        return Outcome::fail(error);
    }
    GlProgram program;
    const std::string vertex = "#version 330 core\nlayout(location = 0) in vec3 position;\nvoid main() { gl_Position = vec4(position, 1.0); }\n";
    const std::string fragment = "#version 330 core\nout vec4 color;\nvoid main() { color = vec4(1.0); }\n";
    if (!program.build(api, vertex, fragment, error)) {
        return Outcome::fail(error);
    }
    unsigned vao = 0;
    unsigned vbo = 0;
    api.glGenVertexArrays(1, &vao);
    api.glBindVertexArray(vao);
    api.glGenBuffers(1, &vbo);
    api.glBindBuffer(glc::ARRAY_BUFFER, vbo);
    api.glEnableVertexAttribArray(0);
    api.glVertexAttribPointer(0, 3, glc::FLOAT, 0, 12, nullptr);
    auto rect = [&](int x0, int y0, int x1, int y1, float z) {
        const float fx0 = static_cast<float>(x0) / 32.0f - 1.0f;
        const float fx1 = static_cast<float>(x1) / 32.0f - 1.0f;
        const float fy0 = static_cast<float>(y0) / 32.0f - 1.0f;
        const float fy1 = static_cast<float>(y1) / 32.0f - 1.0f;
        const float data[18] = {fx0, fy0, z, fx1, fy0, z, fx1, fy1, z, fx0, fy0, z, fx1, fy1, z, fx0, fy1, z};
        api.glBufferData(glc::ARRAY_BUFFER, sizeof(data), data, glc::STREAM_READ);
        api.glDrawArrays(glc::TRIANGLES, 0, 6);
    };
    auto result = [&](unsigned query) {
        unsigned value = 0;
        api.glGetQueryObjectuiv(query, glc::QUERY_RESULT, &value);
        return value;
    };
    target.bind();
    api.glViewport(0, 0, kSide, kSide);
    api.glClearDepth(1.0);
    api.glClearColor(0, 0, 0, 1);
    api.glClear(glc::COLOR_BUFFER_BIT | glc::DEPTH_BUFFER_BIT);
    api.glEnable(glc::DEPTH_TEST);
    api.glDepthFunc(glc::LESS);
    program.use();
    rect(0, 0, 32, 16, -1.0f);
    unsigned queries[4] = {};
    api.glGenQueries(4, queries);
    api.glBeginQuery(glc::SAMPLES_PASSED, queries[0]);
    rect(16, 0, 48, 16, 0.0f);
    api.glEndQuery(glc::SAMPLES_PASSED);
    api.glBeginQuery(glc::ANY_SAMPLES_PASSED, queries[1]);
    rect(16, 0, 48, 16, -0.5f);
    api.glEndQuery(glc::ANY_SAMPLES_PASSED);
    api.glBeginQuery(glc::SAMPLES_PASSED, queries[2]);
    rect(4, 4, 28, 12, 0.5f);
    api.glEndQuery(glc::SAMPLES_PASSED);
    api.glBeginQuery(glc::ANY_SAMPLES_PASSED, queries[3]);
    rect(4, 4, 28, 12, 0.5f);
    api.glEndQuery(glc::ANY_SAMPLES_PASSED);
    const unsigned visible = result(queries[0]);
    if (visible != 16u * 16u) {
        problems.add("SAMPLES_PASSED for a 32x16 rect with its left half behind a nearer rect is " + std::to_string(visible) + ", the covered visible area is 256");
    }
    if (result(queries[1]) == 0) {
        problems.add("ANY_SAMPLES_PASSED is 0 for a partly visible rect");
    }
    if (result(queries[2]) != 0) {
        problems.add("SAMPLES_PASSED for a fully hidden rect is " + std::to_string(result(queries[2])));
    }
    if (result(queries[3]) != 0) {
        problems.add("ANY_SAMPLES_PASSED for a fully hidden rect is non-zero");
    }
    api.glDeleteQueries(4, queries);
    api.glDisable(glc::DEPTH_TEST);
    api.glUseProgram(0);
    GlScene scene(kSide, kSide);
    for (int i = 0; i < 3; ++i) {
        scene.add({i * 10, 0, i * 10 + 8, 8, GlTools::rgba(255, 255, 255, 255)});
    }
    GlProgram capture;
    const char* varyings[] = {"gl_Position"};
    if (!capture.build_stages(api, GlScene::vertex_source(), "", varyings, 1, error)) {
        problems.add(error);
    } else {
        const std::vector<GlScene::Vertex> vertices = scene.vertices();
        unsigned source = 0;
        unsigned sink = 0;
        api.glGenBuffers(1, &source);
        api.glBindBuffer(glc::ARRAY_BUFFER, source);
        api.glBufferData(glc::ARRAY_BUFFER, static_cast<std::ptrdiff_t>(vertices.size() * sizeof(GlScene::Vertex)), vertices.data(), glc::STATIC_DRAW);
        api.glEnableVertexAttribArray(0);
        api.glVertexAttribPointer(0, 2, glc::FLOAT, 0, sizeof(GlScene::Vertex), nullptr);
        api.glEnableVertexAttribArray(1);
        api.glVertexAttribPointer(1, 4, glc::UNSIGNED_BYTE, 1, sizeof(GlScene::Vertex), reinterpret_cast<const void*>(8));
        api.glGenBuffers(1, &sink);
        api.glBindBuffer(glc::TRANSFORM_FEEDBACK_BUFFER, sink);
        api.glBufferData(glc::TRANSFORM_FEEDBACK_BUFFER, static_cast<std::ptrdiff_t>(vertices.size() * 16), nullptr, glc::STATIC_DRAW);
        api.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, sink);
        unsigned counters[2] = {};
        api.glGenQueries(2, counters);
        capture.use();
        api.glEnable(glc::RASTERIZER_DISCARD);
        api.glBeginQuery(glc::PRIMITIVES_GENERATED, counters[0]);
        api.glBeginQuery(glc::TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, counters[1]);
        api.glBeginTransformFeedback(glc::TRIANGLES);
        api.glDrawArrays(glc::TRIANGLES, 0, 18);
        api.glEndTransformFeedback();
        api.glEndQuery(glc::TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
        api.glEndQuery(glc::PRIMITIVES_GENERATED);
        api.glDisable(glc::RASTERIZER_DISCARD);
        if (result(counters[0]) != 6) {
            problems.add("PRIMITIVES_GENERATED for 18 vertices of triangles is " + std::to_string(result(counters[0])) + ", expected 6");
        }
        if (result(counters[1]) != 6) {
            problems.add("TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN for 6 triangles is " + std::to_string(result(counters[1])) + ", expected 6");
        }
        api.glDeleteQueries(2, counters);
        api.glDeleteBuffers(1, &source);
        api.glDeleteBuffers(1, &sink);
        api.glUseProgram(0);
    }
    api.glBindVertexArray(0);
    api.glDeleteVertexArrays(1, &vao);
    api.glDeleteBuffers(1, &vbo);
    target.destroy();
    program.destroy();
    capture.destroy();
    Workload work;
    if (!work.open(api, 512, error)) {
        return Outcome::fail(error);
    }
    unsigned long long times[3] = {};
    const int loads[3] = {8, 64, 512};
    unsigned timers[3] = {};
    api.glGenQueries(3, timers);
    for (int i = 0; i < 3; ++i) {
        api.glBeginQuery(glc::TIME_ELAPSED, timers[i]);
        work.run(loads[i]);
        api.glEndQuery(glc::TIME_ELAPSED);
        api.glFinish();
        api.glGetQueryObjectui64v(timers[i], glc::QUERY_RESULT, &times[i]);
        context.measure("time_elapsed_ns." + std::to_string(loads[i]) + "_layers", static_cast<long long>(times[i]));
    }
    api.glDeleteQueries(3, timers);
    if (times[2] < times[0]) {
        problems.add("TIME_ELAPSED for 512 layers (" + std::to_string(times[2]) + " ns) is below TIME_ELAPSED for 8 layers (" + std::to_string(times[0]) + " ns)");
    }
    if (times[2] > 20'000'000'000ull) {
        problems.add("TIME_ELAPSED of " + std::to_string(times[2]) + " ns is not plausible for the workload");
    }
    GlTools::expect_clean(problems, api, "the query workloads");
    work.close();
    return problems.outcome();
}

Outcome SyncChecks::availability_rules(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (!api.at_least(3, 3)) {
        return Kit::not_advertised("OpenGL 3.3 queries");
    }
    Workload work;
    if (!work.open(api, 256, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    unsigned q[3] = {};
    api.glGenQueries(3, q);
    api.glBeginQuery(glc::SAMPLES_PASSED, q[0]);
    work.run(100);
    api.glEndQuery(glc::SAMPLES_PASSED);
    GlTools::expect_clean(problems, api, "a normal query");
    bool seen_true = false;
    int regressions = 0;
    const auto begin = Clock::now();
    int polls_after = 0;
    while (micros(begin) < 5'000'000 && polls_after < 6) {
        int available = 0;
        api.glGetQueryObjectiv(q[0], glc::QUERY_RESULT_AVAILABLE, &available);
        if (available != 0) {
            seen_true = true;
            ++polls_after;
        } else if (seen_true) {
            ++regressions;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!seen_true) {
        problems.add("QUERY_RESULT_AVAILABLE never became true within 5 s");
    }
    if (regressions != 0) {
        problems.add("QUERY_RESULT_AVAILABLE went from true back to false " + std::to_string(regressions) + " times");
    }
    unsigned value = 0;
    api.glGetQueryObjectuiv(q[0], glc::QUERY_RESULT, &value);
    if (value == 0) {
        problems.add("SAMPLES_PASSED of a 100-layer full-screen pass is 0");
    }
    api.glBeginQuery(glc::SAMPLES_PASSED, q[1]);
    GlTools::expect_clean(problems, api, "BeginQuery");
    api.glBeginQuery(glc::SAMPLES_PASSED, q[2]);
    GlTools::expect_error(problems, api, "BeginQuery on a target that already has an active query", glc::INVALID_OPERATION);
    api.glBeginQuery(glc::ANY_SAMPLES_PASSED, q[1]);
    GlTools::expect_error(problems, api, "BeginQuery with the id of another target's active query", glc::INVALID_OPERATION);
    unsigned ignored = 0;
    api.glGetQueryObjectuiv(q[1], glc::QUERY_RESULT_AVAILABLE, &ignored);
    GlTools::expect_error(problems, api, "reading QUERY_RESULT_AVAILABLE of an active query", glc::INVALID_OPERATION);
    api.glGetQueryObjectuiv(q[1], glc::QUERY_RESULT, &ignored);
    GlTools::expect_error(problems, api, "reading QUERY_RESULT of an active query", glc::INVALID_OPERATION);
    api.glEndQuery(glc::SAMPLES_PASSED);
    GlTools::expect_clean(problems, api, "EndQuery");
    api.glEndQuery(glc::SAMPLES_PASSED);
    GlTools::expect_error(problems, api, "EndQuery with no active query", glc::INVALID_OPERATION);
    api.glBeginQuery(glc::TIME_ELAPSED, q[1]);
    GlTools::expect_error(problems, api, "BeginQuery on an id already used with another target", glc::INVALID_OPERATION);
    api.glBeginQuery(glc::SAMPLES_PASSED, 0);
    GlTools::expect_error(problems, api, "BeginQuery with id 0", glc::INVALID_OPERATION);
    api.glBeginQuery(0x1234, q[2]);
    GlTools::expect_error(problems, api, "BeginQuery with an invalid target", glc::INVALID_ENUM);
    api.glDeleteQueries(3, q);
    work.close();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
