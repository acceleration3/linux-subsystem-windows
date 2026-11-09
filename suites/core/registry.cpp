#include "core/registry.hpp"

#include <set>

#include "core/outcome.hpp"

namespace lsw::suites {

const char* Outcome::name(Status status) {
    switch (status) {
    case Status::Ok:
        return "ok";
    case Status::Fail:
        return "fail";
    case Status::Skip:
        return "skip";
    case Status::Timeout:
        return "timeout";
    case Status::Crash:
        return "crash";
    }
    return "fail";
}

Suite& Registry::add_suite(std::string name, Policy policy) {
    suites_.emplace_back(std::move(name), policy);
    return suites_.back();
}

const Suite* Registry::find_suite(const std::string& name) const {
    for (const Suite& suite : suites_) {
        if (suite.name() == name) {
            return &suite;
        }
    }
    return nullptr;
}

const Check* Registry::find_check(const std::string& id, const Suite** owner) const {
    for (const Suite& suite : suites_) {
        for (const Check& check : suite.checks()) {
            if (check.id == id) {
                if (owner != nullptr) {
                    *owner = &suite;
                }
                return &check;
            }
        }
    }
    return nullptr;
}

std::vector<std::string> Registry::problems() const {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const Suite& suite : suites_) {
        out.insert(out.end(), suite.problems().begin(), suite.problems().end());
        for (const Check& check : suite.checks()) {
            if (!seen.insert(check.id).second) {
                out.push_back("check id " + check.id + " appears in more than one suite");
            }
        }
    }
    for (std::size_t i = 0; i < suites_.size(); ++i) {
        for (std::size_t j = i + 1; j < suites_.size(); ++j) {
            if (suites_[i].name() == suites_[j].name()) {
                out.push_back("duplicate suite " + suites_[i].name());
            }
        }
    }
    return out;
}

}  // namespace lsw::suites
