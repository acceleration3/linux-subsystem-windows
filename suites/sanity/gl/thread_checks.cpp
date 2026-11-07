#include "sanity/gl/thread_checks.hpp"

#include "sanity/gl/kit.hpp"
#include "support/gl_satellite.hpp"
#include "support/gl_scene.hpp"
#include "support/gl_target.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace lsw::suites::sanity::gl {

namespace {

class SharedProblems {
public:
    void add(const std::string& text) {
        std::lock_guard lock(mutex_);
        if (count_++ < 6) {
            problems_.add(text);
        }
    }

    [[nodiscard]] bool failed() {
        std::lock_guard lock(mutex_);
        return count_ > 0;
    }

    [[nodiscard]] Outcome outcome() {
        std::lock_guard lock(mutex_);
        return problems_.outcome();
    }

private:
    std::mutex mutex_;
    Problems problems_;
    int count_ = 0;
};

struct Handoff {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::pair<int, void*>> queue;
    int free_slots = 0;
    bool closed = false;
};

constexpr int kSide = 32;
constexpr std::size_t kTextureBytes = static_cast<std::size_t>(kSide) * kSide * 4;
constexpr std::size_t kBufferBytes = 1024;

}  // namespace

Outcome ThreadChecks::share_across_threads(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    GlSatellite consumer;
    if (!consumer.open(rig, true, 64, 64, error)) {
        return Outcome::fail(error);
    }
    SharedProblems problems;
    std::mutex mutex;
    std::condition_variable changed;
    unsigned texture = 0;
    void* fence = nullptr;
    int round = -1;
    int acknowledged = -1;
    bool done = false;
    constexpr int kRounds = 24;
    std::thread worker([&] {
        std::string worker_error;
        if (!consumer.bind(worker_error)) {
            problems.add(worker_error);
            std::lock_guard lock(mutex);
            done = true;
            changed.notify_all();
            return;
        }
        const GlApi& b = consumer.gl;
        for (int expected_round = 0; expected_round < kRounds; ++expected_round) {
            unsigned tex = 0;
            void* sync = nullptr;
            {
                std::unique_lock lock(mutex);
                if (!changed.wait_for(lock, std::chrono::seconds(20), [&] { return round == expected_round || done; }) || done) {
                    problems.add("the consumer thread timed out waiting for round " + std::to_string(expected_round));
                    return;
                }
                tex = texture;
                sync = fence;
            }
            const unsigned waited = b.glClientWaitSync(sync, glc::SYNC_FLUSH_COMMANDS_BIT, 5'000'000'000ull);
            if (waited != glc::ALREADY_SIGNALED && waited != glc::CONDITION_SATISFIED) {
                problems.add("round " + std::to_string(expected_round) + ": glClientWaitSync on the other context's fence returned " + GlTools::wait_name(waited));
            } else {
                std::vector<std::uint8_t> read(kTextureBytes, 0xCD);
                b.glBindTexture(glc::TEXTURE_2D, tex);
                b.glPixelStorei(glc::PACK_ALIGNMENT, 1);
                b.glGetTexImage(glc::TEXTURE_2D, 0, glc::RGBA, glc::UNSIGNED_BYTE, read.data());
                const unsigned failure = b.drain_errors();
                std::vector<std::uint8_t> expected(kTextureBytes);
                if (expected_round % 2 == 0) {
                    Pattern::fill(static_cast<std::uint64_t>(900 + expected_round), 0, expected.data(), expected.size());
                } else {
                    for (std::size_t i = 0; i < expected.size(); i += 4) {
                        expected[i] = static_cast<std::uint8_t>(expected_round * 8);
                        expected[i + 1] = 40;
                        expected[i + 2] = 200;
                        expected[i + 3] = 255;
                    }
                }
                if (failure != 0 || read != expected) {
                    problems.add("round " + std::to_string(expected_round) + ": the texture written in the first context reads " + GlApi::error_name(failure) +
                                 (read == expected ? "" : " with different data") + " in the second context after the fence");
                }
                b.glBindTexture(glc::TEXTURE_2D, 0);
            }
            std::lock_guard lock(mutex);
            acknowledged = expected_round;
            changed.notify_all();
        }
        consumer.unbind();
    });
    const GlApi& a = rig.gl;
    for (int r = 0; r < kRounds && !problems.failed(); ++r) {
        unsigned tex = 0;
        a.glGenTextures(1, &tex);
        a.glBindTexture(glc::TEXTURE_2D, tex);
        a.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
        a.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), kSide, kSide, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
        if (r % 2 == 0) {
            std::vector<std::uint8_t> data(kTextureBytes);
            Pattern::fill(static_cast<std::uint64_t>(900 + r), 0, data.data(), data.size());
            a.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
            a.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, kSide, kSide, glc::RGBA, glc::UNSIGNED_BYTE, data.data());
        } else {
            unsigned fbo = 0;
            a.glGenFramebuffers(1, &fbo);
            a.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
            a.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, tex, 0);
            a.glViewport(0, 0, kSide, kSide);
            a.glClearColor(static_cast<float>(r * 8) / 255.0f, 40.0f / 255.0f, 200.0f / 255.0f, 1.0f);
            a.glClear(glc::COLOR_BUFFER_BIT);
            a.glBindFramebuffer(glc::FRAMEBUFFER, 0);
            a.glDeleteFramebuffers(1, &fbo);
        }
        a.glBindTexture(glc::TEXTURE_2D, 0);
        void* sync = a.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
        a.glFlush();
        {
            std::lock_guard lock(mutex);
            texture = tex;
            fence = sync;
            round = r;
        }
        changed.notify_all();
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(20), [&] { return acknowledged == r || done; })) {
            problems.add("the producer timed out waiting for the consumer to finish round " + std::to_string(r));
            break;
        }
        lock.unlock();
        a.glDeleteSync(sync);
        a.glDeleteTextures(1, &tex);
    }
    {
        std::lock_guard lock(mutex);
        done = problems.failed() || acknowledged == kRounds - 1;
    }
    changed.notify_all();
    worker.join();
    a.glFinish();
    return problems.outcome();
}

