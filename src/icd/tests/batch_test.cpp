// Phase C: N threads record concurrently, one message per buffer.
//
// The claim batching makes is narrow and checkable: the same commands
// reach the server, in the same per-buffer order, in far fewer messages,
// with no shared state touched while recording. This measures all four.
//
// It deliberately does not need a GPU, a window or a socket. Recording
// is pure accumulation and unpacking is pure parsing, so the interesting
// failures — a lost command, a reordered one, a batch that unpacks
// differently than it was packed — are all reachable here, in
// milliseconds, rather than only as a wrong-looking cube.

#include "lsw/icd/command_recorder.hpp"
#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/protocol_constants.hpp"
#include "lsw/protocol/transport.hpp"
#include "lsw/render/handlers/batch_handler.hpp"
#include "lsw/render/session.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

using lsw::icd::CommandRecorder;
using lsw::protocol::Buffer;
using lsw::protocol::TransportResult;
using lsw::render::handlers::BatchHandler;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

// What each recorded command carries, so a replayed stream can be
// checked for completeness and order.
struct Tag {
    std::uint32_t thread_id;
    std::uint32_t sequence;
};

constexpr std::uint32_t kFakeCmdId = 0x0001'00FF;
constexpr unsigned kThreads = 8;
constexpr unsigned kPerBuffer = 250;

// Stands in for the render server: unpacks batches through the real
// BatchHandler and records what came out.
class Harness final : public lsw::protocol::FrameDispatcher {
public:
    Harness() : batch_(std::make_unique<BatchHandler>(*this)) {}

    [[nodiscard]] bool dispatch(std::uint32_t cmd_id, std::span<const std::uint8_t> payload,
                                Buffer& reply) override {
        if (cmd_id == lsw::protocol::kCmdBatch) {
            return batch_->handle(session_, cmd_id, payload, reply)
                   == lsw::render::HandleResult::Handled;
        }
        if (cmd_id != kFakeCmdId || payload.size() != sizeof(Tag)) {
            return false;
        }
        Tag tag{};
        std::memcpy(&tag, payload.data(), sizeof(tag));
        const std::lock_guard lk{mutex_};
        per_thread_[tag.thread_id].push_back(tag.sequence);
        ++replayed_;
        return true;
    }

    [[nodiscard]] std::size_t replayed() const { return replayed_; }

    // True when every thread's commands arrived complete and in the
    // order that thread recorded them.
    [[nodiscard]] bool order_preserved(unsigned threads, unsigned per_buffer) const {
        if (per_thread_.size() != threads) {
            return false;
        }
        for (const auto& [id, seqs] : per_thread_) {
            if (seqs.size() != per_buffer) {
                return false;
            }
            for (unsigned i = 0; i < per_buffer; ++i) {
                if (seqs[i] != i) {
                    return false;
                }
            }
        }
        return true;
    }

private:
    lsw::render::Session session_;
    std::unique_ptr<BatchHandler> batch_;
    std::mutex mutex_;
    std::map<std::uint32_t, std::vector<std::uint32_t>> per_thread_;
    std::size_t replayed_ = 0;
};

// A transport that counts messages and hands them straight to the
// harness. The lock stands in for the wire being one stream — it is
// taken once per MESSAGE, which is exactly the contention batching is
// meant to reduce.
class CountingTransport final : public lsw::protocol::Transport {
public:
    explicit CountingTransport(Harness& h) : harness_(h) {}

    [[nodiscard]] TransportResult send_async(std::uint32_t cmd_id,
                                             std::span<const std::uint8_t> payload) override {
        messages_.fetch_add(1);
        const std::lock_guard lk{wire_};
        Buffer reply;
        return harness_.dispatch(cmd_id, payload, reply) ? TransportResult::Ok
                                                         : TransportResult::InvalidFrame;
    }

    [[nodiscard]] TransportResult send_sync(std::uint32_t, std::span<const std::uint8_t>,
                                            Buffer*) override {
        return TransportResult::InvalidFrame;
    }

    [[nodiscard]] std::uint64_t messages() const { return messages_.load(); }

private:
    Harness& harness_;
    std::mutex wire_;
    std::atomic<std::uint64_t> messages_{0};
};

void record_one_buffer(lsw::protocol::Transport& target, std::uint32_t thread_id,
                       unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        Tag tag{thread_id, i};
        const std::span<const std::uint8_t> payload{reinterpret_cast<const std::uint8_t*>(&tag),
                                                    sizeof(tag)};
        static_cast<void>(target.send_async(kFakeCmdId, payload));
    }
}

