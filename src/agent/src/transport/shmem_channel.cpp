#include "lsw/agent/transport/shmem_channel.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/transport/shmem.hpp"

#include "lsw/linkproto/region.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace lsw::agent::transport {

bool ShmemChannel::available() const {
    return shmem_->available() && shmem_->rpc_to_host().valid();
}

bool ShmemChannel::send(const nlohmann::json& message) {
    if (!available()) {
        return false;
    }
    const std::string body = message.dump();
    auto& ring = shmem_->rpc_to_host();
    if (!ring.write(reinterpret_cast<const std::uint8_t*>(body.data()), body.size())) {
        // Full ring, or a message larger than the ring can ever hold.
        // Either way the caller's message did not go; it decides whether
        // that is fatal or just a dropped progress tick.
        return false;
    }
    shmem_->ring_rpc_doorbell();
    return true;
}

std::vector<nlohmann::json> ShmemChannel::receive() {
    std::vector<nlohmann::json> out;
    if (!shmem_->available()) {
        return out;
    }
    auto& ring = shmem_->rpc_from_host();
    if (!ring.valid() || ring.empty()) {
        return out;
    }
    if (scratch_.size() < ring.max_message()) {
        scratch_.resize(ring.max_message());
    }
    // Every ``read`` either consumes a record or resynchronises the
    // ring, so draining until empty always terminates.
    while (!ring.empty()) {
        const std::size_t n = ring.read(scratch_.data(), scratch_.size());
        if (n == 0) {
            continue;
        }
        try {
            out.push_back(nlohmann::json::parse(scratch_.data(), scratch_.data() + n));
        } catch (const nlohmann::json::exception& e) {
            LSW_AGENT_WARN("shmem_channel: malformed message ({}) len={}", e.what(), n);
        }
    }
    return out;
}

std::uint64_t ShmemChannel::dropped() const {
    return shmem_->rpc_to_host().dropped();
}

}  // namespace lsw::agent::transport
