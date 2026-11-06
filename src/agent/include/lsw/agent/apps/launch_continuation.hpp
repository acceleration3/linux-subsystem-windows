#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lsw::agent::apps {
class LaunchContinuation {
public:
    struct Candidate {
        unsigned long pid = 0;
        unsigned long parent = 0;
        unsigned long session = 0;
        std::wstring image_lc;
        std::uint64_t created = 0;
        bool in_job = false;
        bool parent_is_explorer = false;
        bool parent_in_launch = false;
        bool owned_by_other_launch = false;
    };

    struct Root {
        unsigned long pid = 0;
        unsigned long session = 0;
        std::wstring image_lc;
        std::uint64_t started = 0;
        bool runs_own_executable = false;
        std::vector<std::uint64_t> newer_launch_starts;
    };

    explicit LaunchContinuation(Root root) : root_(std::move(root)) {}

    [[nodiscard]] bool worth_following(std::uint64_t now) const;
    [[nodiscard]] static bool keep_polling(unsigned long elapsed_ms);
    [[nodiscard]] std::optional<unsigned long> pick(const std::vector<Candidate>& candidates) const;
    void reject(unsigned long pid) { rejected_.insert(pid); }
    void followed(unsigned long pid) { chain_.insert(pid); }

    [[nodiscard]] static constexpr unsigned long poll_ms() { return 100; }

private:
    Root root_;
    std::unordered_set<unsigned long> rejected_;
    std::unordered_set<unsigned long> chain_;
};

}
