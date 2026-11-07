#include "sanity/iso/fleet.hpp"

#include "support/robust_child.hpp"

namespace lsw::suites::sanity::iso {

void Fleet::start(const std::string& id, const std::string& label, const std::string& role,
                  const std::vector<std::pair<std::string, std::string>>& env, int timeout_seconds,
                  int kill_after_ready_ms) {
    std::size_t index = 0;
    {
        std::lock_guard lock(mutex_);
        members_.reserve(64);
        members_.push_back({label, {}});
        index = members_.size() - 1;
    }
    threads_.emplace_back([=, this] {
        CheckResult result = RobustChild::run(id, role, env, timeout_seconds, kill_after_ready_ms);
        std::lock_guard lock(mutex_);
        members_[index].result = std::move(result);
    });
}

void Fleet::join() {
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

}  // namespace lsw::suites::sanity::iso
