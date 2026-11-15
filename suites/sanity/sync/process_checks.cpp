#include "sanity/sync/process_checks.hpp"

#include "core/child_mode.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/shared_fence.hpp"
#include "support/stress_load.hpp"
#include "support/target_adapter.hpp"

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <thread>

namespace lsw::suites::sanity::sync {

namespace {

constexpr int kProcesses = 16;
constexpr int kRounds = 2000;
constexpr DWORD kStepMs = 60000;

Outcome work(Context& context, const std::string& role) {
    if (role != "work") {
        return Outcome::fail("unknown role " + role);
    }
    return StressLoad::roundtrips(context, 6.0, {65536, 1u << 20, 4u << 20});
}

}  // namespace

Outcome ProcessChecks::many_processes(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, work);
    }
    Problems problems;
    std::mutex mutex;
    long long slowest = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < kProcesses; ++i) {
        threads.emplace_back([&, i] {
            const CheckResult result = RobustChild::run(context.id(), "work", {}, 300, -1);
            std::lock_guard lock(mutex);
            slowest = std::max(slowest, result.ms);
            if (result.status != Status::Ok) {
                problems.add("process " + std::to_string(i) + ": " + Outcome::name(result.status) + " " + result.reason);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    context.measure("slowest_process_ms", slowest);
    return problems.outcome();
}

Outcome ProcessChecks::shared_fence_ping_pong(Context& context) {
    if (ApiPolicy::is_skipped("d3d12")) {
        return Context::skip("d3d12 excluded by LSW_SUITES_SKIP_APIS");
    }
    const std::string peer = ChildMode::variable(L"LSW_SUITES_FENCE_NAME");
    AvAdapter adapter;
    std::string error;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D12Fence> fence;
    if (ChildMode::active() && !peer.empty()) {
        if (!SharedFence::open(rig, peer, fence, error)) {
            return Outcome::fail(error);
        }
        for (int i = 0; i < kRounds; ++i) {
            const std::uint64_t seen = static_cast<std::uint64_t>(2 * i + 1);
            if (!SharedFence::reaches(fence.get(), seen, kStepMs)) {
                return Outcome::fail("the peer never signalled " + std::to_string(seen));
            }
            if (fence->GetCompletedValue() != seen) {
                return Outcome::fail("woken for " + std::to_string(seen) + " but the fence holds " + std::to_string(fence->GetCompletedValue()));
            }
            fence->Signal(seen + 1);
        }
        return Outcome::ok();
    }
    HANDLE handle = nullptr;
    const std::string name = "Local\\lsw-suites-pingpong-" + std::to_string(::GetCurrentProcessId());
    if (!SharedFence::create(rig, name, fence, handle, error)) {
        return Context::skip("shared fences are unsupported: " + error);
    }
    ChildRunOptions options;
    options.timeout_seconds = 300;
    options.env = {{"LSW_SUITES_FENCE_NAME", name}};
    CheckResult child;
    std::thread runner([&] { child = ChildRuns::run_self(context.id(), options); });
    std::string failure;
    for (int i = 0; i < kRounds && failure.empty(); ++i) {
        const std::uint64_t mine = static_cast<std::uint64_t>(2 * i);
        if (!SharedFence::reaches(fence.get(), mine, kStepMs)) {
            failure = "the peer never signalled " + std::to_string(mine);
        } else if (fence->GetCompletedValue() != mine) {
            failure = "woken for " + std::to_string(mine) + " but the fence holds " + std::to_string(fence->GetCompletedValue());
        } else {
            fence->Signal(mine + 1);
        }
    }
    if (failure.empty() && !SharedFence::reaches(fence.get(), 2 * kRounds, kStepMs)) {
        failure = "the peer never signalled the final value";
    }
    if (failure.empty() && fence->GetCompletedValue() != static_cast<std::uint64_t>(2 * kRounds)) {
        failure = "the final value is " + std::to_string(fence->GetCompletedValue()) + ", expected " + std::to_string(2 * kRounds);
    }
    if (!failure.empty()) {
        fence->Signal(static_cast<std::uint64_t>(4 * kRounds));
    }
    runner.join();
    ::CloseHandle(handle);
    Problems problems;
    if (!failure.empty()) {
        problems.add(failure);
    }
    if (child.status != Status::Ok) {
        problems.add("the peer process: " + std::string(Outcome::name(child.status)) + " " + child.reason);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::sync
