#pragma once

#include "lsw/host/link/agent_reply.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace lsw::host::link {

// The calls waiting on a reply, by id.
//
// A caller opens a ``Ticket`` before sending and waits on it; the reader
// thread hands the reply to whichever ticket holds that id. A reply for
// an id nobody holds — the caller timed out, or the request was one-way
// — is dropped.
//
// Delivery happens under the table's lock and a ticket leaves the table
// under the same lock, so a reply racing a timeout either lands before
// the ticket is gone or finds no ticket at all; it never writes into a
// waiter that has already returned.
class PendingCalls {
public:
    class Ticket {
    public:
        Ticket(PendingCalls& owner, std::uint64_t id);
        ~Ticket();

        Ticket(const Ticket&) = delete;
        Ticket& operator=(const Ticket&) = delete;
        Ticket(Ticket&&) = delete;
        Ticket& operator=(Ticket&&) = delete;

        [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

        // The reply, or nullopt once ``timeout`` passes without one.
        [[nodiscard]] std::optional<AgentReply> wait(std::chrono::milliseconds timeout);

    private:
        friend class PendingCalls;

        PendingCalls& owner_;
        std::uint64_t id_;
        std::mutex mu_;
        std::condition_variable cv_;
        bool done_ = false;
        AgentReply reply_;
    };

    void deliver(std::uint64_t id, AgentReply reply);

    // Release every waiter with ``why``: the connection their replies
    // would have come back on is gone.
    void fail_all(const std::string& why);

private:
    // Caller holds mu_.
    static void complete_locked(Ticket& ticket, AgentReply reply);

    std::mutex mu_;
    std::unordered_map<std::uint64_t, Ticket*> open_;
};

}  // namespace lsw::host::link
