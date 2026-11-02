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
class PendingCalls {
public:
    [[nodiscard]] std::uint64_t open();

    bool complete(std::uint64_t id, nlohmann::json reply);

    [[nodiscard]] std::optional<nlohmann::json> wait(std::uint64_t id,
                                                     std::chrono::milliseconds timeout);

    void cancel(std::uint64_t id);

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

}
