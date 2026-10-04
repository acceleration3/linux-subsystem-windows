#include "lsw/host/link/pending_calls.hpp"

#include <utility>

namespace lsw::host::link {

PendingCalls::Ticket::Ticket(PendingCalls& owner, std::uint64_t id) : owner_(owner), id_(id) {
    std::lock_guard<std::mutex> lock(owner_.mu_);
    owner_.open_.emplace(id_, this);
}

PendingCalls::Ticket::~Ticket() {
    std::lock_guard<std::mutex> lock(owner_.mu_);
    owner_.open_.erase(id_);
}

std::optional<AgentReply> PendingCalls::Ticket::wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    if (!cv_.wait_for(lock, timeout, [this] { return done_; })) {
        return std::nullopt;
    }
    return std::move(reply_);
}

void PendingCalls::complete_locked(Ticket& ticket, AgentReply reply) {
    std::lock_guard<std::mutex> lock(ticket.mu_);
    ticket.reply_ = std::move(reply);
    ticket.done_ = true;
    ticket.cv_.notify_all();
}

void PendingCalls::deliver(std::uint64_t id, AgentReply reply) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = open_.find(id);
    if (it != open_.end()) {
        complete_locked(*it->second, std::move(reply));
    }
}

void PendingCalls::fail_all(const std::string& why) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [id, ticket] : open_) {
        complete_locked(*ticket, AgentReply::failure(why));
    }
}

}  // namespace lsw::host::link
