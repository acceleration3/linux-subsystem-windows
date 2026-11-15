#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class SyncChecks {
public:
    [[nodiscard]] static Outcome fence_ordering(Context& context);

    [[nodiscard]] static Outcome timeout_semantics(Context& context);

    [[nodiscard]] static Outcome finish_flush(Context& context);

    [[nodiscard]] static Outcome timer_samples_primitives(Context& context);

    [[nodiscard]] static Outcome availability_rules(Context& context);
};
}  // namespace lsw::suites::sanity::gl
