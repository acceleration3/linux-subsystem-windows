#include "core/runner.hpp"

#include "core/check_filter.hpp"
#include "core/exe_identity.hpp"
#include "core/json_report.hpp"
#include "core/run_guard.hpp"
#include "core/worker.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>
#include <thread>

namespace lsw::suites {

namespace {

constexpr int kDefaultTimeoutSeconds = 60;
constexpr int kMaxDefaultJobs = 16;

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buffer;
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& part : parts) {
        out += out.empty() ? "" : ",";
        out += part;
    }
    return out;
}

}  // namespace

Runner::Runner(const Registry& registry, const Options& options)
    : registry_(registry), options_(options), printer_(ExeIdentity::name()) {}

int Runner::list() const {
    for (const Suite& suite : registry_.suites()) {
        for (const Check& check : suite.checks()) {
            std::printf("%s suite=%s policy=%s tags=%s%s\n", check.id.c_str(),
                        suite.name().c_str(),
                        suite.policy() == Policy::Parallel ? "parallel" : "serial",
                        join(check.tags).c_str(), check.exclusive ? " exclusive" : "");
        }
    }
    return 0;
}

bool Runner::select(std::vector<Planned>& plan, std::size_t& parallel_free,
                    std::size_t& parallel_exclusive, std::size_t& serial_end, bool& any_serial,
                    std::string& error) const {
    const CheckFilter filter(options_.filter, options_.tag);
    std::vector<Planned> free_checks;
    std::vector<Planned> exclusive_checks;
    std::vector<Planned> serial_checks;
    std::vector<Planned> pacing_checks;
    for (const std::string& name : options_.suites) {
        const Suite* suite = registry_.find_suite(name);
        if (suite == nullptr) {
            error = "unknown suite: " + name;
            return false;
        }
        for (const Check& check : suite->checks()) {
            if (!filter.accepts(check.id, check.tags)) {
                continue;
            }
            const bool pacing =
                std::find(check.tags.begin(), check.tags.end(), "pacing") != check.tags.end();
            if (pacing ? options_.phase == "main" : options_.phase == "pacing") {
                continue;
            }
            std::vector<Planned>& target =
                pacing                            ? pacing_checks
                : suite->policy() == Policy::Serial ? serial_checks
                : check.exclusive                 ? exclusive_checks
                                                  : free_checks;
            for (int round = 0; round < options_.repeat; ++round) {
                target.push_back({suite, &check});
            }
        }
    }
    if (options_.shuffle) {
        std::mt19937 random(*options_.shuffle);
        std::shuffle(free_checks.begin(), free_checks.end(), random);
        std::shuffle(exclusive_checks.begin(), exclusive_checks.end(), random);
        std::shuffle(pacing_checks.begin(), pacing_checks.end(), random);
    }
    any_serial = !serial_checks.empty() ||
                 std::any_of(options_.suites.begin(), options_.suites.end(),
                             [&](const std::string& name) {
                                 return registry_.find_suite(name)->policy() == Policy::Serial;
                             });
    plan = free_checks;
    parallel_free = plan.size();
    plan.insert(plan.end(), exclusive_checks.begin(), exclusive_checks.end());
    parallel_exclusive = plan.size();
    plan.insert(plan.end(), serial_checks.begin(), serial_checks.end());
    serial_end = plan.size();
    plan.insert(plan.end(), pacing_checks.begin(), pacing_checks.end());
    if (plan.empty()) {
        error = "no check matches";
        return false;
    }
    return true;
}

void Runner::flush_ready() {
    while (printed_ < results_.size() && done_[printed_]) {
        printer_.print(results_[printed_]);
        ++printed_;
    }
}

void Runner::write_progress(bool complete, bool force) {
    if (options_.json.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - last_progress_ < std::chrono::seconds(3)) {
        return;
    }
    last_progress_ = now;
    std::vector<CheckResult> finished;
    for (std::size_t i = 0; i < results_.size(); ++i) {
        if (done_[i]) {
            finished.push_back(results_[i]);
        }
    }
    const JsonReport report(ExeIdentity::name(), started_);
    if (!report.write(options_.json, finished, complete) && complete) {
        std::fprintf(stderr, "%s: cannot write %s\n", ExeIdentity::name().c_str(),
                     options_.json.string().c_str());
    }
}

