#include "sanity/err/delegate.hpp"

#include "support/problems.hpp"

namespace lsw::suites::sanity::err {

Outcome Delegate::all(Context& context, const std::vector<Part>& parts) {
    Problems problems;
    int ran = 0;
    std::string skips;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const Outcome outcome = parts[i](context);
        if (outcome.status() == Status::Fail) {
            problems.add("part " + std::to_string(i + 1) + ": " + outcome.reason());
        } else if (outcome.status() == Status::Skip) {
            skips += (skips.empty() ? "" : "; ") + outcome.reason();
        } else {
            ++ran;
        }
    }
    if (!problems.empty()) {
        return problems.outcome();
    }
    return ran == 0 ? Context::skip(skips) : Outcome::ok();
}

}  // namespace lsw::suites::sanity::err
