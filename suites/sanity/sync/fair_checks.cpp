#include "sanity/sync/fair_checks.hpp"

#include "support/api_loop.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace lsw::suites::sanity::sync {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kHeavy = 32ull << 20;

double elapsed_ms(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

std::uint64_t saturate(GpuBackend& ctx, const std::atomic<bool>& stop, std::string& error) {
    AllocStatus status = AllocStatus::Ok;
    std::string detail;
    std::unique_ptr<GpuBuffer> first = ctx.create(kHeavy, BufferKind::Device, status, detail);
    std::unique_ptr<GpuBuffer> second = first != nullptr ? ctx.create(kHeavy, BufferKind::Device, status, detail) : nullptr;
    if (second == nullptr) {
        error = "allocating the heavy buffers failed: " + detail;
        return 0;
    }
    std::uint64_t bytes = 0;
    while (!stop.load()) {
        for (int i = 0; i < 4 && !stop.load(); ++i) {
            if (!ctx.copy(*first, 0, *second, 0, kHeavy, error) || !ctx.copy(*second, 0, *first, 0, kHeavy, error)) {
                return bytes;
            }
            bytes += 2 * kHeavy;
        }
        if (!ctx.finish(error)) {
            return bytes;
        }
    }
    static_cast<void>(ctx.finish(error));
    return bytes;
}

bool tiny_roundtrip(GpuBackend& ctx, GpuBuffer& buffer, std::uint64_t seed, std::string& error) {
    std::uint8_t expected[4096];
    std::uint8_t actual[4096];
    Pattern::fill(seed, 0, expected, sizeof(expected));
    if (!ctx.upload(buffer, 0, expected, sizeof(expected), error) || !ctx.finish(error) || !ctx.download(buffer, 0, actual, sizeof(actual), error)) {
        return false;
    }
    if (std::memcmp(expected, actual, sizeof(expected)) != 0) {
        error = "the tiny roundtrip returned other data";
        return false;
    }
    return true;
}

}  // namespace

Outcome FairChecks::contention_no_deadlock(Context& context) {
    constexpr int kThreads = 8;
    constexpr std::uint64_t kSlice = 1ull << 20;
    Problems problems;
    std::mutex problems_mutex;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::vector<std::unique_ptr<GpuBackend>> contexts;
        for (int i = 0; i < kThreads; ++i) {
            std::unique_ptr<GpuBackend> next = backend.new_context();
            if (next == nullptr) {
                problems.add(api + ": creating context " + std::to_string(i) + " failed");
                return;
            }
            contexts.push_back(std::move(next));
        }
        std::timed_mutex lock_a;
        std::timed_mutex lock_b;
        std::atomic<std::uint64_t> iterations{0};
        std::atomic<std::uint64_t> backoffs{0};
        const auto end = Clock::now() + std::chrono::seconds(8);
        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&, i] {
                GpuBackend& ctx = *contexts[static_cast<std::size_t>(i)];
                std::timed_mutex& first = i % 2 == 0 ? lock_a : lock_b;
                std::timed_mutex& second = i % 2 == 0 ? lock_b : lock_a;
                AllocStatus status = AllocStatus::Ok;
                std::string error;
                std::unique_ptr<GpuBuffer> own = ctx.create(kSlice, BufferKind::Device, status, error);
                std::vector<std::uint8_t> expected(kSlice);
                std::vector<std::uint8_t> actual(kSlice);
                bool ok = own != nullptr;
                std::uint64_t seed = 1000ull * static_cast<std::uint64_t>(i);
                while (ok && Clock::now() < end) {
                    if (!first.try_lock_for(std::chrono::seconds(5))) {
                        backoffs.fetch_add(1);
                        continue;
                    }
                    if (!second.try_lock_for(std::chrono::milliseconds(50))) {
                        first.unlock();
                        backoffs.fetch_add(1);
                        continue;
                    }
                    Pattern::fill(++seed, 0, expected.data(), kSlice);
                    ok = ctx.upload(*own, 0, expected.data(), kSlice, error) && ctx.finish(error) && ctx.download(*own, 0, actual.data(), kSlice, error) &&
                         std::memcmp(expected.data(), actual.data(), kSlice) == 0;
                    if (!ok && error.empty()) {
                        error = "data mismatch";
                    }
                    second.unlock();
                    first.unlock();
                    iterations.fetch_add(1);
                }
                if (!ok) {
                    std::lock_guard lock(problems_mutex);
                    problems.add(api + " thread " + std::to_string(i) + ": " + error);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        context.measure(api + ".iterations", static_cast<long long>(iterations.load()));
        context.measure(api + ".backoffs", static_cast<long long>(backoffs.load()));
        if (iterations.load() < static_cast<std::uint64_t>(kThreads)) {
            problems.add(api + ": only " + std::to_string(iterations.load()) + " lock-protected rounds completed in 8 seconds");
        }
    });
    return problems.outcome();
}

