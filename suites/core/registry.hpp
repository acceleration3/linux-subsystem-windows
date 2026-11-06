#pragma once

#include "core/suite.hpp"

#include <deque>
#include <string>

namespace lsw::suites {
class Registry {
public:
    Suite& add_suite(std::string name, Policy policy);

    [[nodiscard]] const std::deque<Suite>& suites() const { return suites_; }

    [[nodiscard]] const Suite* find_suite(const std::string& name) const;

    [[nodiscard]] const Check* find_check(const std::string& id,
                                          const Suite** owner = nullptr) const;

private:
    std::deque<Suite> suites_;
};
}  // namespace lsw::suites
