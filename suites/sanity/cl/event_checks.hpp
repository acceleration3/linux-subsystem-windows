#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class EventChecks {
public:
    [[nodiscard]] static Outcome in_order_ordering(Context& context);

    [[nodiscard]] static Outcome out_of_order_deps(Context& context);

    [[nodiscard]] static Outcome user_events_callbacks(Context& context);

    [[nodiscard]] static Outcome profiling_monotonic(Context& context);
};
}  // namespace lsw::suites::sanity::cl
