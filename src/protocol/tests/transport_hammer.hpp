#pragma once

// The concurrency exercise every ``Transport`` implementation must pass,
// factored out so the socket and the shared-memory transport are held to
// literally the same test rather than to two tests that look alike.
//
// What it is looking for is the failure mode that motivated A1: one
// transport per ``VkInstance``, several application threads inside it,
// and a reply matched to its request by nothing but position. A missing
// lock shows up three ways — a torn frame the server cannot parse, a
// duplicated reply token, or a caller handed somebody else's answer —
// and the payload echo below catches the third, which is the one that
// corrupts silently.

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/transport.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace lsw::protocol::test {

// Command ids the hammer's server half must understand. Any echo server
// works: reply with the request payload, byte for byte.
inline constexpr std::uint32_t kHammerEchoCmd = 0x7000'0001;
inline constexpr std::uint32_t kHammerNoteCmd = 0x7000'0002;  // async, no reply

// One request body: enough to tell every in-flight call apart.
struct HammerTag {
    std::uint32_t thread_id;
    std::uint32_t sequence;
    std::uint64_t nonce;

    [[nodiscard]] bool operator==(const HammerTag&) const = default;
};

struct HammerResult {
    std::atomic<std::uint64_t> sync_calls{0};
    std::atomic<std::uint64_t> async_calls{0};
    std::atomic<std::uint64_t> mismatched_replies{0};  // reply belonged to another caller
    std::atomic<std::uint64_t> short_replies{0};       // reply was not a whole tag
    std::atomic<std::uint64_t> transport_errors{0};

    [[nodiscard]] bool clean() const noexcept {
        return mismatched_replies.load() == 0 && short_replies.load() == 0
               && transport_errors.load() == 0;
    }
};

// Drive ``t`` from ``threads`` threads, ``iterations`` calls each, mixing
// sync and async. Every sync reply must be the exact payload that thread
// sent — that is the assertion the whole thing exists for.
//
// ``async_every`` picks how often a call is fire-and-forget: 0 means
// never, 3 means every third. Interleaving async writes with sync round
// trips is deliberate, because an unlocked async write lands in the
// middle of somebody else's frame just as happily as a sync one does.
inline void hammer_transport(Transport& t, HammerResult& out, unsigned threads = 8,
                             unsigned iterations = 200, unsigned async_every = 3) {
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned id = 0; id < threads; ++id) {
        workers.emplace_back([&, id] {
            Buffer reply;
            for (unsigned i = 0; i < iterations; ++i) {
                HammerTag tag{};
                tag.thread_id = id;
                tag.sequence = i;
                // Distinct per call across the whole run, so a reply that
                // came from another thread cannot accidentally compare
                // equal to the one we were waiting for.
                tag.nonce = (static_cast<std::uint64_t>(id) << 32) ^ ((i * 0x9E37'79B9ULL) + 1);

                const std::span<const std::uint8_t> payload{
                    reinterpret_cast<const std::uint8_t*>(&tag), sizeof(tag)};

                if (async_every != 0 && (i % async_every) == 0) {
                    if (t.send_async(kHammerNoteCmd, payload) != TransportResult::Ok) {
                        out.transport_errors.fetch_add(1);
                        return;
                    }
                    out.async_calls.fetch_add(1);
                    continue;
                }

                reply.clear();
                if (t.send_sync(kHammerEchoCmd, payload, &reply) != TransportResult::Ok) {
                    out.transport_errors.fetch_add(1);
                    return;
                }
                out.sync_calls.fetch_add(1);

                if (reply.size() != sizeof(HammerTag)) {
                    out.short_replies.fetch_add(1);
                    continue;
                }
                HammerTag got{};
                std::memcpy(&got, reply.data(), sizeof(got));
                if (!(got == tag)) {
                    out.mismatched_replies.fetch_add(1);
                }
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }
}

}  // namespace lsw::protocol::test
