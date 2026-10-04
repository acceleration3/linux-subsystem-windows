#include "lsw/agent/session/pending_calls.hpp"

#include <stdexcept>
#include <utility>

namespace lsw::agent::session {

std::uint64_t PendingCalls::open() {
    const std::lock_guard lock(mu_);
    if (closed_) {
        throw std::runtime_error(*closed_);
    }
    const std::uint64_t id = next_id_++;
    slots_.emplace(id, Slot{});
    return id;
}

bool PendingCalls::complete(std::uint64_t id, nlohmann::json reply) {
    {
        const std::lock_guard lock(mu_);
        const auto it = slots_.find(id);
        if (it == slots_.end() || it->second.reply) {
            return false;
        }
        it->second.reply = std::move(reply);
    }
    cv_.notify_all();
    return true;
}

std::optional<nlohmann::json> PendingCalls::wait(std::uint64_t id,
                                                 std::chrono::milliseconds timeout) {
    std::unique_lock lock(mu_);
    const auto ready = [&] {
        const auto it = slots_.find(id);
        return closed_.has_value() || it == slots_.end() || it->second.reply.has_value();
    };
    cv_.wait_for(lock, timeout, ready);

    const auto it = slots_.find(id);
    if (it != slots_.end() && it->second.reply) {
        nlohmann::json reply = std::move(*it->second.reply);
        slots_.erase(it);
        return reply;
    }
    if (it != slots_.end()) {
        slots_.erase(it);
    }
    if (closed_) {
        throw std::runtime_error(*closed_);
    }
    return std::nullopt;
}

void PendingCalls::cancel(std::uint64_t id) {
    const std::lock_guard lock(mu_);
    slots_.erase(id);
}

void PendingCalls::close(std::string reason) {
    {
        const std::lock_guard lock(mu_);
        if (!closed_) {
            closed_ = std::move(reason);
        }
    }
    cv_.notify_all();
}

std::size_t PendingCalls::in_flight() const {
    const std::lock_guard lock(mu_);
    return slots_.size();
}

}  // namespace lsw::agent::session