Outcome ThreadChecks::make_current_rules(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    Problems problems;
    GlLibrary& library = GlLibrary::instance();
    const HDC dc = rig.dc();
    const HGLRC handle = rig.context.handle();
    rig.gl.glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    rig.gl.glClear(glc::COLOR_BUFFER_BIT);
    BOOL busy_result = TRUE;
    DWORD busy_error = 0;
    std::thread([&] {
        ::SetLastError(0);
        busy_result = library.make_current(dc, handle);
        busy_error = ::GetLastError();
        if (busy_result != FALSE) {
            library.make_current(nullptr, nullptr);
        }
    }).join();
    context.measure("busy.result", static_cast<long long>(busy_result));
    context.measure("busy.error", static_cast<long long>(busy_error));
    if (busy_result != FALSE) {
        problems.add("wglMakeCurrent on a second thread succeeded for a context that is current on the first thread");
        if (!rig.context.make_current(dc)) {
            return Outcome::fail("the first thread cannot reclaim its context after the second thread took it");
        }
    }
    GlContext::release();
    if (library.current_context() != nullptr) {
        problems.add("wglGetCurrentContext is not NULL after wglMakeCurrent(NULL,NULL)");
    }
    BOOL free_result = FALSE;
    std::thread([&] {
        free_result = library.make_current(dc, handle);
        if (free_result != FALSE) {
            library.make_current(nullptr, nullptr);
        }
    }).join();
    if (free_result == FALSE) {
        problems.add("after wglMakeCurrent(NULL,NULL) another thread cannot make the context current (error " + std::to_string(::GetLastError()) + ")");
    }
    if (!rig.context.make_current(dc)) {
        return Outcome::fail("the first thread cannot make its context current again");
    }
    float before[4] = {};
    rig.gl.glGetFloatv(0x0C22, before);
    GlContext::release();
    auto clear_color = reinterpret_cast<void(__stdcall*)(float, float, float, float)>(library.export_symbol("glClearColor"));
    auto clear = reinterpret_cast<void(__stdcall*)(unsigned)>(library.export_symbol("glClear"));
    auto finish = reinterpret_cast<void(__stdcall*)()>(library.export_symbol("glFinish"));
    auto get_error = reinterpret_cast<unsigned(__stdcall*)()>(library.export_symbol("glGetError"));
    if (clear_color == nullptr || clear == nullptr || finish == nullptr || get_error == nullptr) {
        problems.add("opengl32.dll does not export glClearColor, glClear, glFinish and glGetError");
    } else {
        clear_color(0.0f, 0.0f, 1.0f, 1.0f);
        clear(glc::COLOR_BUFFER_BIT);
        finish();
        (void)get_error();
    }
    if (!rig.context.make_current(dc)) {
        return Outcome::fail("the context cannot be made current after calls without a current context");
    }
    float after[4] = {};
    rig.gl.glGetFloatv(0x0C22, after);
    std::uint8_t pixel[4] = {};
    rig.gl.glPixelStorei(glc::PACK_ALIGNMENT, 1);
    rig.gl.glReadPixels(2, 2, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, pixel);
    if (after[0] != before[0] || after[1] != before[1] || after[2] != before[2]) {
        problems.add("GL calls made with no current context changed the clear colour of the released context from (" + std::to_string(before[0]) + "," +
                     std::to_string(before[1]) + "," + std::to_string(before[2]) + ") to (" + std::to_string(after[0]) + "," + std::to_string(after[1]) +
                     "," + std::to_string(after[2]) + ")");
    }
    if (pixel[0] != 255 || pixel[2] != 0) {
        problems.add("GL calls made with no current context changed the framebuffer: pixel reads (" + std::to_string(pixel[0]) + "," +
                     std::to_string(pixel[1]) + "," + std::to_string(pixel[2]) + ")");
    }
    return problems.outcome();
}

