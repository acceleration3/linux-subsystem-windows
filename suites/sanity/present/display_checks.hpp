#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::present {
class DisplayChecks {
public:
    [[nodiscard]] static Outcome host_receives_sequence(Context& context);

    [[nodiscard]] static Outcome output_stays_active(Context& context);

    [[nodiscard]] static Outcome idle_then_present(Context& context);

    [[nodiscard]] static Outcome input_to_photon_monotonic(Context& context);
};
}  // namespace lsw::suites::sanity::present