void Runner::execute(const std::vector<Planned>& plan, std::size_t begin, std::size_t end,
                     int jobs, bool machine_wide) {
    const Worker worker(options_.artifacts);
    std::atomic<std::size_t> next{begin};
    const auto loop = [&] {
        for (;;) {
            const std::size_t index = next.fetch_add(1);
            if (index >= end) {
                return;
            }
            const Planned& planned = plan[index];
            const int timeout = options_.timeout_seconds > 0 ? options_.timeout_seconds
                                : planned.check->timeout_seconds > 0
                                    ? planned.check->timeout_seconds
                                    : kDefaultTimeoutSeconds;
            std::string started;
            CheckResult result;
            if (machine_wide) {
                const std::lock_guard<MachineLock> held(machine_lock_);
                started = utc_now();
                result = worker.run(planned.check->id, timeout);
            } else {
                started = utc_now();
                result = worker.run(planned.check->id, timeout);
            }
            result.suite = planned.suite->name();
            result.tags = planned.check->tags;
            result.started = started;
            const std::lock_guard<std::mutex> lock(mutex_);
            log_.append(result);
            results_[index] = std::move(result);
            done_[index] = true;
            flush_ready();
            write_progress(false, false);
        }
    };
    const std::size_t count = end - begin;
    const std::size_t threads = std::min(static_cast<std::size_t>(std::max(jobs, 1)), count);
    std::vector<std::thread> pool;
    for (std::size_t i = 1; i < threads; ++i) {
        pool.emplace_back(loop);
    }
    loop();
    for (std::thread& thread : pool) {
        thread.join();
    }
}

int Runner::run() {
    std::vector<Planned> plan;
    std::size_t free_end = 0;
    std::size_t exclusive_end = 0;
    std::size_t serial_end = 0;
    bool any_serial = false;
    std::string error;
    if (!select(plan, free_end, exclusive_end, serial_end, any_serial, error)) {
        std::fprintf(stderr, "%s: %s\n", ExeIdentity::name().c_str(), error.c_str());
        return 2;
    }
    RunGuard guard;
    if (!guard.acquire(any_serial, error)) {
        std::fprintf(stderr, "%s: refusing to start: %s\n", ExeIdentity::name().c_str(),
                     error.c_str());
        return 2;
    }
    int jobs = options_.jobs;
    if (jobs <= 0) {
        jobs = static_cast<int>(
            std::min<unsigned>(std::max(1u, std::thread::hardware_concurrency()), kMaxDefaultJobs));
    }
    std::error_code ignored;
    std::filesystem::create_directories(options_.artifacts, ignored);
    results_.assign(plan.size(), {});
    done_.assign(plan.size(), false);
    printed_ = 0;
    started_ = utc_now();
    last_progress_ = std::chrono::steady_clock::now();
    if (!options_.json.empty()) {
        std::filesystem::path log_path = options_.json;
        log_path.replace_extension(".jsonl");
        if (!log_.open(log_path)) {
            std::fprintf(stderr, "%s: cannot write %s\n", ExeIdentity::name().c_str(), log_path.string().c_str());
        }
    }
    execute(plan, 0, free_end, jobs);
    execute(plan, free_end, exclusive_end, 1, true);
    execute(plan, exclusive_end, serial_end, 1);
    execute(plan, serial_end, plan.size(), 1, true);

    int passed = 0;
    int failed = 0;
    int skipped = 0;
    for (const CheckResult& result : results_) {
        if (result.status == Status::Ok) {
            ++passed;
        } else if (result.status == Status::Skip) {
            ++skipped;
        } else {
            ++failed;
        }
    }
    printer_.summary(passed, failed, skipped);
    if (!options_.json.empty()) {
        const JsonReport report(ExeIdentity::name(), started_);
        if (!report.write(options_.json, results_, true)) {
            std::fprintf(stderr, "%s: cannot write %s\n", ExeIdentity::name().c_str(),
                         options_.json.string().c_str());
            return 2;
        }
    }
    return failed > 0 ? 1 : 0;
}

}  // namespace lsw::suites
