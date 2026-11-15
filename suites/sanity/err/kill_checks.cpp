#include "sanity/err/kill_checks.hpp"

#include "core/child_mode.hpp"
#include "support/api_policy.hpp"
#include "support/backend_harness.hpp"
#include "support/fresh_process.hpp"
#include "support/gate_window.hpp"
#include "support/gpu_backend.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"

#include <windows.h>

#include <algorithm>
#include <random>

namespace lsw::suites::sanity::err {

namespace {

constexpr int kKills = 20;
constexpr std::uint64_t kBufferBytes = 64ull << 20;

Outcome submit_loop(Context& context, const std::string& api) {
    AvAdapter adapter;
    std::string error;
    GpuBackend* backend = BackendHarness::open(api, adapter, error).release();
    if (backend == nullptr) {
        return Context::skip(error);
    }
    const std::uint64_t size = std::min<std::uint64_t>(kBufferBytes, backend->max_allocation() != 0 ? backend->max_allocation() : kBufferBytes);
    AllocStatus status = AllocStatus::Ok;
    std::string detail;
    GpuBuffer* first = backend->create(size, BufferKind::Device, status, detail).release();
    GpuBuffer* second = first != nullptr ? backend->create(size, BufferKind::Device, status, detail).release() : nullptr;
    if (second == nullptr) {
        return Outcome::fail(api + ": creating the buffers failed: " + detail);
    }
    if (!BackendHarness::fill_and_verify(*backend, *first, 4242, std::min<std::uint64_t>(size, 1 << 20), context, error)) {
        return Outcome::fail(api + ": " + error);
    }
    RobustChild::ready();
    const ULONGLONG end = GetTickCount64() + 60000;
    while (GetTickCount64() < end) {
        if (!backend->copy(*first, 0, *second, 0, size, error) || !backend->copy(*second, 0, *first, 0, size, error)) {
            return Outcome::fail(api + ": a copy failed: " + error);
        }
    }
    return Outcome::ok();
}

Outcome role(Context& context, const std::string& name) {
    const std::string prefix = "submit-";
    if (name.rfind(prefix, 0) != 0) {
        return Outcome::fail("unknown role " + name);
    }
    return submit_loop(context, name.substr(prefix.size()));
}

}  // namespace

Outcome KillChecks::mid_submit(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, role);
    }
    const std::vector<std::string> apis = ApiPolicy::filter({"d3d11", "d3d12", "vulkan", "opencl", "gl", "d3d9"});
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    const unsigned seed = static_cast<unsigned>(::GetTickCount64());
    std::mt19937 random(seed);
    context.measure("seed", seed);
    Problems problems;
    GateWindow window(context.id());
    window.open(problems);
    int killed = 0;
    int skipped = 0;
    for (int i = 0; i < kKills; ++i) {
        const std::string& api = apis[static_cast<std::size_t>(i) % apis.size()];
        const int delay = static_cast<int>(random() % 600);
        const CheckResult result = RobustChild::run(context.id(), "submit-" + api, {}, 90, delay);
        if (result.status == Status::Timeout && result.reason.rfind("terminated", 0) == 0) {
            ++killed;
        } else if (result.status == Status::Skip) {
            ++skipped;
        } else {
            problems.add("round " + std::to_string(i) + " (" + api + ", kill after " + std::to_string(delay) + " ms): " + Outcome::name(result.status) + " " + result.reason);
        }
    }
    context.measure("killed", killed);
    context.measure("skipped", skipped);
    window.close(context, problems, "mid_submit", 5000000);
    FreshProcess::check(context, problems, context.id(), "mid_submit");
    if (!problems.empty()) {
        return problems.outcome();
    }
    if (killed == 0) {
        return Context::skip("no child reached its submit loop");
    }
    return Outcome::ok();
}

}  // namespace lsw::suites::sanity::err
