#include "sanity/err/shader_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/err/spin_workload.hpp"
#include "support/api_policy.hpp"
#include "support/fresh_process.hpp"
#include "support/gate_window.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"

namespace lsw::suites::sanity::err {

namespace {

constexpr long long kPeerLimitUs = 5000000;
constexpr int kKillAfterReadyMs = 3000;

Outcome role(Context& context, const std::string& name) {
    const std::string forever_prefix = "spin-forever-";
    const std::string prefix = "spin-";
    if (name.rfind(forever_prefix, 0) == 0) {
        return SpinWorkload::run(context, name.substr(forever_prefix.size()), true);
    }
    if (name.rfind(prefix, 0) == 0) {
        return SpinWorkload::run(context, name.substr(prefix.size()), false);
    }
    return Outcome::fail("unknown role " + name);
}

Outcome drive(Context& context, bool forever) {
    if (ChildMode::active()) {
        return RobustChild::child(context, role);
    }
    const std::vector<std::string> apis = ApiPolicy::filter(forever ? std::vector<std::string>{"d3d11", "d3d12", "vulkan", "opencl"}
                                                                    : std::vector<std::string>{"d3d11", "d3d12", "vulkan"});
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    std::vector<std::string> skipped;
    GateWindow window(context.id());
    window.open(problems);
    for (const std::string& api : apis) {
        const CheckResult result = RobustChild::run(context.id(), (forever ? "spin-forever-" : "spin-") + api, {}, 240, forever ? kKillAfterReadyMs : -1);
        context.measure(api + ".child_ms", static_cast<long long>(result.ms));
        context.measure(api + ".child_status", Outcome::name(result.status));
        RobustChild::forward(context, api + ".", result);
        if (result.status == Status::Skip) {
            skipped.push_back(api + ": " + result.reason);
        } else if (result.status == Status::Timeout && forever && result.reason.rfind("terminated", 0) == 0) {
            context.measure(api + ".spin.result", "no loss reported before the process was terminated");
        } else if (result.status != Status::Ok) {
            problems.add(api + ": " + Outcome::name(result.status) + " " + result.reason);
        }
    }
    window.close(context, problems, forever ? "infinite" : "long_running", kPeerLimitUs);
    FreshProcess::check(context, problems, context.id(), forever ? "infinite" : "long_running");
    if (!problems.empty()) {
        return problems.outcome();
    }
    if (skipped.size() == apis.size()) {
        return Context::skip(skipped.front());
    }
    for (const std::string& note : skipped) {
        context.note(note);
    }
    return Outcome::ok();
}

}  // namespace

Outcome ShaderChecks::long_running(Context& context) {
    return drive(context, false);
}

Outcome ShaderChecks::infinite(Context& context) {
    return drive(context, true);
}

}  // namespace lsw::suites::sanity::err
