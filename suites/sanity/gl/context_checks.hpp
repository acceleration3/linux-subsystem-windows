#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class ContextChecks {
public:
    [[nodiscard]] static Outcome legacy_create(Context& context);

    [[nodiscard]] static Outcome arb_version_profile(Context& context);

    [[nodiscard]] static Outcome flags_granted_or_error(Context& context);

    [[nodiscard]] static Outcome robustness_reset(Context& context);

    [[nodiscard]] static Outcome no_error(Context& context);

    [[nodiscard]] static Outcome share_lists(Context& context);

    [[nodiscard]] static Outcome non_shared_isolation(Context& context);

    [[nodiscard]] static Outcome many_contexts(Context& context);
};
}  // namespace lsw::suites::sanity::gl