Outcome FairChecks::low_rate_not_starved(Context& context) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::unique_ptr<GpuBackend> light = backend.new_context();
        std::unique_ptr<GpuBackend> heavy = backend.new_context();
        if (light == nullptr || heavy == nullptr) {
            problems.add(api + ": creating the contexts failed");
            return;
        }
        AllocStatus status = AllocStatus::Ok;
        std::string error;
        std::unique_ptr<GpuBuffer> tiny = light->create(4096, BufferKind::Device, status, error);
        if (tiny == nullptr) {
            problems.add(api + ": " + error);
            return;
        }
        std::vector<double> idle;
        for (int i = 0; i < 40; ++i) {
            const auto begin = Clock::now();
            if (!tiny_roundtrip(*light, *tiny, 100 + static_cast<std::uint64_t>(i), error)) {
                problems.add(api + ": idle roundtrip: " + error);
                return;
            }
            idle.push_back(elapsed_ms(begin));
            ::Sleep(10);
        }
        std::sort(idle.begin(), idle.end());
        const double idle_median = idle[idle.size() / 2];
        std::atomic<bool> stop{false};
        std::string heavy_error;
        std::thread load([&] { static_cast<void>(saturate(*heavy, stop, heavy_error)); });
        ::Sleep(300);
        double worst = 0;
        for (int i = 0; i < 300; ++i) {
            const auto begin = Clock::now();
            if (!tiny_roundtrip(*light, *tiny, 500 + static_cast<std::uint64_t>(i), error)) {
                problems.add(api + ": loaded roundtrip " + std::to_string(i) + ": " + error);
                break;
            }
            worst = std::max(worst, elapsed_ms(begin));
            ::Sleep(10);
        }
        stop.store(true);
        load.join();
        context.measure(api + ".idle_median_ms", idle_median);
        context.measure(api + ".loaded_worst_ms", worst);
        const double bound = idle_median * 50.0 + 500.0;
        if (worst > bound) {
            problems.add(api + ": a tiny job took " + std::to_string(worst) + " ms next to a saturating context, idle median " + std::to_string(idle_median) + " ms, bound " + std::to_string(bound) + " ms");
        }
    });
    return problems.outcome();
}

Outcome FairChecks::equal_load_share(Context& context) {
    Problems problems;
    ApiLoop::run(problems, GpuBackends::names(), [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        std::unique_ptr<GpuBackend> a = backend.new_context();
        std::unique_ptr<GpuBackend> b = backend.new_context();
        if (a == nullptr || b == nullptr) {
            problems.add(api + ": creating the contexts failed");
            return;
        }
        std::atomic<bool> stop{false};
        std::uint64_t bytes_a = 0;
        std::uint64_t bytes_b = 0;
        std::string error_a;
        std::string error_b;
        std::thread first([&] { bytes_a = saturate(*a, stop, error_a); });
        std::thread second([&] { bytes_b = saturate(*b, stop, error_b); });
        ::Sleep(6000);
        stop.store(true);
        first.join();
        second.join();
        if (!error_a.empty() || !error_b.empty()) {
            problems.add(api + ": a saturating context failed: " + error_a + error_b);
            return;
        }
        const std::uint64_t total = bytes_a + bytes_b;
        context.measure(api + ".context_a_bytes", static_cast<long long>(bytes_a));
        context.measure(api + ".context_b_bytes", static_cast<long long>(bytes_b));
        if (total == 0 || bytes_a * 3 < total || bytes_b * 3 < total) {
            problems.add(api + ": the contexts moved " + std::to_string(bytes_a) + " and " + std::to_string(bytes_b) + " bytes, one is below a third of the combined throughput");
        }
    });
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::sync
