#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::capture {
class CadenceChecks {
public:
    [[nodiscard]] static Outcome wgc_cadence_matches_present(Context& context);

    [[nodiscard]] static Outcome duplication_cadence(Context& context);

    [[nodiscard]] static Outcome latency_present_to_capture(Context& context);

    [[nodiscard]] static Outcome steady_no_drift(Context& context);
};
}  // namespace lsw::suites::sanity::capture
