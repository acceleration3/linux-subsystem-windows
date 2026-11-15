#include "sanity/diag/diag_checks.hpp"

#include "support/api_loop.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/process_sampler.hpp"
#include "support/stress_load.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace lsw::suites::sanity::diag {

namespace {

using Clock = std::chrono::steady_clock;

std::filesystem::path log_root() {
    const char* data = std::getenv("ProgramData");
    return std::filesystem::path(data != nullptr ? data : "C:\\ProgramData") / "av";
}

struct LogScan {
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t lines = 0;
    std::uint64_t matches = 0;
    std::vector<std::string> samples;
};

LogScan scan(const std::vector<std::string>& keywords) {
    LogScan result;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(log_root(), std::filesystem::directory_options::skip_permission_denied, error)) {
        if (!entry.is_regular_file(error) || entry.path().extension() != ".log") {
            continue;
        }
        ++result.files;
        result.bytes += static_cast<std::uint64_t>(entry.file_size(error));
        std::ifstream file(entry.path());
        std::string line;
        while (std::getline(file, line)) {
            ++result.lines;
            for (const std::string& keyword : keywords) {
                if (line.find(keyword) != std::string::npos) {
                    ++result.matches;
                    if (result.samples.size() < 5) {
                        result.samples.push_back(line.substr(0, 200));
                    }
                    break;
                }
            }
        }
    }
    return result;
}

Outcome record(Context& context, const std::string& name, const std::vector<std::string>& keywords, bool workload) {
    const LogScan before = scan(keywords);
    if (workload) {
        static_cast<void>(StressLoad::roundtrips(context, 6.0, {4096, 1u << 20, 16u << 20}));
    }
    const LogScan after = scan(keywords);
    context.measure(name + ".log_files", static_cast<long long>(after.files));
    context.measure(name + ".log_bytes", static_cast<long long>(after.bytes));
    context.measure(name + ".matches_before", static_cast<long long>(before.matches));
    context.measure(name + ".matches_after", static_cast<long long>(after.matches));
    context.measure(name + ".matches_delta", static_cast<long long>(after.matches) - static_cast<long long>(before.matches));
    for (std::size_t i = 0; i < after.samples.size(); ++i) {
        context.measure(name + ".sample" + std::to_string(i), after.samples[i]);
    }
    return Outcome::ok();
}

}  // namespace

Outcome DiagChecks::transfer_splits(Context& context) {
    return record(context, "splits", {"split", "piece", "chunk"}, true);
}

Outcome DiagChecks::transfer_arena_size(Context& context) {
    return record(context, "arena", {"arena", "staging", "slice size"}, true);
}

Outcome DiagChecks::transfer_refusals(Context& context) {
    return record(context, "refusals", {"refus", "retry", "retried", "rejected"}, true);
}

Outcome DiagChecks::transfer_slice_waits(Context& context) {
    return record(context, "slice_waits", {"slice wait", "waiting for slice", "slice busy"}, true);
}

Outcome DiagChecks::memory_lent_pages(Context& context) {
    return record(context, "lent", {"lent", "lend", "returned pages"}, true);
}

Outcome DiagChecks::memory_host_window_use(Context& context) {
    return record(context, "host_window", {"window", "occupancy"}, true);
}

Outcome DiagChecks::stats_channel_counters(Context& context) {
    return record(context, "channel", {"channel", "counter", "stats"}, true);
}

Outcome DiagChecks::registry_object_table(Context& context) {
    const ProcessSample before = ProcessSampler::take();
    std::vector<HANDLE> events;
    for (int i = 0; i < 20000; ++i) {
        events.push_back(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    }
    const ProcessSample during = ProcessSampler::take();
    for (HANDLE event : events) {
        ::CloseHandle(event);
    }
    const ProcessSample after = ProcessSampler::take();
    context.measure("handles_before", static_cast<long long>(before.handles));
    context.measure("handles_during", static_cast<long long>(during.handles));
    context.measure("handles_after", static_cast<long long>(after.handles));
    context.measure("private_bytes_growth", static_cast<long long>(during.private_bytes) - static_cast<long long>(before.private_bytes));
    return record(context, "object_table", {"registry", "object table", "objects"}, false);
}

Outcome DiagChecks::sched_queue_depth(Context& context) {
    return record(context, "queue_depth", {"queue depth", "queued", "depth"}, true);
}

Outcome DiagChecks::latency_roundtrip_percentiles(Context& context) {
    Problems unused;
    ApiLoop::run(unused, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        for (std::uint64_t size : {4096ull, 65536ull, 1ull << 20, 16ull << 20}) {
            AllocStatus status = AllocStatus::Ok;
            std::string error;
            std::unique_ptr<GpuBuffer> buffer = backend.create(size, BufferKind::Device, status, error);
            if (buffer == nullptr) {
                continue;
            }
            std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
            std::vector<std::uint8_t> back(data.size());
            Pattern::fill(size, 0, data.data(), data.size());
            std::vector<double> times;
            for (int i = 0; i < 100; ++i) {
                const auto begin = Clock::now();
                if (!backend.upload(*buffer, 0, data.data(), size, error) || !backend.finish(error) || !backend.download(*buffer, 0, back.data(), size, error)) {
                    break;
                }
                times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
            }
            if (times.empty()) {
                continue;
            }
            std::sort(times.begin(), times.end());
            const std::string key = api + "." + std::to_string(size) + ".";
            context.measure(key + "p50_ms", times[times.size() / 2]);
            context.measure(key + "p90_ms", times[times.size() * 9 / 10]);
            context.measure(key + "p99_ms", times[std::min(times.size() - 1, times.size() * 99 / 100)]);
        }
    });
    return Outcome::ok();
}

}  // namespace lsw::suites::sanity::diag