// ---- the two runs being compared ----------------------------------------

// Before: every vkCmd* is its own message, as it was.
[[nodiscard]] std::uint64_t run_unbatched(Harness& harness) {
    CountingTransport transport{harness};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (unsigned t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t] { record_one_buffer(transport, t, kPerBuffer); });
    }
    for (auto& w : workers) {
        w.join();
    }
    return transport.messages();
}

// After: each thread owns a command buffer, records into its own
// recorder — no shared state at all — and ships one message at End.
[[nodiscard]] std::uint64_t run_batched(Harness& harness) {
    CountingTransport transport{harness};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (unsigned t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t] {
            CommandRecorder recorder;  // per-buffer, per-thread, unshared
            record_one_buffer(recorder, t, kPerBuffer);
            static_cast<void>(recorder.flush(transport));
        });
    }
    for (auto& w : workers) {
        w.join();
    }
    return transport.messages();
}

void test_malformed_batches() {
    Harness harness;
    Buffer reply;
    BatchHandler handler{harness};
    lsw::render::Session session;

    // A batch claiming more entries than it carries.
    {
        Buffer b;
        b.write_pod<std::uint32_t>(4);
        b.write_pod<std::uint32_t>(kFakeCmdId);
        b.write_pod<std::uint32_t>(0);
        check(handler.handle(session, lsw::protocol::kCmdBatch, b.bytes(), reply)
                  == lsw::render::HandleResult::ProtocolError,
              "a truncated batch is a protocol error, not a partial replay");
    }
    // An entry whose length runs past the end of the payload.
    {
        Buffer b;
        b.write_pod<std::uint32_t>(1);
        b.write_pod<std::uint32_t>(kFakeCmdId);
        b.write_pod<std::uint32_t>(9999);
        check(handler.handle(session, lsw::protocol::kCmdBatch, b.bytes(), reply)
                  == lsw::render::HandleResult::ProtocolError,
              "an over-long entry is refused rather than read past the buffer");
    }
    // A batch inside a batch, which would recurse.
    {
        Buffer b;
        b.write_pod<std::uint32_t>(1);
        b.write_pod<std::uint32_t>(lsw::protocol::kCmdBatch);
        b.write_pod<std::uint32_t>(0);
        check(handler.handle(session, lsw::protocol::kCmdBatch, b.bytes(), reply)
                  == lsw::render::HandleResult::ProtocolError,
              "a nested batch is refused");
    }
}

void test_reset_discards() {
    Harness harness;
    CountingTransport transport{harness};
    CommandRecorder recorder;
    record_one_buffer(recorder, 0, 10);
    check(recorder.recorded_count() == 10, "recorder accumulated 10 commands");
    // vkBeginCommandBuffer / vkResetCommandBuffer both mean the buffer's
    // contents are gone; a surviving batch would replay commands the
    // application discarded.
    recorder.reset();
    check(recorder.recorded_count() == 0, "reset discards the pending batch");
    static_cast<void>(recorder.flush(transport));
    check(transport.messages() == 0, "an empty recorder sends nothing at all");
    check(harness.replayed() == 0, "and nothing reaches the server");
}

}  // namespace

int main() {
    Harness unbatched;
    const std::uint64_t before = run_unbatched(unbatched);
    check(unbatched.replayed() == kThreads * kPerBuffer, "unbatched: every command arrived");
    check(unbatched.order_preserved(kThreads, kPerBuffer),
          "unbatched: per-thread order preserved");

    Harness batched;
    const std::uint64_t after = run_batched(batched);
    check(batched.replayed() == kThreads * kPerBuffer, "batched: every command arrived");
    check(batched.order_preserved(kThreads, kPerBuffer), "batched: per-thread order preserved");

    check(before == kThreads * kPerBuffer, "before: one message per recorded command");
    check(after == kThreads, "after: one message per command buffer");
    check(after < before, "batching reduced the message count");

    test_reset_discards();
    test_malformed_batches();

    std::printf("\n  %u threads x %u commands: %llu messages -> %llu (%.0fx fewer)\n", kThreads,
                kPerBuffer, static_cast<unsigned long long>(before),
                static_cast<unsigned long long>(after),
                static_cast<double>(before) / static_cast<double>(after));

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("OK");
    return 0;
}
