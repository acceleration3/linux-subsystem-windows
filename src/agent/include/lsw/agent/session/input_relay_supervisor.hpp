#pragma once

#include "lsw/agent/win/unique_handle.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

namespace lsw::agent::session {
class InputRelaySupervisor {
public:
    InputRelaySupervisor() = default;
    ~InputRelaySupervisor();

    InputRelaySupervisor(const InputRelaySupervisor&) = delete;
    InputRelaySupervisor& operator=(const InputRelaySupervisor&) = delete;

    void reconcile(const std::vector<std::uint32_t>& sessions);

    void stop_all();

private:
    struct Relay {
        win::UniqueHandle process;
        std::uint32_t pid = 0;
        std::chrono::steady_clock::time_point started{};
        std::chrono::steady_clock::time_point retry_at{};
        std::chrono::seconds backoff{1};
    };

    [[nodiscard]] static bool exited(const Relay& relay, std::uint32_t session);
    [[nodiscard]] bool launch(std::uint32_t session, Relay& relay);

    std::map<std::uint32_t, Relay> relays_;
};

}
