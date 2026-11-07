#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class SupportChecks {
public:
    [[nodiscard]] static Outcome support_bits_match_use(Context& context);

    [[nodiscard]] static Outcome cross_api_equal(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
