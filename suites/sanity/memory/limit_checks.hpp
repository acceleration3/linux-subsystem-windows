#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::memory {
class LimitChecks {
public:
    [[nodiscard]] static Outcome allocate_up_to_reported(Context& context);

    [[nodiscard]] static Outcome fail_beyond_reported(Context& context);

    [[nodiscard]] static Outcome single_allocation_max(Context& context);

    [[nodiscard]] static Outcome zero_size(Context& context);

    [[nodiscard]] static Outcome after_failure_usable(Context& context);

    [[nodiscard]] static Outcome freed_becomes_usable(Context& context);

    [[nodiscard]] static Outcome fragmented_pattern(Context& context);
};
}  // namespace lsw::suites::sanity::memory
