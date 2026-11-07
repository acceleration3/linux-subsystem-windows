#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::enumeration {
class StabilityChecks {
public:
    [[nodiscard]] static Outcome repeat_calls(Context& context);

    [[nodiscard]] static Outcome across_processes(Context& context);

    [[nodiscard]] static Outcome across_bitness(Context& context);

    [[nodiscard]] static Outcome adapter_order_matches_primary(Context& context);

    [[nodiscard]] static Outcome create_destroy_enumeration(Context& context);
};
}  // namespace lsw::suites::sanity::enumeration
