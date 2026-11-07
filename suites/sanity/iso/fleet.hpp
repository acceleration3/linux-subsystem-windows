#pragma once

#include "core/check_result.hpp"
#include "core/context.hpp"

#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lsw::suites::sanity::iso {
struct FleetMember {
    std::string label;
    CheckResult result;
};

class Fleet {
public:
    Fleet() = default;
    Fleet(const Fleet&) = delete;
    Fleet& operator=(const Fleet&) = delete;
    ~Fleet() { join(); }

    void start(const std::string& id, const std::string& label, const std::string& role,
               const std::vector<std::pair<std::string, std::string>>& env, int timeout_seconds,
               int kill_after_ready_ms = -1);

    void join();

    [[nodiscard]] const std::vector<FleetMember>& members() const { return members_; }

private:
    std::vector<std::thread> threads_;
    std::vector<FleetMember> members_;
    std::mutex mutex_;
};
}  // namespace lsw::suites::sanity::iso
