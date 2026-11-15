#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class ExtChecks {
public:
    [[nodiscard]] static Outcome string_matches_entry_points(Context& context);

    [[nodiscard]] static Outcome unadvertised_not_supported(Context& context);

    [[nodiscard]] static Outcome version_implies_core_functions(Context& context);

    [[nodiscard]] static Outcome external_memory_caps(Context& context);

    [[nodiscard]] static Outcome getError_contracts(Context& context);

    [[nodiscard]] static Outcome debug_output(Context& context);
};
}  // namespace lsw::suites::sanity::gl
