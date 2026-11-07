#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::sync {
class OrderChecks {
public:
    [[nodiscard]] static Outcome readback_after_signal(Context& context);

    [[nodiscard]] static Outcome flush_makes_visible(Context& context);

    [[nodiscard]] static Outcome queue_order_in_order(Context& context);

    [[nodiscard]] static Outcome out_of_order_events(Context& context);
};
}  // namespace lsw::suites::sanity::sync
