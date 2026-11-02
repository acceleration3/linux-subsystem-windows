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

    void fail_all(const std::string& why);

private:
    static void complete_locked(Ticket& ticket, AgentReply reply);

    std::mutex mu_;
    std::unordered_map<std::uint64_t, Ticket*> open_;
};

}
