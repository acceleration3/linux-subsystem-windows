#pragma once

#include "core/check.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lsw::suites {
enum class Policy { Parallel, Serial };

class Suite {
public:
    Suite(std::string name, Policy policy) : name_(std::move(name)), policy_(policy) {}

    Suite& add(Check check) {
        if (!ids_.insert(check.id).second) {
            problems_.push_back("duplicate check id " + check.id + " in suite " + name_);
            return *this;
        }
        checks_.push_back(std::move(check));
        return *this;
    }

    void add_problem(std::string text) { problems_.push_back(std::move(text)); }

    [[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }

    [[nodiscard]] const std::string& name() const { return name_; }

    [[nodiscard]] Policy policy() const { return policy_; }

    [[nodiscard]] const std::vector<Check>& checks() const { return checks_; }

private:
    std::string name_;
    Policy policy_;
    std::vector<Check> checks_;
    std::set<std::string> ids_;
    std::vector<std::string> problems_;
};
}  // namespace lsw::suites
