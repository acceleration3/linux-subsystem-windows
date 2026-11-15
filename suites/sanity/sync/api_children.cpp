#include "sanity/sync/api_children.hpp"

#include "core/child_mode.hpp"
#include "support/api_policy.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"

namespace lsw::suites::sanity::sync {

namespace {

constexpr const char* kPrefix = "api-";

}  // namespace

Outcome ApiChildren::run(Context& context, const std::vector<std::string>& apis, const Body& body, int timeout_seconds) {
    if (ChildMode::active()) {
        return RobustChild::child(context, [&](Context& inner, const std::string& role) {
            const std::string prefix = kPrefix;
            if (role.rfind(prefix, 0) != 0) {
                return Outcome::fail("unknown role " + role);
            }
            return body(inner, role.substr(prefix.size()));
        });
    }
    const std::vector<std::string> enabled = ApiPolicy::filter(apis);
    if (enabled.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    int ran = 0;
    std::string skips;
    for (const std::string& api : enabled) {
        const CheckResult result = RobustChild::run(context.id(), kPrefix + api, {}, timeout_seconds, -1);
        RobustChild::forward(context, api + ".", result);
        if (result.status == Status::Ok) {
            ++ran;
        } else if (result.status == Status::Skip) {
            skips += (skips.empty() ? "" : "; ") + api + ": " + result.reason;
        } else {
            problems.add(api + ": " + Outcome::name(result.status) + " " + result.reason);
        }
    }
    if (!problems.empty()) {
        return problems.outcome();
    }
    if (ran == 0) {
        return Context::skip(skips);
    }
    if (!skips.empty()) {
        context.note("skipped: " + skips);
    }
    return Outcome::ok();
}

}  // namespace lsw::suites::sanity::sync
