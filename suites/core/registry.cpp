#include "core/registry.hpp"

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

}  // namespace lsw::suites
