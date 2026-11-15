#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::present {
class TimingChecks {
public:
    [[nodiscard]] static Outcome vsync_interval_equals_refresh(Context& context);

    [[nodiscard]] static Outcome interval_two(Context& context);

    [[nodiscard]] static Outcome immediate_ordering(Context& context);

    [[nodiscard]] static Outcome frame_latency_limits(Context& context);

    [[nodiscard]] static Outcome present_stats_vs_capture(Context& context);

    [[nodiscard]] static Outcome high_refresh_rates(Context& context);
};
}  // namespace lsw::suites::sanity::present
