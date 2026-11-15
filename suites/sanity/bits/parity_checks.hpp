#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::bits {
class ParityChecks {
public:
    [[nodiscard]] static Outcome operations_same_result(Context& context);

    [[nodiscard]] static Outcome integer_widths(Context& context);

    [[nodiscard]] static Outcome data_roundtrip(Context& context);

    [[nodiscard]] static Outcome error_codes(Context& context);
};
}  // namespace lsw::suites::sanity::bits
