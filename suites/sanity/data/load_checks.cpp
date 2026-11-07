#include "sanity/data/load_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/child_runs.hpp"
#include "support/problems.hpp"
#include "support/stress_load.hpp"

#include <windows.h>

#include <thread>

namespace lsw::suites::sanity::data {

namespace {

const std::vector<std::uint64_t> kStressSizes = {4096, 65537, 1u << 20, 8u << 20};

ChildRunOptions role(const std::string& name, double seconds, int timeout) {
    ChildRunOptions options;
    options.timeout_seconds = timeout;
    options.env = {{"LSW_SUITES_ROLE", name}, {"LSW_SUITES_SECONDS", std::to_string(seconds)}};
    return options;
}

double child_seconds() {
    const std::string text = ChildMode::variable(L"LSW_SUITES_SECONDS");
    return text.empty() ? 5.0 : std::stod(text);
}

Outcome run_role(Context& context, const std::string& name) {
    const double seconds = child_seconds();
    if (name == "render") {
        return StressLoad::render(context, seconds);
    }
    if (name == "compute") {
        return StressLoad::compute(context, seconds);
    }
    if (name == "hog") {
        return StressLoad::hog(context, seconds);
    }
    return StressLoad::roundtrips(context, seconds, kStressSizes);
}

}  // namespace

Outcome LoadChecks::parallel_processes(Context& context) {
    if (ChildMode::active()) {
        return run_role(context, ChildMode::variable(L"LSW_SUITES_ROLE"));
    }
    constexpr int kProcesses = 8;
    std::vector<CheckResult> results(kProcesses);
    std::vector<std::thread> threads;
    for (int i = 0; i < kProcesses; ++i) {
        threads.emplace_back([&results, i] {
            results[static_cast<std::size_t>(i)] =
                ChildRuns::run_self("sanity.data.load.parallel_processes", role("stress", 8.0, 120));
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    Problems problems;
    for (int i = 0; i < kProcesses; ++i) {
        const CheckResult& result = results[static_cast<std::size_t>(i)];
        if (result.status != Status::Ok) {
            problems.add("process " + std::to_string(i) + ": " + Outcome::name(result.status) + " " + result.reason);
        }
    }
    return problems.outcome();
}

Outcome LoadChecks::parallel_with_render_load(Context& context) {
    if (ChildMode::active()) {
        return run_role(context, ChildMode::variable(L"LSW_SUITES_ROLE"));
    }
    CheckResult render;
    CheckResult compute;
    std::thread render_thread([&] { render = ChildRuns::run_self("sanity.data.load.parallel_with_render_load", role("render", 12.0, 90)); });
    std::thread compute_thread([&] { compute = ChildRuns::run_self("sanity.data.load.parallel_with_render_load", role("compute", 12.0, 90)); });
    ::Sleep(1000);
    StressResult stress;
    const Outcome outcome = StressLoad::roundtrips(context, 8.0, kStressSizes, &stress);
    render_thread.join();
    compute_thread.join();
    context.measure("roundtrip.mib_per_s", static_cast<double>(stress.bytes) / 1048576.0 / stress.seconds);
    Problems problems;
    if (outcome.status() != Status::Ok) {
        problems.add(outcome.reason());
    }
    if (render.status != Status::Ok) {
        problems.add("render load process: " + render.reason);
    }
    if (compute.status != Status::Ok) {
        problems.add("compute load process: " + compute.reason);
    }
    return problems.outcome();
}

Outcome LoadChecks::memory_pressure_host(Context& context) {
    const std::string declared = ChildMode::variable(L"LSW_SUITES_HOST_PRESSURE");
    context.measure("host_pressure_declared", !declared.empty());
    StressResult stress;
    std::vector<std::uint64_t> sizes;
    for (std::uint64_t size : {(4ull << 20) - 1, 4ull << 20, (4ull << 20) + 1, (16ull << 20) + 1, 32ull << 20, 64ull << 20}) {
        sizes.push_back(size);
    }
    const Outcome outcome = StressLoad::roundtrips(context, 16.0, sizes, &stress);
    context.measure("roundtrip.mib_per_s", static_cast<double>(stress.bytes) / 1048576.0 / stress.seconds);
    return outcome;
}

Outcome LoadChecks::memory_pressure_guest(Context& context) {
    if (ChildMode::active()) {
        return run_role(context, ChildMode::variable(L"LSW_SUITES_ROLE"));
    }
    const std::string event_name = "Local\\lsw-suites-hog-" + std::to_string(::GetCurrentProcessId());
    HANDLE ready = ::CreateEventW(nullptr, TRUE, FALSE, Text::wide(event_name).c_str());
    ChildRunOptions options = role("hog", 25.0, 90);
    options.env.emplace_back("LSW_SUITES_READY_EVENT", event_name);
    CheckResult hog;
    std::thread hog_thread([&] { hog = ChildRuns::run_self("sanity.data.load.memory_pressure_guest", options); });
    const bool committed = ::WaitForSingleObject(ready, 60000) == WAIT_OBJECT_0;
    ::CloseHandle(ready);
    StressResult stress;
    std::vector<std::uint64_t> sizes = {(4ull << 20) - 1, 4ull << 20, (16ull << 20) + 1, 32ull << 20, 64ull << 20};
    const Outcome outcome = committed ? StressLoad::roundtrips(context, 12.0, sizes, &stress) : Outcome::fail("the memory hog never reported");
    hog_thread.join();
    context.measure("hog.bytes", ChildRuns::number(hog, "hog.bytes"));
    Problems problems;
    if (outcome.status() != Status::Ok) {
        problems.add(outcome.reason());
    }
    if (hog.status != Status::Ok) {
        problems.add("memory hog: " + hog.reason);
    }
    return problems.outcome();
}

Outcome LoadChecks::sustained_30s(Context& context) {
    constexpr int kWindows = 3;
    double throughput[kWindows] = {};
    for (int window = 0; window < kWindows; ++window) {
        StressResult stress;
        const Outcome outcome = StressLoad::roundtrips(context, 10.0, kStressSizes, &stress);
        if (outcome.status() != Status::Ok) {
            return outcome;
        }
        throughput[window] = static_cast<double>(stress.bytes) / 1048576.0 / stress.seconds;
        context.measure("window" + std::to_string(window) + ".mib_per_s", throughput[window]);
    }
    if (throughput[2] < throughput[0] / 2.0) {
        return Outcome::fail("throughput fell from " + std::to_string(throughput[0]) + " to " +
                             std::to_string(throughput[2]) + " MiB/s");
    }
    return Outcome::ok();
}

}  // namespace lsw::suites::sanity::data
