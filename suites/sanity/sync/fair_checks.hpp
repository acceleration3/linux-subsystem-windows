#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::sync {
class FairChecks {
public:
    [[nodiscard]] static Outcome contention_no_deadlock(Context& context);

    [[nodiscard]] static Outcome low_rate_not_starved(Context& context);

    [[nodiscard]] static Outcome equal_load_share(Context& context);
};
}  // namespace lsw::suites::sanity::sync
