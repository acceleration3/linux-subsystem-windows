#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class KernelChecks {
public:
    [[nodiscard]] static Outcome atomics_vs_cpu(Context& context);

    [[nodiscard]] static Outcome local_memory_barrier(Context& context);

    [[nodiscard]] static Outcome args_and_ndrange(Context& context);

    [[nodiscard]] static Outcome large_workloads(Context& context);
};
}  // namespace lsw::suites::sanity::cl
