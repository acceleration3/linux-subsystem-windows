#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::apps {
class LaunchContinuation {
public:
    struct Candidate {
        unsigned long pid = 0;
        unsigned long session = 0;
        std::wstring image_lc;
        std::uint64_t created = 0;
        bool owned_by_other_launch = false;
    };

    LaunchContinuation(unsigned long root, unsigned long session, std::wstring image_lc,
                       std::uint64_t started)
        : root_(root), session_(session), image_lc_(std::move(image_lc)), started_(started) {}

    [[nodiscard]] std::optional<unsigned long> pick(const std::vector<Candidate>& candidates) const;

private:
    unsigned long root_;
    unsigned long session_;
    std::wstring image_lc_;
    std::uint64_t started_;
};

}
