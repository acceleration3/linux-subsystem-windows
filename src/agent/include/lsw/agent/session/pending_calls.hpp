#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace lsw::agent::session {

// Requests sent to a session helper that are waiting for their reply.
//
// The service forwards a command from a connection thread and blocks
// that thread on the answer, which the pipe's reader thread delivers.
// Every wait is bounded and every waiter is woken when the helper goes
// away: the one thing a forwarded request must never do is hang the
// manager's connection behind a helper that died or wedged.
class PendingCalls {
public:
    // Allocate an id for a request about to be sent. Throws
    // std::runtime_error with the close reason once ``close`` was called.
    [[nodiscard]] std::uint64_t open();

    // Deliver the reply for ``id``. False when nobody is waiting for it
    // any more (timed out, or never sent) — a late reply is dropped.
    bool complete(std::uint64_t id, nlohmann::json reply);

    // Block until ``id``'s reply arrives, up to ``timeout``. Nullopt on
    // timeout (the id is then forgotten, so a late reply is dropped).
    // Throws std::runtime_error with the close reason if the channel
    // closes first.
    [[nodiscard]] std::optional<nlohmann::json> wait(std::uint64_t id,
                                                     std::chrono::milliseconds timeout);

    // Forget ``id`` without waiting — its request could not be sent.
    void cancel(std::uint64_t id);

    // Fail every waiter, and every later ``open``, with ``reason``.
    void close(std::string reason);

    [[nodiscard]] std::size_t in_flight() const;

private:
    struct Slot {
        std::optional<nlohmann::json> reply;
    };

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::uint64_t next_id_ = 1;
    std::unordered_map<std::uint64_t, Slot> slots_;
    std::optional<std::string> closed_;
};

}  // namespace lsw::agent::session
