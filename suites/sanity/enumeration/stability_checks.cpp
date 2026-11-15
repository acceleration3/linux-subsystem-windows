#include "sanity/enumeration/stability_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/api_snapshot.hpp"
#include "support/av_adapters.hpp"
#include "support/child_runs.hpp"
#include "support/display_devices.hpp"
#include "support/dyn_library.hpp"
#include "support/kmt_api.hpp"
#include "support/problems.hpp"

#include <d3d11.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace lsw::suites::sanity::enumeration {

namespace {

std::string kmt_text(const std::vector<KmtAdapter>& adapters) {
    std::string out;
    for (std::size_t i = 0; i < adapters.size(); ++i) {
        out += "kmt." + std::to_string(i) + " luid=" +
               Text::hex(static_cast<std::uint32_t>(adapters[i].luid.HighPart), 8) + ":" +
               Text::hex(adapters[i].luid.LowPart, 8) + "\n";
    }
    return out;
}

std::string setupapi_text() {
    std::string out;
    for (const DisplayDevice& device : DisplayDevices::enumerate(true)) {
        out += Text::utf8(device.instance_id) + " " + device.name + " " + device.driver_version + "\n";
    }
    return out;
}

bool light_snapshot(KmtApi& kmt, std::string& text, std::string& error) {
    std::vector<AvAdapter> adapters;
    std::vector<KmtAdapter> kmt_adapters;
    if (!AvAdapters::all(adapters, error) || !kmt.enum_adapters(kmt_adapters, error)) {
        return false;
    }
    text = ApiSnapshot::describe_dxgi(adapters, false) + kmt_text(kmt_adapters);
    return true;
}

std::string first_difference(const std::string& expected, const std::string& actual) {
    std::size_t line_start = 0;
    std::size_t at = 0;
    while (at < expected.size() && at < actual.size() && expected[at] == actual[at]) {
        if (expected[at] == '\n') {
            line_start = at + 1;
        }
        ++at;
    }
    const auto line = [&](const std::string& text) {
        const std::size_t end = text.find('\n', line_start);
        return text.substr(line_start, end == std::string::npos ? std::string::npos : end - line_start);
    };
    return "expected '" + line(expected) + "' got '" + line(actual) + "'";
}

}  // namespace

Outcome StabilityChecks::repeat_calls(Context& context) {
    ApiSnapshot first;
    std::string error;
    if (!ApiSnapshot::take(first, error)) {
        return Outcome::fail(error);
    }
    const std::string setupapi = setupapi_text();
    Problems problems;
    for (int round = 1; round < 100; ++round) {
        ApiSnapshot next;
        if (!ApiSnapshot::take(next, error)) {
            return Outcome::fail("round " + std::to_string(round) + ": " + error);
        }
        const auto compare = [&](const char* api, const std::string& a, const std::string& b) {
            if (a != b) {
                problems.add(std::string(api) + " changed in round " + std::to_string(round) + ": " +
                             first_difference(a, b));
            }
        };
        compare("DXGI", first.dxgi, next.dxgi);
        compare("D3DKMT", first.kmt, next.kmt);
        compare("Vulkan", first.vulkan, next.vulkan);
        compare("OpenCL", first.opencl, next.opencl);
        compare("SetupAPI", setupapi, setupapi_text());
        if (!problems.empty()) {
            break;
        }
    }
    context.measure("snapshot", first.all());
    return problems.outcome();
}

