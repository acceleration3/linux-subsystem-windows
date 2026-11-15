#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::present {
class PacingChecks {
public:
    [[nodiscard]] static Outcome stutter_per_path(Context& context);

    [[nodiscard]] static Outcome pacing_under_gpu_load(Context& context);

    [[nodiscard]] static Outcome pacing_with_parallel_presenters(Context& context);

    [[nodiscard]] static Outcome present_to_display(Context& context);

    [[nodiscard]] static Outcome mirror_host_cadence(Context& context);
};
}  // namespace lsw::suites::sanity::present
