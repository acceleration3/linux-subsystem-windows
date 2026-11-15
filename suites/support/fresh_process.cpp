#include "support/fresh_process.hpp"

#include "support/child_runs.hpp"
#include "support/render_probe.hpp"

namespace lsw::suites {

void FreshProcess::check(Context& context, Problems& problems, const std::string& id, const std::string& label) {
    ChildRunOptions options;
    options.timeout_seconds = 90;
    options.env = {{"LSW_SUITES_ROLE", "fresh-probe"}};
    const CheckResult result = ChildRuns::run_self(id, options);
    context.measure(label + ".fresh_process_ms", static_cast<long long>(result.ms));
    if (result.status != Status::Ok) {
        problems.add("a process started after " + label + " could not render and read back: " + Outcome::name(result.status) + " " + result.reason);
    }
}

Outcome FreshProcess::child(Context&) {
    RenderProbe probe;
    std::string error;
    if (!probe.open(error)) {
        return Outcome::fail(error);
    }
    for (unsigned step = 0; step < 5; ++step) {
        if (!probe.cycle(step * 40, error)) {
            return Outcome::fail(error);
        }
    }
    return Outcome::ok();
}

}  // namespace lsw::suites
