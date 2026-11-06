#pragma once

#include "core/check.hpp"

#include <string>
#include <utility>
#include <vector>

namespace lsw::suites {
enum class Policy { Parallel, Serial };

class Suite {
public:
    Suite(std::string name, Policy policy) : name_(std::move(name)), policy_(policy) {}

    Suite& add(Check check) {
        checks_.push_back(std::move(check));
        return *this;
    }

    [[nodiscard]] const std::string& name() const { return name_; }

    [[nodiscard]] Policy policy() const { return policy_; }

    [[nodiscard]] const std::vector<Check>& checks() const { return checks_; }

private:
    std::string name_;
    Policy policy_;
    std::vector<Check> checks_;
};
}  // namespace lsw::suites
