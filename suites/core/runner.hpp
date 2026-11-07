#pragma once

#include "core/check_result.hpp"
#include "core/options.hpp"
#include "core/registry.hpp"
#include "core/report_printer.hpp"
#include "core/machine_lock.hpp"
#include "core/result_log.hpp"

#include <mutex>
#include <vector>

namespace lsw::suites {
class Runner {
public:
    Runner(const Registry& registry, const Options& options);

    [[nodiscard]] int list() const;

    [[nodiscard]] int run();

private:
    struct Planned {
        const Suite* suite;
        const Check* check;
    };

    [[nodiscard]] bool select(std::vector<Planned>& plan, std::size_t& parallel_free,
                              std::size_t& parallel_exclusive, bool& any_serial,
                              std::string& error) const;

    void execute(const std::vector<Planned>& plan, std::size_t begin, std::size_t end, int jobs,
                 bool machine_wide = false);

    void flush_ready();

    const Registry& registry_;
    const Options& options_;
    ReportPrinter printer_;
    ResultLog log_;
    MachineLock machine_lock_;
    std::mutex mutex_;
    std::vector<CheckResult> results_;
    std::vector<bool> done_;
    std::size_t printed_ = 0;
};
}  // namespace lsw::suites