Outcome ThreadChecks::concurrent_contexts(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    constexpr int kThreads = 4;
    GlSatellite satellites[kThreads];
    for (GlSatellite& satellite : satellites) {
        if (!satellite.open(rig, false, 64, 64, error)) {
            return Outcome::fail(error);
        }
    }
    SharedProblems problems;
    std::atomic<long long> frames{0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            std::string thread_error;
            GlSatellite& satellite = satellites[t];
            if (!satellite.bind(thread_error)) {
                problems.add("thread " + std::to_string(t) + ": " + thread_error);
                return;
            }
            GlTarget target;
            GlScene frame(48, 48);
            if (!target.create(satellite.gl, 48, 48, glc::RGBA8, false, thread_error)) {
                problems.add("thread " + std::to_string(t) + ": " + thread_error);
                return;
            }
            int iteration = 0;
            while (std::chrono::steady_clock::now() < deadline && !problems.failed()) {
                frame.clear_rects();
                frame.clear_color(GlTools::rgba(t * 60 + 10, 255 - t * 60, 30 + iteration % 100, 255));
                frame.add({iteration % 16, 4 + t * 4, 16 + iteration % 16, 20 + t * 4, GlTools::rgba(255 - t * 60, t * 60, iteration % 256, 255)});
                if (!frame.draw(satellite.gl, thread_error)) {
                    problems.add("thread " + std::to_string(t) + ": " + thread_error);
                    break;
                }
                std::vector<std::uint8_t> image;
                if (!target.read_rgba(image, thread_error)) {
                    problems.add("thread " + std::to_string(t) + ": " + thread_error);
                    break;
                }
                const std::vector<std::uint8_t> expected = frame.expected();
                if (image != expected) {
                    std::size_t at = 0;
                    while (at < image.size() && image[at] == expected[at]) {
                        ++at;
                    }
                    problems.add("thread " + std::to_string(t) + " iteration " + std::to_string(iteration) + ": the image differs from the thread's own scene at byte " +
                                 std::to_string(at) + " (read " + std::to_string(image[at]) + ", expected " + std::to_string(expected[at]) + ")");
                    break;
                }
                ++iteration;
                frames.fetch_add(1);
            }
            satellite.gl.glFinish();
            frame.release();
            target.destroy();
            satellite.unbind();
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    context.measure("frames", frames.load());
    if (frames.load() == 0) {
        return Outcome::fail("no thread rendered a frame");
    }
    return problems.outcome();
}

Outcome ThreadChecks::producer_consumer(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    constexpr int kSlots = 4;
    constexpr int kSequences = 300;
    GlSatellite producer;
    GlSatellite consumer;
    if (!producer.open(rig, true, 64, 64, error) || !consumer.open(rig, true, 64, 64, error)) {
        return Outcome::fail(error);
    }
    unsigned textures[kSlots] = {};
    unsigned buffers[kSlots] = {};
    GlContext::release();
    if (!producer.bind(error)) {
        return Outcome::fail(error);
    }
    {
        const GlApi& p = producer.gl;
        for (int s = 0; s < kSlots; ++s) {
            p.glGenTextures(1, &textures[s]);
            p.glBindTexture(glc::TEXTURE_2D, textures[s]);
            p.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
            p.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), kSide, kSide, 0, glc::RGBA, glc::UNSIGNED_BYTE, nullptr);
            p.glGenBuffers(1, &buffers[s]);
            p.glBindBuffer(glc::COPY_WRITE_BUFFER, buffers[s]);
            p.glBufferData(glc::COPY_WRITE_BUFFER, static_cast<std::ptrdiff_t>(kBufferBytes), nullptr, glc::DYNAMIC_DRAW);
        }
        p.glBindTexture(glc::TEXTURE_2D, 0);
        p.glBindBuffer(glc::COPY_WRITE_BUFFER, 0);
        p.glFinish();
        if (p.drain_errors() != 0) {
            return Outcome::fail("creating the ring of textures and buffers gave a GL error");
        }
    }
    producer.unbind();
    SharedProblems problems;
    Handoff handoff;
    handoff.free_slots = kSlots;
    std::vector<int> sequence_of_slot(kSlots, -1);
    std::thread producing([&] {
        std::string thread_error;
        if (!producer.bind(thread_error)) {
            problems.add(thread_error);
            std::lock_guard lock(handoff.mutex);
            handoff.closed = true;
            handoff.changed.notify_all();
            return;
        }
        const GlApi& p = producer.gl;
        for (int seq = 0; seq < kSequences && !problems.failed(); ++seq) {
            const int slot = seq % kSlots;
            {
                std::unique_lock lock(handoff.mutex);
                if (!handoff.changed.wait_for(lock, std::chrono::seconds(20), [&] { return handoff.free_slots > 0 || handoff.closed; }) || handoff.closed) {
                    problems.add("the producer found no free slot at sequence " + std::to_string(seq));
                    break;
                }
                --handoff.free_slots;
            }
            const std::vector<std::uint8_t> texels = Kit::bytes(static_cast<std::uint64_t>(5000 + seq), kTextureBytes);
            const std::vector<std::uint8_t> bytes = Kit::bytes(static_cast<std::uint64_t>(9000 + seq), kBufferBytes);
            p.glBindTexture(glc::TEXTURE_2D, textures[slot]);
            p.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
            p.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, kSide, kSide, glc::RGBA, glc::UNSIGNED_BYTE, texels.data());
            p.glBindBuffer(glc::COPY_WRITE_BUFFER, buffers[slot]);
            p.glBufferSubData(glc::COPY_WRITE_BUFFER, 0, static_cast<std::ptrdiff_t>(kBufferBytes), bytes.data());
            void* fence = p.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
            p.glFlush();
            {
                std::lock_guard lock(handoff.mutex);
                sequence_of_slot[static_cast<std::size_t>(slot)] = seq;
                handoff.queue.emplace_back(slot, fence);
            }
            handoff.changed.notify_all();
        }
        p.glFinish();
        producer.unbind();
    });
    std::thread consuming([&] {
        std::string thread_error;
        if (!consumer.bind(thread_error)) {
            problems.add(thread_error);
            std::lock_guard lock(handoff.mutex);
            handoff.closed = true;
            handoff.changed.notify_all();
            return;
        }
        const GlApi& c = consumer.gl;
        for (int seq = 0; seq < kSequences && !problems.failed(); ++seq) {
            int slot = 0;
            void* fence = nullptr;
            int seq_of = -1;
            {
                std::unique_lock lock(handoff.mutex);
                if (!handoff.changed.wait_for(lock, std::chrono::seconds(20), [&] { return !handoff.queue.empty() || handoff.closed; }) || handoff.queue.empty()) {
                    problems.add("the consumer received nothing for sequence " + std::to_string(seq));
                    break;
                }
                slot = handoff.queue.front().first;
                fence = handoff.queue.front().second;
                handoff.queue.pop_front();
                seq_of = sequence_of_slot[static_cast<std::size_t>(slot)];
            }
            const unsigned waited = c.glClientWaitSync(fence, glc::SYNC_FLUSH_COMMANDS_BIT, 5'000'000'000ull);
            if (waited != glc::ALREADY_SIGNALED && waited != glc::CONDITION_SATISFIED) {
                problems.add("sequence " + std::to_string(seq) + ": glClientWaitSync returned " + GlTools::wait_name(waited));
                break;
            }
            std::vector<std::uint8_t> texels(kTextureBytes, 0xCD);
            std::vector<std::uint8_t> bytes(kBufferBytes, 0xCD);
            c.glBindTexture(glc::TEXTURE_2D, textures[slot]);
            c.glPixelStorei(glc::PACK_ALIGNMENT, 1);
            c.glGetTexImage(glc::TEXTURE_2D, 0, glc::RGBA, glc::UNSIGNED_BYTE, texels.data());
            c.glBindBuffer(glc::COPY_READ_BUFFER, buffers[slot]);
            c.glGetBufferSubData(glc::COPY_READ_BUFFER, 0, static_cast<std::ptrdiff_t>(kBufferBytes), bytes.data());
            const unsigned failure = c.drain_errors();
            if (failure != 0) {
                problems.add("sequence " + std::to_string(seq) + ": reading the slot gave " + GlApi::error_name(failure));
                break;
            }
            if (texels != Kit::bytes(static_cast<std::uint64_t>(5000 + seq_of), kTextureBytes)) {
                problems.add("sequence " + std::to_string(seq) + " slot " + std::to_string(slot) + ": the texture is stale or torn after the fence signalled");
                break;
            }
            if (bytes != Kit::bytes(static_cast<std::uint64_t>(9000 + seq_of), kBufferBytes)) {
                problems.add("sequence " + std::to_string(seq) + " slot " + std::to_string(slot) + ": the buffer is stale or torn after the fence signalled");
                break;
            }
            c.glDeleteSync(fence);
            {
                std::lock_guard lock(handoff.mutex);
                ++handoff.free_slots;
            }
            handoff.changed.notify_all();
        }
        {
            std::lock_guard lock(handoff.mutex);
            handoff.closed = true;
        }
        handoff.changed.notify_all();
        c.glFinish();
        consumer.unbind();
    });
    producing.join();
    consuming.join();
    std::string restore_error;
    if (!producer.bind(restore_error)) {
        return Outcome::fail(restore_error);
    }
    producer.gl.glDeleteTextures(kSlots, textures);
    producer.gl.glDeleteBuffers(kSlots, buffers);
    producer.gl.glFinish();
    producer.unbind();
    context.measure("sequences", kSequences);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