Outcome StabilityChecks::across_processes(Context& context) {
    ApiSnapshot own;
    std::string error;
    if (!ApiSnapshot::take(own, error)) {
        return Outcome::fail(error);
    }
    const std::string snapshot = own.all();
    context.measure("snapshot", snapshot);
    if (ChildMode::active()) {
        return Outcome::ok();
    }
    constexpr int kProcesses = 8;
    std::vector<CheckResult> results(kProcesses + 1);
    std::vector<std::thread> threads;
    for (int i = 0; i < kProcesses; ++i) {
        threads.emplace_back([&results, i] {
            results[static_cast<std::size_t>(i)] =
                ChildRuns::run_self("sanity.enum.stable.across_processes", {});
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    results[kProcesses] = ChildRuns::run_self("sanity.enum.stable.across_processes", {});
    Problems problems;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const std::string text = ChildRuns::text(results[i], "snapshot");
        if (text.empty()) {
            problems.add("process " + std::to_string(i) + " reported no snapshot: " + results[i].reason);
        } else if (text != snapshot) {
            problems.add("process " + std::to_string(i) + " differs: " + first_difference(snapshot, text));
        }
    }
    return problems.outcome();
}

Outcome StabilityChecks::across_bitness(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    KmtApi kmt;
    std::vector<KmtAdapter> kmt_adapters;
    if (!AvAdapters::all(adapters, error) || !kmt.load(error) || !kmt.enum_adapters(kmt_adapters, error)) {
        return Outcome::fail(error);
    }
    const std::string identity = ApiSnapshot::describe_dxgi(adapters, true) + kmt_text(kmt_adapters);
    context.measure("identity", identity);
    if (ChildMode::active()) {
        return Outcome::ok();
    }
    CheckResult sibling;
    if (!ChildRuns::run_sibling("sanity.enum.stable.across_bitness", {}, sibling, error)) {
        return Outcome::fail(error);
    }
    const std::string other = ChildRuns::text(sibling, "identity");
    if (other != identity) {
        return Outcome::fail("the other bitness differs: " + first_difference(identity, other));
    }
    return Outcome::ok();
}

Outcome StabilityChecks::adapter_order_matches_primary(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::all(adapters, error)) {
        return Outcome::fail(error);
    }
    const HMONITOR primary = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (primary == nullptr || ::GetMonitorInfoW(primary, &info) == FALSE) {
        return Outcome::fail("the primary monitor cannot be queried");
    }
    Problems problems;
    std::string primary_text;
    int owners = 0;
    for (std::size_t i = 0; i < adapters.size(); ++i) {
        for (const AvOutput& output : adapters[i].outputs) {
            if (output.monitor == primary) {
                ++owners;
                primary_text = "adapter " + std::to_string(i) + " luid=" + adapters[i].luid_text() +
                               " output=" + output.device_name;
                if (Text::upper(output.device_name) != Text::upper(Text::utf8(info.szDevice))) {
                    problems.add("DXGI output " + output.device_name + " owns the primary monitor but GDI names it " +
                                 Text::utf8(info.szDevice));
                }
            }
        }
    }
    if (owners != 1) {
        problems.add("the primary monitor belongs to " + std::to_string(owners) + " DXGI outputs");
    }
    std::string order;
    for (std::size_t i = 0; i < adapters.size(); ++i) {
        order += "adapter " + std::to_string(i) + " luid=" + adapters[i].luid_text() + " outputs=";
        for (const AvOutput& output : adapters[i].outputs) {
            order += output.device_name + ",";
        }
        order += "\n";
    }
    context.measure("primary", primary_text);
    context.measure("order", order);
    if (ChildMode::active()) {
        return problems.outcome();
    }
    CheckResult sibling;
    if (!ChildRuns::run_sibling("sanity.enum.stable.adapter_order_matches_primary", {}, sibling, error)) {
        problems.add(error);
    } else {
        if (ChildRuns::text(sibling, "primary") != primary_text) {
            problems.add("primary output differs between bitnesses: '" + ChildRuns::text(sibling, "primary") +
                         "' vs '" + primary_text + "'");
        }
        if (ChildRuns::text(sibling, "order") != order) {
            problems.add("adapter or output order differs between bitnesses: " +
                         first_difference(order, ChildRuns::text(sibling, "order")));
        }
    }
    return problems.outcome();
}

Outcome StabilityChecks::create_destroy_enumeration(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    KmtApi kmt;
    std::string baseline_light;
    ApiSnapshot baseline;
    if (!AvAdapters::accel(adapters, error) || !kmt.load(error) ||
        !light_snapshot(kmt, baseline_light, error) || !ApiSnapshot::take(baseline, error)) {
        return Outcome::fail(error);
    }
    DynLibrary d3d11;
    if (!d3d11.load("d3d11.dll")) {
        return Outcome::fail("d3d11.dll cannot be loaded");
    }
    auto create = d3d11.symbol<PFN_D3D11_CREATE_DEVICE>("D3D11CreateDevice");
    if (create == nullptr) {
        return Outcome::fail("d3d11.dll has no D3D11CreateDevice");
    }
    const LUID luid = adapters.front().luid;
    std::atomic<bool> stop{false};
    std::atomic<int> created{0};
    std::atomic<int> failed{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([&] {
            while (!stop.load()) {
                IUnknown* adapter = AvAdapters::open_adapter(luid);
                if (adapter == nullptr) {
                    failed.fetch_add(1);
                    continue;
                }
                ID3D11Device* device = nullptr;
                const HRESULT result = create(static_cast<IDXGIAdapter*>(adapter), D3D_DRIVER_TYPE_UNKNOWN,
                                              nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                                              nullptr);
                if (SUCCEEDED(result) && device != nullptr) {
                    created.fetch_add(1);
                    device->Release();
                } else {
                    failed.fetch_add(1);
                }
                adapter->Release();
            }
        });
    }
    Problems problems;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    int rounds = 0;
    while (std::chrono::steady_clock::now() < deadline && problems.empty()) {
        ++rounds;
        std::string text;
        if (!light_snapshot(kmt, text, error)) {
            problems.add("enumeration failed in round " + std::to_string(rounds) + ": " + error);
        } else if (text != baseline_light) {
            problems.add("round " + std::to_string(rounds) + " returned a different list: " +
                         first_difference(baseline_light, text));
        }
        if (rounds % 10 == 0) {
            ApiSnapshot next;
            if (!ApiSnapshot::take(next, error)) {
                problems.add("Vulkan or OpenCL enumeration failed in round " + std::to_string(rounds) + ": " + error);
            } else if (next.all() != baseline.all()) {
                problems.add("full enumeration differs in round " + std::to_string(rounds) + ": " +
                             first_difference(baseline.all(), next.all()));
            }
        }
    }
    stop.store(true);
    for (std::thread& worker : workers) {
        worker.join();
    }
    context.measure("rounds", rounds);
    context.measure("devices.created", created.load());
    context.measure("devices.failed", failed.load());
    if (created.load() == 0) {
        problems.add("no D3D11 device could be created on the adapter while enumerating");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::enumeration
